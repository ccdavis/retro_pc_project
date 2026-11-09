/* klatt_fx.h - integer-only Klatt cascade/parallel formant synthesizer.
 *
 * A fixed-point port of parwave.c from klatt 3.04 (Dennis Klatt's Fortran,
 * C by Jon Iles and Nick Ing-Simmons, later cleaned up by Reece H. Dunn;
 * GPL).  See NOTES.md for what was kept and what was cut.
 *
 * Design rules (this file is meant to compile with Open Watcom -3 in 16-bit
 * real mode as well as with a host C99 compiler):
 *   - no floating point, no malloc, no stdio
 *   - every 32-bit quantity is spelled int32_t/uint32_t ("int" may be 16 bit)
 *   - no recursion, no large stack arrays
 *   - tables are const arrays in klatt_tab.h (2680 bytes total)
 *
 * Number formats:
 *   resonator coefficients   Q12 in int32   (antiresonator: Q10)
 *   audio-path signals       integer, parwave's 16-bit output scale
 *   linear amplitudes        Q12 in int32
 *   glottal pulse            Q12 (includes the 0.028 factor)
 *   noise low-pass state     Q4
 * The resonator sum is evaluated modulo 2^32, which is exact as long as the
 * true output y satisfies |y| < 2^19; parwave's internal transients (the
 * F3/F4 cascade sections boost the glottal closure step ~6x above the output
 * level) reach ~2^16..2^17, so there are two bits of headroom.
 */
#ifndef KLATT_FX_H
#define KLATT_FX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frame control parameters (same meaning as klatt_frame_t in parwave.h;
 * all in Hz / dB / integer units, 16-bit). */
typedef struct {
    int16_t F0hz10;  /* voicing fundamental in tenths of Hz (0 = unvoiced)  */
    int16_t AVdb;    /* amplitude of voicing, dB      0..70 */
    int16_t F1hz, B1hz;
    int16_t F2hz, B2hz;
    int16_t F3hz, B3hz;
    int16_t F4hz, B4hz;
    int16_t F5hz, B5hz;
    int16_t F6hz, B6hz;
    int16_t FNZhz, BNZhz;  /* nasal zero  */
    int16_t FNPhz, BNPhz;  /* nasal pole  */
    int16_t ASP;     /* aspiration dB                  0..70 */
    int16_t Kopen;   /* open-phase samples (at sr)    10..65 */
    int16_t Aturb;   /* breathiness dB                 0..80 */
    int16_t TLTdb;   /* source spectral tilt dB        0..24 */
    int16_t AF;      /* frication dB                   0..70 */
    int16_t Kskew;   /* period skew                    0..40 */
    int16_t A1, B1phz;   /* parallel F1 amplitude / bandwidth */
    int16_t A2, B2phz;
    int16_t A3, B3phz;
    int16_t A4, B4phz;
    int16_t A5, B5phz;
    int16_t A6, B6phz;
    int16_t ANP;     /* parallel nasal pole amplitude dB */
    int16_t AB;      /* bypass path amplitude dB */
    int16_t AVpdb;   /* parallel voicing amplitude dB */
    int16_t Gain0;   /* overall gain dB (60 = unity; 0 -> 57) */
} kfx_frame_t;

typedef struct {
    int32_t a, b, c;   /* Q12 (Q10 for the antiresonator) */
    int32_t p1, p2;    /* delay line */
} kfx_res_t;

/* Option bits for kfx_t.flags (zero = exact port of parwave).
 *
 * Idle parallel sections (gain 0 and delay line 0) are always skipped; that
 * is bit-exact.  The only approximation is:
 */
#define KFX_LITE_SRC1X   0x01  /* run the glottal source once per sample
                                  instead of 4x oversampled + rlp.  Period
                                  and open-phase timing keep their 1/4-sample
                                  resolution (the pulse polynomial is stepped
                                  by 4); only the anti-alias resonator is
                                  replaced by a 1x equivalent (f = 0.38 sr,
                                  bw = 0.252 sr, the same analog pole). */
#define KFX_LITE_NOISE_WHITE 0x02  /* no 0.75 low-pass on the noise source
                                  (kfx_t.noise_k = 0): rsynth's element
                                  amplitudes were tuned for white noise. */
#define KFX_LITE_RLP_F4  0x04  /* with SRC1X: the source low-pass slot is
                                  the cascade F4 section instead (set from
                                  F4hz/B4hz each frame), so an 8 kHz voice
                                  gets F1-F4 at the cost of F1-F3. */
