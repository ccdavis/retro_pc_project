/* wgout.c - eSpeak NG's wavegen command queue, consumed for the integer Klatt
 * synthesizer (klatt_fx) instead of espeak's wavegen.c/klatt.c.
 *
 * What it does, per queue command (see synthesize.c for the producer):
 *   WCMD_KLATT/KLATT2  a spectral frame pair (frame_t fr1 -> fr2 over
 *                      'length' samples): the same parameter mapping as
 *                      espeak's klatt.c SetSynth_Klatt()/Wavegen_Klatt(),
 *                      done in integers, one kfx_frame_t every STEP*64
 *                      espeak samples, rendered by klatt_fx
 *   WCMD_WAVE          a sampled unvoiced consonant from phondata (22050 Hz,
 *                      8 or 16 bit), resampled to the output rate
 *   WCMD_WAVE2         a sample mixed into the voiced synthesis (voiced
 *                      fricatives), resampled likewise
 *   WCMD_PAUSE         silence
 *   WCMD_PITCH/AMPLITUDE/VOICE/EMBEDDED   state, as in wavegen.c
 * All lengths and increments coming from the frontend are in espeak's
 * 22050 Hz sample units ("virtual samples"); the output rate is converted
 * with an exact remainder accumulator.  Integer only.
 *
 * Dropped from espeak's klatt path: F0 flutter, the 64-sample fade in/out
 * at segment ends, echo, the amplitude envelope (klatt.c ignores it too).
 */
#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>
#include "wavegen.h"
#include "synthesize.h"
#include "voice.h"
#include "speech.h"
#include "translate.h"
#include "klatt_fx.h"
#include "espk.h"

#define VRATE 22050    /* the data's sample rate */

/* ---- globals the frontend expects from wavegen.c ---- */
intptr_t wcmdq[N_WCMDQ][4];
int wcmdq_head = 0;
int wcmdq_tail = 0;
int samplerate = 0;
int embedded_value[N_EMBEDDED_VALUES];
int wgout_trace = 0;

const int embedded_default[N_EMBEDDED_VALUES] = { 0, 50, espeakRATE_NORMAL, 100, 50, 0, 0, 0, espeakRATE_NORMAL, 0, 0, 0, 0, 0, 0 };
static const int embedded_max[N_EMBEDDED_VALUES]     = { 0, 0x7fff, 2000, 300, 99, 99, 99, 0, 2000, 0, 0, 0, 0, 4, 0 };

/* set from y = pow(2,x) * 128,  x=-1 to 1 (wavegen.c) */
#define MAX_PITCH_VALUE  101
static const unsigned char pitch_adjust_tab[MAX_PITCH_VALUE+1] = {
	 64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
	 80,  81,  82,  83,  84,  86,  87,  88,  89,  91,  92,  93,  94,  96,  97,  98,
	100, 101, 103, 104, 105, 107, 108, 110, 111, 113, 115, 116, 118, 119, 121, 123,
	124, 126, 128, 130, 132, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153,
	155, 158, 160, 162, 164, 167, 169, 171, 174, 176, 179, 181, 184, 186, 189, 191,
	194, 197, 199, 202, 205, 208, 211, 214, 217, 220, 223, 226, 229, 232, 236, 239,
	242, 246, 249, 252, 254, 255
};

static voice_t *wvoice = NULL;
static voice_t v2;
static int general_amplitude = 60;
static int consonant_amp = 26;
static WGEN_DATA wdata;

/* ---- output configuration and buffer ---- */
static espk_cfg_t cfg;
static kfx_t kfx;
static uint32_t rs_rem;           /* virtual -> output remainder */
static uint32_t rs_step_q16;      /* source samples per output sample, Q16 */
static int32_t level_q8;          /* sample-path level factor from cfg.level_db */
static int fmax;                  /* highest usable formant frequency */

static uint8_t *obuf;
static long olen, ocap;
static long n_frames, n_wave;

/* Resident use (espkd): samples go to a sink as they are produced instead of
 * the growing buffer; the sink may block (coroutine yield).  wgout_abort set
 * (by the sink, on a flush) makes the render loops skip their work so the
 * frontend runs to the end of the clause quickly and cleanly. */
