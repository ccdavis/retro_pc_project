/*
 * engine_espk.c -- ESPK (eSpeak-NG English front end + Q12 integer klatt_fx) on Core 1.
 * API from third_party/espk/espk/espk.h; samples arrive through the
 * wgout_sink function pointer as unsigned 8-bit, a flush raises wgout_abort so
 * the pipeline unwinds without a longjmp (espkd/engine_espk.c).
 *
 * The data pack (ESPK.DAT, 378 KB) is used in place: on the host it is read from
 * a file, on the RP2350 it is an .incbin blob in flash (espk_data_mem.c).
 */
#include <string.h>
#include "doubletalk_core1.h"
#include "espk.h"

#ifndef ESPK_DAT_PATH
#define ESPK_DAT_PATH "ESPK.DAT"
#endif

static espk_cfg_t s_cfg;
static int s_ready = 0;
unsigned long g_espk_samples = 0;

static void discard_sink(const uint8_t *p, int n) { (void)p; (void)n; }
static void sink(const uint8_t *p, int n)
{
    g_espk_samples += (unsigned long)n;
    if (!synth_push_pcm8u(p, n)) wgout_abort = 1;
}

int engine_espk_init(int sample_rate)
{
    espk_default_cfg(&s_cfg);
    s_cfg.sr = sample_rate;
    if (sample_rate > 8000) s_cfg.nfcascade = 4;   /* F1-F4 at 11 kHz, as measured best in NOTES.md */
    s_cfg.level_db = 6;                           /* espk default; +3 dB left ESPK ~14 dB under DECtalk */
    if (espk_init(ESPK_DAT_PATH, &s_cfg) != 0) return -1;
    /* warm-up (espkd/engine_espk.c): every lazily allocated buffer exists before
     * the first real utterance.  ESPK initialises lazily on first use, so this is
     * never on the boot path. */
    wgout_sink = discard_sink;
    espk_set_rate(175); espk_set_pitch(50);
    espk_speak_stream("Warm up: 1,234.5 dollars; the 3rd e-mail from Mr. O'Neil at 10:30 AM (test) & 50% done? Yes!");
    wgout_sink = NULL;
    s_ready = 1;
    return 0;
}

void engine_espk_speak(const char *text, int wpm, int pitch_0_99)
{
    if (!s_ready) return;
    espk_set_rate(wpm);
    espk_set_pitch(pitch_0_99);
    wgout_sink = sink;
    espk_speak_stream(text);
    wgout_sink = NULL;
}
