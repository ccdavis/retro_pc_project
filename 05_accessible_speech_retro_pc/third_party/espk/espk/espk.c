/* espk.c - driver: initialise the eSpeak NG subset from the packed data,
 * select en+klatt, and run text -> queue -> samples (speech.c's Synthesize
 * loop without events, callbacks and audio devices). */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>
#include <espeak-ng/encoding.h>
#include "speech.h"
#include "synthesize.h"
#include "synthdata.h"
#include "translate.h"
#include "readclause.h"
#include "setlengths.h"
#include "voice.h"
#include "wavegen.h"
#include "phoneme.h"
#include "common.h"
#include "espk.h"

int espk_data_load(const char *file);
voice_t *LoadVoice(const char *vname, int control);
espeak_ng_STATUS DoVoiceChange(voice_t *v);

static long null_clock(void) { return 0; }
long (*espk_clock)(void) = null_clock;

static espk_cfg_t cfg;


void espk_default_cfg(espk_cfg_t *c)
{
	memset(c, 0, sizeof(*c));
	c->sr = 8000;
	c->nfcascade = 3;
	c->flags = 0x13;          /* KFX_FLAGS_DOS */
	c->step = 3;              /* 8.7 ms Klatt frames */
	c->kopen = 10;            /* espeak's effective value (nopen = 40) */
	c->level_db = 6;
	c->gain_ref = 270;         /* wdata.amplitude giving Gain0 60 dB: matches espeak-ng's own level (tools/levels.py) */
	c->speed = 175;
	c->pitch = 50;
}

int espk_init(const char *datafile, const espk_cfg_t *c)
{
	int param, srate = 22050, r;
	espeak_VOICE sel;
	cfg = *c;
	if ((r = espk_data_load(datafile)) != 0) {
		fprintf(stderr, "espk: cannot load %s (%d)\n", datafile, r);
		return -1;
	}
	if (LoadPhData(&srate, NULL) != ENS_OK) {
		fprintf(stderr, "espk: bad phoneme data\n");
		return -2;
	}
	WavegenInit(srate, 0);
	wgout_configure(&cfg);
	wgout_trace = cfg.trace;
	memset(espeak_GetCurrentVoice(), 0, sizeof(espeak_VOICE));
	SetVoiceStack(NULL, "");
	SynthesizeInit();
	InitNamedata();
	VoiceReset(0);
	for (param = 0; param < N_SPEECH_PARAM; param++)
		param_stack[0].parameter[param] = saved_parameters[param] = param_defaults[param];
	SetParameter(espeakRATE, cfg.speed, 0);
	SetParameter(espeakVOLUME, 100, 0);
	SetParameter(espeakCAPITALS, 0, 0);
	SetParameter(espeakPUNCTUATION, 0, 0);
	SetParameter(espeakWORDGAP, 0, 0);
	SetParameter(espeakPITCH, cfg.pitch, 0);
	option_phonemes = cfg.trace ? 1 : 0;
	option_phoneme_events = 0;
	espeak_srand(1);

	/* espeak_ng_SetVoiceByName("en+klatt") without the voice directory scan */
	if (LoadVoice("en", 1) == NULL) {
		fprintf(stderr, "espk: voice en not found\n");
		return -3;
	}
	LoadVoice("!v/klatt", 2);
	DoVoiceChange(voice);
	memset(&sel, 0, sizeof(sel));
	sel.name = "en+klatt";
	sel.languages = voice->language_name;
	SetVoiceStack(&sel, "!v/klatt");
	wgout_run();          /* consume the WCMD_VOICE queued by DoVoiceChange */
	if (cfg.trace)
		f_trans = stderr;
	p_decoder = create_text_decoder();
	return 0;
}

int espk_speak(const char *text, uint8_t **samples, long *n, espk_stats_t *st)
{
	long t0, t1;
	memset(st, 0, sizeof(*st));
	option_ssml = 0;
	option_phoneme_input = 0;
	option_endpause = 0;
	wgout_reset_output();


	t0 = espk_clock();
	if (text_decoder_decode_string_multibyte(p_decoder, text, translator->encoding, 0) != ENS_OK)
		return -1;
	SpeakNextClause(0);
	t1 = espk_clock(); st->front_ticks += t1 - t0;
	for (;;) {
		t0 = espk_clock();
		wgout_run();
		t1 = espk_clock(); st->synth_ticks += t1 - t0;
		t0 = t1;
		if (Generate(phoneme_list, &n_phoneme_list, 1) == 0) {
			if (WcmdqUsed() == 0) {
				if (SpeakNextClause(1) == 0) {
					t1 = espk_clock(); st->front_ticks += t1 - t0;
					break;
				}
			}
		}
		t1 = espk_clock(); st->front_ticks += t1 - t0;
	}
	*samples = wgout_samples(n);
	st->n_samples = *n;
	st->n_frames = wgout_frames();
	st->n_wave = wgout_wave_samples();
	return 0;
}

int espk_speak_stream(const char *text)
{
	option_ssml = 0;
	option_phoneme_input = 0;
	option_endpause = 0;
	wgout_abort = 0;
	InitText(espeakKEEP_NAMEDATA);
	wgout_reset_output();
	if (text_decoder_decode_string_multibyte(p_decoder, text, translator->encoding, 0) != ENS_OK)
		return -1;
	SpeakNextClause(0);
	for (;;) {
		wgout_run();
		if (wgout_abort) break;
		if (Generate(phoneme_list, &n_phoneme_list, 1) == 0) {
			if (WcmdqUsed() == 0) {
				if (SpeakNextClause(1) == 0)
					break;
			}
		}
	}
	SpeakNextClause(2);          /* n_phoneme_list = 0, WcmdqStop(): clean state for the next text */
	return 0;
}

void espk_set_rate(int wpm) { SetParameter(espeakRATE, wpm, 0); }
void espk_set_pitch(int pitch) { SetParameter(espeakPITCH, pitch, 0); }