void (*wgout_sink)(const uint8_t *p, int n) = NULL;
volatile int wgout_abort = 0;

static void emit(const uint8_t *p, int n)
{
	if (wgout_sink) {
		if (!wgout_abort) wgout_sink(p, n);
		olen += n;
		return;
	}
	if (olen + n > ocap) {
		ocap = (olen + n) * 2 + 65536;
		obuf = (uint8_t *)realloc(obuf, ocap);
	}
	memcpy(obuf + olen, p, n);
	olen += n;
}

static int virt_to_out(int nvirt)
{
	uint32_t t = rs_rem + (uint32_t)nvirt * (uint32_t)cfg.sr;
	rs_rem = t % VRATE;
	return (int)(t / VRATE);
}

/* dB -> linear, Q8 (parwave's 1 dB table, 87 dB = 32767) */
static const short amptable[88] = {
	   0,      0,     0,     0,     0,     0,     0,    0,     0,    0,   0,   0,  0, 6, 7,
	   8,      9,    10,    11,    13,    14,    16,   18,    20,   22,  25,  28, 32,
	  35,    40,    45,    51,    57,    64,    71,   80,    90,  101, 114, 128,
	 142,   159,   179,   202,   227,   256,   284,  318,   359,  405,
	 455,   512,   568,   638,   719,   881,   911, 1024,  1137, 1276,
	1438,  1622,  1823,  2048,  2273,  2552,  2875, 3244,  3645,
	4096,  4547,  5104,  5751,  6488,  7291,  8192, 9093, 10207,
	11502, 12976, 14582, 16384, 18350, 20644, 23429,
	26214, 29491, 32767
};

/* 20*log10(num/den) in whole dB, for num,den > 0 (frame-rate work).
 * log2 by normalisation plus a 17-entry mantissa table (1/16 steps). */
static const uint8_t log2_frac16[17] = { 0, 22, 42, 61, 79, 96, 112, 128, 143, 157, 171, 184, 197, 209, 221, 233, 244 };  /* log2(1+i/16)*256 */
static int32_t log2_q8(uint32_t x)
{
	int e = 0;
	while (x >= 0x20000UL) { x >>= 1; e++; }
	while (x < 0x10000UL) { x <<= 1; e--; }
	/* x in [1,2) as Q16; fraction table by the top 4 bits */
	{
		uint32_t f = x - 0x10000UL;           /* Q16 fraction */
		int i = (int)(f >> 12);               /* 0..15 */
		int32_t r = (int32_t)(f & 0xFFF);     /* Q12 remainder */
		int32_t lo = log2_frac16[i], hi = log2_frac16[i + 1];
		return ((int32_t)e << 8) + lo + (((hi - lo) * r) >> 12);
	}
}
static int db_ratio(int32_t num, int32_t den)
{
	int32_t d = log2_q8((uint32_t)num) - log2_q8((uint32_t)den);   /* log2 ratio Q8 */
	/* 20 log10(2) = 6.0206 dB per octave */
	return (int)((d * 1541L + (d >= 0 ? 32768L : -32768L)) >> 16);   /* 6.0206*256 = 1541 */
}

/* ---- queue plumbing (wavegen.c) ---- */
void WcmdqStop(void) { wcmdq_head = 0; wcmdq_tail = 0; }
int WcmdqFree(void) { int i = wcmdq_head - wcmdq_tail; if (i <= 0) i += N_WCMDQ; return i; }
int WcmdqUsed(void) { return N_WCMDQ - WcmdqFree(); }
void WcmdqInc(void) { wcmdq_tail++; if (wcmdq_tail >= N_WCMDQ) wcmdq_tail = 0; }
static void WcmdqIncHead(void) { wcmdq_head++; if (wcmdq_head >= N_WCMDQ) wcmdq_head = 0; }

int GetAmplitude(void)
{
	int amp;
	static const unsigned char amp_emphasis[5] = { 16, 16, 10, 16, 22 };
	amp = (embedded_value[EMBED_A])*55/100;
	general_amplitude = amp * amp_emphasis[embedded_value[EMBED_F]] / 16;
	return general_amplitude;
}