#define KFX_LITE_SRC_DIFF 0x08 /* with SRC1X: the voicing source is the first
                                  difference of the natural pulse (a +6 dB/oct
                                  pre-emphasis of the voice, no fundamental),
                                  scaled by 2^kfx_t.src_diff_sh. */
#define KFX_LITE_OUT_FIR 0x10  /* the output stage is the FIR
                                  y = g (x - k x[-1]) (pre-emphasis, Q10
                                  kfx_t.out_gain / out_k) instead of
                                  parwave's 2-pole output low-pass; same
                                  per-sample cost (the antiresonator form). */
/* The DOS voice: 1x source, white noise, pre-emphasised output.  Measured by
 * Whisper WER (NOTES.md) this is what makes the rsynth element table
 * intelligible at 8 kHz / 8 bits; klt_voice_default() assumes it. */
#define KFX_FLAGS_DOS (KFX_LITE_SRC1X | KFX_LITE_NOISE_WHITE | KFX_LITE_OUT_FIR)

typedef struct {
    /* configuration */
    uint16_t sr;          /* output sample rate */
    uint8_t  nfcascade;   /* number of cascade formants 1..6 */
    uint8_t  flags;       /* KFX_LITE_* */
    uint32_t inv_sr;      /* 2^32 / sr, for Hz -> Q16 fraction of sr */

    /* per-frame linear amplitudes, Q12 */
    int32_t amp_voice, par_amp_voice, amp_aspir, amp_frica;
    int32_t amp_bypas, amp_breth, amp_gain0;

    /* voicing source state */
    int32_t nper;         /* position in period, in 4x samples */
    int32_t T0;           /* period length in 4x samples (4 = unvoiced) */
    int32_t nopen;        /* open phase length in 4x samples */
    int32_t nmod;         /* start of noise amplitude modulation */
    int32_t nrand;        /* last raw random value, -8191..8191 */
    int32_t nlast;        /* noise lowpass state, Q4 */
    int32_t noise_k;      /* noise low-pass coefficient, Q4 (12 = parwave's 0.75, 0 = white) */
    int32_t pulse_a;      /* Q12 (includes the 0.028 factor) */
    int32_t pulse_b;      /* Q12 */
    int32_t vwave;        /* Q12 */
    int32_t vwave_prev;   /* previous vwave (SRC_DIFF) */
    int8_t  src_diff_sh;  /* SRC_DIFF output gain, left shift (default 2) */
    int16_t out_k, out_gain; /* OUT_FIR: pre-emphasis coefficient and gain, Q10 (default 0.9, 4.0) */
    int32_t decay, onemd; /* tilt filter, Q14 */
    int32_t vlast;        /* tilt filter state */
    int32_t glotlast;     /* previous par_glotout */
    int32_t skew;
    int32_t ns;           /* samples rendered since kfx_set_frame() */
    uint32_t seed;        /* LCG state */
    uint8_t par_active;   /* parallel branch: gains or delay lines non-zero */
    uint8_t par_gain_on;  /* parallel branch: any gain non-zero this frame */
    uint8_t rnpp_on;      /* r1p/rnpp worth evaluating this frame */
    uint8_t r56_on;       /* r5p/r6p worth evaluating this frame */

    kfx_frame_t fr;       /* copy of the current frame (AVdb/Gain0 adjusted) */

    kfx_res_t rnpp, r1p, r2p, r3p, r4p, r5p, r6p;
    kfx_res_t r1c, r2c, r3c, r4c, r5c, r6c;
    kfx_res_t rnpc, rnz, rlp, rout;
} kfx_t;

/* Reset state and pick the sample rate (8000 or 11025 are the intended ones;
 * anything up to 20000 works) and the number of cascade formants (1..6). */
void kfx_init(kfx_t *k, uint16_t sr, uint8_t nfcascade, uint8_t flags);

/* Install a new parameter frame.  Call once every frame (typically 10 ms),
 * then kfx_render*() for that many samples.  This is parwave's frame_init. */
void kfx_set_frame(kfx_t *k, const kfx_frame_t *f);

/* Render n samples of 16-bit signed audio. */
void kfx_render16(kfx_t *k, int16_t *out, int16_t n);

/* Render n samples of 8-bit unsigned audio (Sound Blaster style, 128 = 0). */
void kfx_render8(kfx_t *k, uint8_t *out, int16_t n);

/* Random generator shared with the parity harness (returns -8191..8191). */
int32_t kfx_noise_next(kfx_t *k);

#ifdef __cplusplus
}
#endif
#endif /* KLATT_FX_H */
