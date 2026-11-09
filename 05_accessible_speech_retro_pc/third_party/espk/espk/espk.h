/* espk.h - eSpeak NG English frontend driving the integer Klatt synthesizer
 * (klatt_fx).  Host and DJGPP.  See ../NOTES.md. */
#ifndef ESPK_H
#define ESPK_H
#include <stdint.h>

typedef struct {
    int sr;          /* output sample rate: 8000 or 11025 */
    int nfcascade;   /* cascade formants for klatt_fx (3..5) */
    int flags;       /* KFX_LITE_* flags (KFX_FLAGS_DOS) */
    int step;        /* Klatt frames per parameter frame: 1 = every 64/22050 s (2.9 ms), 3 = 8.7 ms */
    int kopen;       /* glottal open phase, samples at sr (klatt_fx Kopen, 10..65) */
    int level_db;    /* overall level trim, dB (0 = calibrated to espeak-ng's own output) */
    int gain_ref;    /* wdata.amplitude that maps to Gain0 = 60 dB (calibration) */
    int speed;       /* words per minute (espeak rate; 175 = espeak default) */
    int pitch;       /* espeak pitch 0..99 (50 default) */
    int trace;       /* 1: print phoneme string, 2: also dump Klatt frames */
} espk_cfg_t;

typedef struct {
    long front_ticks;   /* time in text->queue (translate, lengths, intonation, frames) */
    long synth_ticks;   /* time in queue->samples (klatt_fx, sample playback) */
    long n_samples;
    long n_frames;      /* Klatt parameter frames rendered */
    long n_wave;        /* sampled (unvoiced) output samples */
} espk_stats_t;

/* the caller supplies a monotonic clock for the stats (uclock() on DOS) */
extern long (*espk_clock)(void);

void espk_default_cfg(espk_cfg_t *c);
/* load the packed data (ESPK.DAT) and select the en+klatt voice; 0 = ok */
int espk_init(const char *datafile, const espk_cfg_t *cfg);
/* synthesize text to 8-bit unsigned samples at cfg->sr; the buffer belongs
 * to espk until the next call.  0 = ok */
int espk_speak(const char *text, uint8_t **samples, long *n, espk_stats_t *st);
/* resident (streaming) use: samples go to wgout_sink as produced; returns
 * early when wgout_abort is raised by the sink.  Each call is a fresh text
 * (InitText); the engine state is reset afterwards (SpeakNextClause(2)). */
int espk_speak_stream(const char *text);
void espk_set_rate(int wpm);       /* espeak words per minute */
void espk_set_pitch(int pitch);    /* espeak pitch 0..99 */
/* data pack access (espk_data.c) */
const void *espk_data_get(const char *name, long *len);
int espk_data_owns(const void *p);   /* 1 if p points into the pack (never free it) */

/* wgout.c: output stage configuration and the queue consumer */
void wgout_configure(const espk_cfg_t *cfg);
void wgout_reset_output(void);
void wgout_run(void);
uint8_t *wgout_samples(long *n);
long wgout_frames(void);
long wgout_wave_samples(void);
extern int wgout_trace;
extern void (*wgout_sink)(const uint8_t *p, int n);
extern volatile int wgout_abort;
#endif