void InitBreath(void) {}
void WavegenFini(void) {}
int WavegenFill(void) { wgout_run(); return 1; }

void WavegenInit(int rate, int wavemult_fact)
{
	int ix;
	(void)wavemult_fact;
	wvoice = NULL;
	samplerate = rate;
	wdata.amplitude = 32;
	wdata.amplitude_fmt = 100;
	for (ix = 0; ix < N_EMBEDDED_VALUES; ix++)
		embedded_value[ix] = embedded_default[ix];
}

static int SetWithRange0(int value, int max)
{
	if (value < 0) return 0;
	if (value > max) return max;
	return value;
}

void SetEmbedded(int control, int value)
{
	int sign = 0;
	int command = control & 0x1f;
	if ((control & 0x60) == 0x60) sign = -1;
	else if ((control & 0x60) == 0x40) sign = 1;
	if (command < N_EMBEDDED_VALUES) {
		if (sign == 0) embedded_value[command] = value;
		else embedded_value[command] += (value * sign);
		embedded_value[command] = SetWithRange0(embedded_value[command], embedded_max[command]);
	}
	switch (command) {
	case EMBED_A:
	case EMBED_F:
		general_amplitude = GetAmplitude();
		break;
	}
}

void WavegenSetVoice(voice_t *v)
{
	memcpy(&v2, v, sizeof(v2));
	wvoice = &v2;
	consonant_amp = (v->consonant_amp * 26) / 100;
}

static void SetAmplitude(int length, unsigned char *amp_env, int value)
{
	(void)length; (void)amp_env;
	if (wvoice == NULL) return;
	wdata.amplitude = (value * general_amplitude) / 16;
	wdata.amplitude_v = (wdata.amplitude * wvoice->consonant_ampv * 15) / 100;
}

void SetPitch2(voice_t *voice, int pitch1, int pitch2, int *pitch_base, int *pitch_range)
{
	int base, range, pitch_value;
	if (pitch1 > pitch2) { int x = pitch1; pitch1 = pitch2; pitch2 = x; }
	if ((pitch_value = embedded_value[EMBED_P]) > MAX_PITCH_VALUE) pitch_value = MAX_PITCH_VALUE;
	pitch_value -= embedded_value[EMBED_T];
	if (pitch_value < 0) pitch_value = 0;
	base = (voice->pitch_base * pitch_adjust_tab[pitch_value]) / 128;
	range = (voice->pitch_range * embedded_value[EMBED_R]) / 50;
	base -= (range - voice->pitch_range) * 18;
	*pitch_base = base + (pitch1 * range) / 2;
	*pitch_range = base + (pitch2 * range) / 2 - *pitch_base;
}

static void SetPitch(int length, unsigned char *env, int pitch1, int pitch2)
{
	if (wvoice == NULL) return;
	if ((wdata.pitch_env = env) == NULL) wdata.pitch_env = env_fall;
	wdata.pitch_ix = 0;
	wdata.pitch_inc = (length == 0) ? 0 : (256 * ENV_LEN * STEPSIZE) / length;
	SetPitch2(wvoice, pitch1, pitch2, &wdata.pitch_base, &wdata.pitch_range);
	wdata.pitch = ((wdata.pitch_env[0] * wdata.pitch_range) >> 8) + wdata.pitch_base;
}

/* ---- the Klatt parameter tracks (klatt.c SetSynth_Klatt, integer) ---- */
/* Q8 values and per-STEPSIZE increments */
static int32_t pf_cur[7], pf_inc[7];    /* formant Hz: 0 = nasal zero, 1..6 */
static int32_t pb_cur[4], pb_inc[4];    /* bandwidth Hz, 1..3 */
static int32_t pa_cur[7], pa_inc[7];    /* parallel amplitude dB, 1..6 */
static int32_t kp_cur[5], kp_inc[5];    /* AV, FNZ(unused here), Tilt, Aspr, Skew */
static frame_t prev_fr;
static int have_prev;

static void kfx_reset_all(void)
{
	kfx_init(&kfx, (uint16_t)cfg.sr, (uint8_t)cfg.nfcascade, (uint8_t)cfg.flags);
}

/* KlattReset(0): clear the resonator delay lines only */
static void kfx_reset_tract(void)
{
	kfx_res_t *r[] = { &kfx.rnpp, &kfx.r1p, &kfx.r2p, &kfx.r3p, &kfx.r4p, &kfx.r5p, &kfx.r6p,
	                   &kfx.r1c, &kfx.r2c, &kfx.r3c, &kfx.r4c, &kfx.r5c, &kfx.r6c, &kfx.rnpc, &kfx.rnz };
	unsigned i;
	for (i = 0; i < sizeof(r) / sizeof(r[0]); i++) { r[i]->p1 = 0; r[i]->p2 = 0; }
}

static int32_t ramp_inc(int32_t from, int32_t to, int length)
{
	/* (to - from) in Q8 per STEPSIZE samples */
	if (length <= 0) length = 1;   /* a zero-length frame pair must not divide by zero (upstream klatt.c would) */
	return (int32_t)(((to - from) * 256L * STEPSIZE) / length);
}

static void SetSynth_Klatt(int length, frame_t *fr1, frame_t *fr2)
{
	int ix;
	int32_t v1, v2n;

	if (have_prev) {
		for (ix = 1; ix < 6; ix++) {
			if (prev_fr.ffreq[ix] != fr1->ffreq[ix]) { kfx_reset_tract(); break; }
		}
	}
	memcpy(&prev_fr, fr2, sizeof(prev_fr));
	have_prev = 1;

	for (ix = 0; ix < 5; ix++) {
		if (fr1->frflags & FRFLAG_KLATT) {
			kp_cur[ix] = fr1->klattp[ix] << 8;
			kp_inc[ix] = ramp_inc(fr1->klattp[ix], fr2->klattp[ix], length);
		} else {
			kp_cur[ix] = 0; kp_inc[ix] = 0;
		}
	}
	for (ix = 1; ix < 6; ix++) {
		v1 = (fr1->ffreq[ix] * wvoice->freq[ix]) / 256 + wvoice->freqadd[ix];
		v2n = (fr2->ffreq[ix] * wvoice->freq[ix]) / 256 + wvoice->freqadd[ix];
		pf_cur[ix] = v1 << 8;
		pf_inc[ix] = ramp_inc(v1, v2n, length);
		if (ix < 4) {
			v1 = (fr1->bw[ix] * 2 * wvoice->width[ix]) / 256;
			v2n = fr2->bw[ix] * 2;
			pb_cur[ix] = v1 << 8;
			pb_inc[ix] = ramp_inc(v1, v2n, length);
		}
	}
	/* nasal zero: 0 = same as the nasal pole (no effect) */
	v1 = fr1->klattp[KLATT_FNZ] * 2; if (v1 == 0) v1 = 280;
	v2n = fr2->klattp[KLATT_FNZ] * 2; if (v2n == 0) v2n = 280;
	pf_cur[0] = v1 << 8;
	pf_inc[0] = ramp_inc(v1, v2n, length);

	if (fr1->frflags & FRFLAG_KLATT) {
		for (ix = 1; ix < 7; ix++) {
			pa_cur[ix] = fr1->klatt_ap[ix] << 8;
			pa_inc[ix] = ramp_inc(fr1->klatt_ap[ix], fr2->klatt_ap[ix], length);
		}
	}
}

static void advance_step(void)
{
	int ix;
	for (ix = 0; ix < 7; ix++) { pf_cur[ix] += pf_inc[ix]; pa_cur[ix] += pa_inc[ix]; }
	for (ix = 1; ix < 4; ix++) pb_cur[ix] += pb_inc[ix];
	for (ix = 0; ix < 5; ix++) kp_cur[ix] += kp_inc[ix];
	wdata.pitch_ix += wdata.pitch_inc;
	if ((ix = wdata.pitch_ix >> 8) > 127) ix = 127;
	wdata.pitch = ((wdata.pitch_env[ix] * wdata.pitch_range) >> 8) + wdata.pitch_base;
}

/* ---- mixed wave (voiced fricatives) ---- */
static uint32_t mix_pos;   /* Q16 source sample position */

static int32_t mix_sample(void)
{
	int32_t sample, z2;
	int s = (int)(mix_pos >> 16);
	int bytes = (wdata.mix_wave_scale == 0) ? s * 2 : s;
	if (wdata.mix_wavefile_ix >= wdata.n_mix_wavefile)
		return 0;
	if (bytes + wdata.mix_wavefile_offset >= wdata.mix_wavefile_max)
		wdata.mix_wavefile_offset -= (wdata.mix_wavefile_max * 3) / 4;
	if (wdata.mix_wave_scale == 0) {
		const unsigned char *p = wdata.mix_wavefile + wdata.mix_wavefile_offset + bytes;
		sample = p[0] + ((signed char)p[1] * 256);
	} else {
		sample = (signed char)wdata.mix_wavefile[wdata.mix_wavefile_offset + bytes] * wdata.mix_wave_scale;
	}
	mix_pos += rs_step_q16;
	wdata.mix_wavefile_ix = (wdata.mix_wave_scale == 0) ? (int)(mix_pos >> 16) * 2 : (int)(mix_pos >> 16);
	z2 = sample * wdata.amplitude_v / 1024;
	z2 = (z2 * wdata.mix_wave_amp) / 40;
	return (z2 * level_q8) >> 8;
}

/* ---- one Klatt frame: build kfx_frame_t from the tracks and render ---- */
static const int16_t par_bw[7] = { 59, 59, 89, 149, 200, 200, 500 };

static void klatt_frame(int nvirt)
{
	kfx_frame_t f;
	int nout, g;
	int32_t p;
	memset(&f, 0, sizeof(f));

	p = wdata.pitch; if (p < 102400) p = 102400;   /* 25 Hz minimum */
	f.F0hz10 = (int16_t)((p * 10L) >> 12);
	f.AVdb  = (int16_t)(kp_cur[KLATT_AV] >> 8);
	f.TLTdb = (int16_t)(kp_cur[KLATT_Tilt] >> 8);
	f.ASP   = (int16_t)(kp_cur[KLATT_Aspr] >> 8);
	f.Kskew = (int16_t)(kp_cur[KLATT_Skew] >> 8);
	f.Kopen = (int16_t)cfg.kopen;
	f.F1hz = (int16_t)(pf_cur[1] >> 8); f.B1hz = (int16_t)(pb_cur[1] >> 8);
	f.F2hz = (int16_t)(pf_cur[2] >> 8); f.B2hz = (int16_t)(pb_cur[2] >> 8);
	f.F3hz = (int16_t)(pf_cur[3] >> 8); f.B3hz = (int16_t)(pb_cur[3] >> 8);
	f.F4hz = (int16_t)(pf_cur[4] >> 8); f.B4hz = 200;
	f.F5hz = (int16_t)(pf_cur[5] >> 8); f.B5hz = 200;
	f.F6hz = 6500; f.B6hz = 500;
	if (f.F1hz > fmax) f.F1hz = (int16_t)fmax;
	if (f.F2hz > fmax) f.F2hz = (int16_t)fmax;
	if (f.F3hz > fmax) f.F3hz = (int16_t)fmax;
	if (f.F4hz > fmax) f.F4hz = (int16_t)fmax;
	if (f.F5hz > fmax) f.F5hz = (int16_t)fmax;
	if (f.F6hz > fmax) f.F6hz = (int16_t)fmax;
	if (f.B1hz < 20) f.B1hz = 20;
	if (f.B2hz < 20) f.B2hz = 20;
	if (f.B3hz < 20) f.B3hz = 20;
	f.FNZhz = (int16_t)(pf_cur[0] >> 8); f.BNZhz = 89;
	f.FNPhz = 280; f.BNPhz = 89;
	f.A1 = (int16_t)(pa_cur[1] >> 8); f.B1phz = par_bw[1];
	f.A2 = (int16_t)(pa_cur[2] >> 8); f.B2phz = par_bw[2];
	f.A3 = (int16_t)(pa_cur[3] >> 8); f.B3phz = par_bw[3];
	f.A4 = (int16_t)(pa_cur[4] >> 8); f.B4phz = par_bw[4];
	f.A5 = (int16_t)(pa_cur[5] >> 8); f.B5phz = par_bw[5];
	f.A6 = (int16_t)(pa_cur[6] >> 8); f.B6phz = par_bw[6];
	if ((pf_cur[5] >> 8) > fmax) f.A5 = 0;
	if (6500 > fmax) f.A6 = 0;
	f.ANP = 0; f.AB = 0; f.AVpdb = 0; f.Aturb = 0; f.AF = 0;
	/* espeak: out * wdata.amplitude * amp_gain0; here folded into Gain0 */
	g = 60 + cfg.level_db;
	if (wdata.amplitude > 0) g += db_ratio(wdata.amplitude, cfg.gain_ref);
	else g = 0;
	if (g > 80) g = 80;
	if (g < 4) g = 4;
	f.Gain0 = (int16_t)g;

	if (wgout_trace >= 2)
		fprintf(stderr, "KF f0=%d av=%d tilt=%d asp=%d F=%d/%d %d/%d %d/%d %d %d nz=%d A=%d %d %d %d %d %d g=%d amp=%d n=%d\n",
		        f.F0hz10, f.AVdb, f.TLTdb, f.ASP, f.F1hz, f.B1hz, f.F2hz, f.B2hz, f.F3hz, f.B3hz, f.F4hz, f.F5hz,
		        f.FNZhz, f.A1, f.A2, f.A3, f.A4, f.A5, f.A6, f.Gain0, wdata.amplitude, nvirt);

	kfx_set_frame(&kfx, &f);
	n_frames++;
	nout = virt_to_out(nvirt);
	if (wgout_abort) return;
	while (nout > 0) {
		uint8_t b8[256];
		int n = nout > 256 ? 256 : nout;
		if (wdata.mix_wavefile_ix < wdata.n_mix_wavefile) {
			int16_t b16[256];
			int i;
			kfx_render16(&kfx, b16, (int16_t)n);
			for (i = 0; i < n; i++) {
				int32_t v = b16[i] + mix_sample();
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				b8[i] = (uint8_t)((v >> 8) + 128);
			}
		} else {
			kfx_render8(&kfx, b8, (int16_t)n);
		}
		emit(b8, n);
		nout -= n;
	}
}

static void Wavegen_Klatt(int length, frame_t *fr1, frame_t *fr2)
{
	int done = 0;
	SetSynth_Klatt(length, fr1, fr2);
	while (done < length) {
		int k, nv = 0;
		for (k = 0; k < cfg.step && done < length; k++) {
			int n = length - done;
			if (n > STEPSIZE) n = STEPSIZE;
			nv += n;
			done += n;
		}
		klatt_frame(nv);
		/* advance the tracks by the steps just rendered */
		for (k = 0; k < cfg.step; k++) advance_step();
	}
}

/* ---- sampled consonants ---- */
static void PlayWave(int length, const unsigned char *data, int scale, int amp)
{
	int nout = virt_to_out(length);
	uint32_t pos = 0;
	int32_t g = (int32_t)consonant_amp * general_amplitude * amp;   /* < 2^16 */
	uint8_t b8[256];
	int bi = 0;
	n_wave += nout;
	kfx_reset_all();
	if (wgout_abort) return;
	while (nout-- > 0) {
		int i0 = (int)(pos >> 16), i1 = (int)((pos + rs_step_q16) >> 16), i, cnt;
		int32_t sum = 0, v;
		if (i1 <= i0) i1 = i0 + 1;
		if (i1 > length) i1 = length;
		if (i0 >= length) i0 = length - 1;
		cnt = i1 - i0; if (cnt < 1) cnt = 1;
		for (i = i0; i < i1; i++) {
			if (scale == 0) sum += data[i * 2] + ((signed char)data[i * 2 + 1] * 256);
			else sum += (signed char)data[i] * scale;
		}
		v = sum / cnt;                 /* box-filtered decimation */
		v = (v * g) >> 15;             /* wavegen.c: *consonant_amp*general_amplitude >> 10, *amp/32 */
		v = (v * level_q8) >> 8;
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		b8[bi++] = (uint8_t)((v >> 8) + 128);
		if (bi == 256) { emit(b8, bi); bi = 0; }
		pos += rs_step_q16;
	}
	if (bi) emit(b8, bi);
}

static void PlaySilence(int length)
{
	int nout = virt_to_out(length);
	static const uint8_t z[256] = { [0 ... 255] = 128 };
	kfx_reset_all();
	if (wgout_abort) return;
	while (nout > 0) { int n = nout > 256 ? 256 : nout; emit(z, n); nout -= n; }
}

/* ---- configuration ---- */
void wgout_configure(const espk_cfg_t *c)
{
	cfg = *c;
	rs_step_q16 = ((uint32_t)VRATE << 16) / (uint32_t)cfg.sr;
	fmax = cfg.sr / 2 - 100;
	level_q8 = 256;
	if (cfg.level_db != 0) {
		/* 10^(db/20) as Q8 via the 1 dB table: ratio of two entries */
		int32_t a = amptable[60 + cfg.level_db], b = amptable[60];
		level_q8 = (a * 256L) / b;
	}
	kfx_reset_all();
	wgout_reset_output();
}

void wgout_reset_output(void)
{
	olen = 0;
	rs_rem = 0;
	n_frames = n_wave = 0;
	have_prev = 0;
	wdata.n_mix_wavefile = 0;
	wdata.mix_wavefile_ix = 0;
	memset(pa_cur, 0, sizeof(pa_cur)); memset(pa_inc, 0, sizeof(pa_inc));
	kfx_reset_all();
}

uint8_t *wgout_samples(long *n) { *n = olen; return obuf; }
long wgout_frames(void) { return n_frames; }
long wgout_wave_samples(void) { return n_wave; }

/* ---- the consumer (WavegenFill2) ---- */
void wgout_run(void)
{
	while (WcmdqUsed() > 0) {
		intptr_t *q = wcmdq[wcmdq_head];
		int length = (int)q[1];
		switch (q[0] & 0xff) {
		case WCMD_PITCH:
			SetPitch(length, (unsigned char *)q[2], (int)(q[3] >> 16), (int)(q[3] & 0xffff));
			break;
		case WCMD_PAUSE:
			wdata.n_mix_wavefile = 0;
			wdata.amplitude_fmt = 100;
			PlaySilence(length);
			break;
		case WCMD_WAVE:
			wdata.n_mix_wavefile = 0;
			PlayWave(length, (const unsigned char *)q[2], (int)(q[3] & 0xff), (int)(q[3] >> 8));
			break;
		case WCMD_WAVE2:
			wdata.mix_wave_amp = (int)(q[3] >> 8);
			wdata.mix_wave_scale = (int)(q[3] & 0xff);
			wdata.n_mix_wavefile = (length & 0xffff);
			wdata.mix_wavefile_max = (length >> 16) & 0xffff;
			if (wdata.mix_wave_scale == 0) {
				wdata.n_mix_wavefile *= 2;
				wdata.mix_wavefile_max *= 2;
			}
			wdata.mix_wavefile_ix = 0;
			wdata.mix_wavefile_offset = 0;
			wdata.mix_wavefile = (unsigned char *)q[2];
			mix_pos = 0;
			break;
		case WCMD_KLATT2:
		case WCMD_SPECT2:
			wdata.n_mix_wavefile = 0;
			/* fall through */
		case WCMD_KLATT:
		case WCMD_SPECT:
			Wavegen_Klatt(length & 0xffff, (frame_t *)q[2], (frame_t *)q[3]);
			break;
		case WCMD_MARKER:
			break;
		case WCMD_AMPLITUDE:
			SetAmplitude(length, (unsigned char *)q[2], (int)q[3]);
			break;
		case WCMD_VOICE:
			WavegenSetVoice((voice_t *)q[2]);
			free((voice_t *)q[2]);
			break;
		case WCMD_EMBEDDED:
			SetEmbedded((int)q[1], (int)q[2]);
			break;
		case WCMD_PHONEME_ALIGNMENT:
			free((char *)q[1]);
			break;
		case WCMD_FMT_AMPLITUDE:
			wdata.amplitude_fmt = (int)q[1] ? (int)q[1] : 100;
			break;
		default:
			break;
		}
		WcmdqIncHead();
	}
}
