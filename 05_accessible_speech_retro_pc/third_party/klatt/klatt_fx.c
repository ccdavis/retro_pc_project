/* klatt_fx.c - integer-only port of parwave.c (klatt 3.04).
 *
 * Copyright (C) 2011-2015 Reece H. Dunn (parwave.c clean-up)
 * (c) 1993,94 Jon Iles and Nick Ing-Simmons (C re-implementation)
 * Fixed-point port (c) 2026 for the accessible_os FreeDOS speech project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.  See upstream/klatt-3.04/COPYING.
 *
 * What is kept from parwave.c: the NATURAL glottal source run at 4x the
 * sample rate through the rlp downsampling resonator, spectral tilt,
 * breathiness, aspiration, the nasal zero/pole pair, 1..6 cascade formants,
 * the parallel branch (F1, nasal pole, F2..F6, bypass) fed by frication plus
 * the first difference of the parallel voicing, the output resonator and
 * the Gain0 scaling.  Kskew (period skew) is kept.
 *
 * What is cut: IMPULSIVE and SAMPLED glottal sources, F0 flutter, the
 * ALL_PARALLEL model switch (this is always cascade+parallel), the 7th/8th
 * cascade formants (only valid at >= 16 kHz), and stderr warnings.
 *
 * Per-sample cost (callgrind, x86-64, gcc -O2, "Hello..." sentence, see
 * NOTES.md): 4 x (source update + rlp resonator) + nasal zero/pole +
 * nfcascade cascade sections + rout, plus the parallel branch (5..7
 * resonators + bypass) only while it is not idle.  A resonator costs ~15
 * instructions (three 32-bit multiplies).  Measured: 344 instr/sample with
 * the exact 4x source, 283 with KFX_LITE_SRC1X (8 kHz, nfcascade 3), i.e.
 * 2.75 M and 2.26 M instructions per second of audio.  About 26 IMULs per
 * voiced sample (SRC1X), 42 during frication.
 */
#include "klatt_fx.h"
#include "klatt_tab.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define KFX_INLINE inline
#else
#define KFX_INLINE
#endif

/* 32-bit multiply, low 32 bits, well defined on overflow (unsigned wrap). */
#if defined(__WATCOMC__) && defined(__386__) == 0 && !defined(KFX_NO_ASM)
/* 16-bit Watcom calls a helper for every 32-bit multiply; on a 386 one IMUL does it (low 32 bits, wraps mod 2^32) */
int32_t kfx_imul32(int32_t x, int32_t y);      /* klatt/imul32.asm: one 386 IMUL */
#define MUL32(x, y) kfx_imul32((int32_t)(x), (int32_t)(y))
#else
#define MUL32(x, y) ((int32_t)((uint32_t)(x) * (uint32_t)(y)))
#endif
#define SUM3(x, y, z, r) ((int32_t)((uint32_t)(x) + (uint32_t)(y) + (uint32_t)(z) + (uint32_t)(r)))

#define Q12_ONE   4096L

/* Internal signals run at parwave's scale divided by 2^KFX_HEADROOM: the
 * cascade sections near Nyquist boost the glottal closure transient to
 * ~40x the output level, and the resonator sum must stay below 2^31 with
 * Q12 coefficients (|y| < 2^19).  Two bits of headroom keep the audited
 * peak (1.2M on parwave's scale) inside the limit; the output gain puts the
 * bits back. */
#define KFX_HEADROOM 2
#define OUT_MAX   32767L
#define OUT_MIN   (-32767L)

/* factor * 4.096 in Q12: DBtoLIN(dB) * factor in Q12 = (amptab[dB] * F) >> 12 */
#define F_1_000   16777L
#define F_0_600   10066L
#define F_0_400    6711L
#define F_0_250    4194L
#define F_0_150    2517L
#define F_0_100    1678L
#define F_0_060    1007L
#define F_0_050     839L
#define F_0_040     671L
#define F_0_030     503L
#define F_0_022     369L

/* ------------------------------------------------------------------ */
/* Resonators                                                          */

#ifdef KFX_CHECK
/* Host-only overflow audit: evaluate the true sum in 64 bits and count the
 * samples where the modulo-2^32 evaluation would have been wrong. */
#include <stdio.h>
long kfx_check_overflows = 0;
long kfx_check_peak = 0;
void (*kfx_check_hook)(const kfx_res_t *r, long y, int ovf) = 0;
static void check_sum(const kfx_res_t *r, int32_t x, int shift)
{
    long long t = (long long)r->a * x + (long long)r->b * r->p1 + (long long)r->c * r->p2;
    long long y = t >> shift;
    int ovf = (t > 2147483647LL || t < -2147483648LL);
    if (ovf) kfx_check_overflows++;
    if (y > kfx_check_peak) kfx_check_peak = (long)y;
    if (-y > kfx_check_peak) kfx_check_peak = (long)-y;
    if (kfx_check_hook) kfx_check_hook(r, (long)(y < 0 ? -y : y), ovf);
}
#define CHECK(r, x, s) check_sum(r, x, s)
#else
#define CHECK(r, x, s) ((void)0)
#endif

#if defined(__WATCOMC__) && defined(__386__) == 0 && !defined(KFX_NO_ASM)
int32_t kfx_res386(kfx_res_t *r, int32_t x);      /* klatt/res386.asm */
int32_t kfx_ares386(kfx_res_t *r, int32_t x);
#pragma aux kfx_res386 "*_" parm [bx] [dx ax] value [dx ax] modify [cx];
#pragma aux kfx_ares386 "*_" parm [bx] [dx ax] value [dx ax] modify [cx];
#define resonator(r, x) kfx_res386((r), (x))
#define antiresonator(r, x) kfx_ares386((r), (x))
#else
static int32_t resonator(kfx_res_t *r, int32_t x)
{
    int32_t y;
    CHECK(r, x, 12);
    y = SUM3(MUL32(r->a, x), MUL32(r->b, r->p1), MUL32(r->c, r->p2), 2048L) >> 12;
    r->p2 = r->p1;
    r->p1 = y;
    return y;
}

/* Same difference equation with the delay line holding inputs; Q10. */
static int32_t antiresonator(kfx_res_t *r, int32_t x)
{
    int32_t y;
    CHECK(r, x, 10);
    y = SUM3(MUL32(r->a, x), MUL32(r->b, r->p1), MUL32(r->c, r->p2), 512L) >> 10;
    r->p2 = r->p1;
    r->p1 = x;
    return y;
}
#endif

/* Interpolated table read; u is a Q16 fraction of the sample rate, <= 32768. */
static int32_t tab_lookup(const int16_t KFX_TAB_FAR *tab, uint32_t u)
{
    uint32_t idx = u >> 6;
    int32_t frac = (int32_t)(u & 63);
    int32_t v0 = tab[idx];
    int32_t v1 = tab[idx + 1];
    return v0 + (((v1 - v0) * frac + 32) >> 6);
}

/* Hz -> Q16 fraction of the sample rate (0..65535). */
static uint32_t hz_frac(const kfx_t *k, int32_t hz)
{
    if (hz <= 0)
        return 0;
    return ((uint32_t)hz * k->inv_sr) >> 16;
}

/* exp(-pi * bw / sr) in Q15 */
static int32_t exp_bw(const kfx_t *k, int32_t bw)
{
    uint32_t u = hz_frac(k, bw);
    if (u > 32768u)
        u = 32768u;
    return tab_lookup(kfx_exptab, u);
}

/* cos(2 * pi * f / sr) in Q15 */
static int32_t cos_f(const kfx_t *k, int32_t f)
{
    uint32_t u = hz_frac(k, f);
    u &= 0xFFFFu;
    if (u > 32768u)
        u = 65536u - u;
    return tab_lookup(kfx_costab, u);
}

/* parwave setabc(): resonator coefficients from centre frequency and
 * bandwidth.  a,b,c in Q12. */
static void setabc(const kfx_t *k, int32_t f, int32_t bw, kfx_res_t *r)
{
    int32_t rr = exp_bw(k, bw);          /* Q15 */
    int32_t cc = cos_f(k, f);            /* Q15 */
    r->c = -((rr * rr + 131072L) >> 18); /* -r^2      Q12 */
    r->b = (rr * cc + 65536L) >> 17;     /* 2 r cos   Q12 */
    r->a = Q12_ONE - r->b - r->c;
}

/* parwave setzeroabc(): antiresonator coefficients, Q10. */
static void setzeroabc(const kfx_t *k, int32_t f, int32_t bw, kfx_res_t *r)
{
    kfx_res_t t;
    setabc(k, f, bw, &t);
    if (f != 0 && t.a != 0) {
        r->a = (1L << 22) / t.a;
        r->b = -((t.b * 1024L) / t.a);
        r->c = -((t.c * 1024L) / t.a);
    } else {
        /* parwave leaves the resonator coefficients in place here; keep
         * the same numeric values re-scaled from Q12 to Q10 */
        r->a = t.a >> 2;
        r->b = t.b >> 2;
        r->c = t.c >> 2;
    }
}

/* DBtoLIN(dB) * factor, Q12 */
static int32_t db_gain(int32_t dB, int32_t fq12)
{
    if (dB < 0 || dB > 87)
        return 0;
    return ((int32_t)kfx_amptab[dB] * fq12 + 2048L) >> 12;
}

/* ------------------------------------------------------------------ */

int32_t kfx_noise_next(kfx_t *k)
{
    int32_t n;
    k->seed = k->seed * 1664525UL + 1013904223UL;
    /* 14 random bits mapped onto -8191..8191 (equal to
     * ((r14 * 16383) >> 14) - 8191, without the multiply) */
    n = (int32_t)((k->seed >> 17) & 0x3FFFu) - 8192L;
    if (n == -8192L)
        n = -8191L;
    return n;
}

static void res_clear(kfx_res_t *r)
{
    r->a = r->b = r->c = 0;
    r->p1 = r->p2 = 0;
}

void kfx_init(kfx_t *k, uint16_t sr, uint8_t nfcascade, uint8_t flags)
{
    uint8_t *p = (uint8_t *)k;
    uint16_t i;
    for (i = 0; i < sizeof(*k); i++)
        p[i] = 0;
    k->sr = sr;
    k->inv_sr = 0xFFFFFFFFUL / (uint32_t)sr;
    if (nfcascade < 1) nfcascade = 1;
    if (nfcascade > 6) nfcascade = 6;
    k->nfcascade = nfcascade;
    k->flags = flags;
    k->seed = 5;
    k->noise_k = (flags & KFX_LITE_NOISE_WHITE) ? 0 : 12;
    k->src_diff_sh = 2;
    k->out_k = 922;       /* 0.9 */
    k->out_gain = 4096;   /* 4.0 */
    k->onemd = 16384L;
    k->T0 = 0;      /* as parwave_init: forces a pitch-sync reset on the first sample */
    k->nmod = 0;
    res_clear(&k->rnz);
    if (flags & KFX_LITE_SRC1X) {
        /* 1x equivalent of the 4x-rate rlp: same pole in real frequency */
        setabc(k, (int32_t)((3800L * sr) / 10000L), (int32_t)((2520L * sr) / 10000L), &k->rlp);
    } else {
        /* glottal downsampling low-pass: f = 0.095 sr, bw = 0.063 sr,
         * coefficients computed for sr but run at 4 sr (as parwave) */
        setabc(k, (int32_t)((950L * sr) / 10000L), (int32_t)((630L * sr) / 10000L), &k->rlp);
    }
}

#define RES_IDLE(r) (((r).a | (r).p1 | (r).p2) == 0)

void kfx_set_frame(kfx_t *k, const kfx_frame_t *f)
{
    kfx_frame_t *fr = &k->fr;
    *fr = *f;
    k->ns = 0;

    fr->AVdb = (int16_t)(fr->AVdb - 7);
    if (fr->AVdb < 0)
        fr->AVdb = 0;

    k->amp_aspir = db_gain(fr->ASP, F_0_050);
    k->amp_frica = db_gain(fr->AF, F_0_250);
    k->par_amp_voice = db_gain(fr->AVpdb, F_1_000);
    k->amp_bypas = db_gain(fr->AB, F_0_050);
    fr->Gain0 = (int16_t)(fr->Gain0 - 3);
    if (fr->Gain0 <= 0)
        fr->Gain0 = 57;
    k->amp_gain0 = db_gain(fr->Gain0, F_1_000);

    /* cascade */
    if (k->nfcascade >= 6) setabc(k, fr->F6hz, fr->B6hz, &k->r6c);
    if (k->nfcascade >= 5) setabc(k, fr->F5hz, fr->B5hz, &k->r5c);
    if (k->nfcascade >= 4) setabc(k, fr->F4hz, fr->B4hz, &k->r4c);
    if (k->nfcascade >= 3) setabc(k, fr->F3hz, fr->B3hz, &k->r3c);
    if (k->nfcascade >= 2) setabc(k, fr->F2hz, fr->B2hz, &k->r2c);
    setabc(k, fr->F1hz, fr->B1hz, &k->r1c);

    setabc(k, fr->FNPhz, fr->BNPhz, &k->rnpc);
    setzeroabc(k, fr->FNZhz, fr->BNZhz, &k->rnz);

    /* parallel: fold the amplitude into 'a' */
    setabc(k, fr->F1hz, fr->B1phz, &k->r1p);
    k->r1p.a = MUL32(k->r1p.a, db_gain(fr->A1, F_0_400)) >> 12;
    setabc(k, fr->FNPhz, fr->BNPhz, &k->rnpp);
    k->rnpp.a = MUL32(k->rnpp.a, db_gain(fr->ANP, F_0_600)) >> 12;
    setabc(k, fr->F2hz, fr->B2phz, &k->r2p);
    k->r2p.a = MUL32(k->r2p.a, db_gain(fr->A2, F_0_150)) >> 12;
    setabc(k, fr->F3hz, fr->B3phz, &k->r3p);
    k->r3p.a = MUL32(k->r3p.a, db_gain(fr->A3, F_0_060)) >> 12;
    setabc(k, fr->F4hz, fr->B4phz, &k->r4p);
    k->r4p.a = MUL32(k->r4p.a, db_gain(fr->A4, F_0_040)) >> 12;
    setabc(k, fr->F5hz, fr->B5phz, &k->r5p);
    k->r5p.a = MUL32(k->r5p.a, db_gain(fr->A5, F_0_022)) >> 12;
    setabc(k, fr->F6hz, fr->B6phz, &k->r6p);
    k->r6p.a = MUL32(k->r6p.a, db_gain(fr->A6, F_0_030)) >> 12;

    /* output low-pass, or the pre-emphasis FIR in the antiresonator slot */
    if (k->flags & KFX_LITE_OUT_FIR) {
        k->rout.a = k->out_gain;
        k->rout.b = -((MUL32(k->out_gain, k->out_k) + 512L) >> 10);
        k->rout.c = 0;
    } else {
        setabc(k, 0, (int32_t)(k->sr / 2), &k->rout);
    }

    /* 1x source: the anti-alias slot can serve as the cascade F4 section */
    if ((k->flags & (KFX_LITE_SRC1X | KFX_LITE_RLP_F4)) == (KFX_LITE_SRC1X | KFX_LITE_RLP_F4))
        setabc(k, fr->F4hz, fr->B4hz, &k->rlp);

    /* which parallel sections can be skipped this frame (bit-exact: a
     * section with zero gain and an empty delay line outputs zero) */
    k->rnpp_on = !(RES_IDLE(k->r1p) && RES_IDLE(k->rnpp));
    k->r56_on = !(RES_IDLE(k->r5p) && RES_IDLE(k->r6p));
    k->par_gain_on = (k->r2p.a | k->r3p.a | k->r4p.a | k->r5p.a | k->r6p.a |
                      k->r1p.a | k->rnpp.a | k->amp_bypas | k->par_amp_voice) != 0;
    if (k->par_gain_on)
        k->par_active = 1;
}

/* parwave pitch_synch_par_reset() */
static void pitch_synch_par_reset(kfx_t *k)
{
    kfx_frame_t *fr = &k->fr;
    if (fr->F0hz10 > 0) {
        int32_t temp, tmp;
        k->T0 = (40L * k->sr) / fr->F0hz10;
        k->amp_voice = db_gain(fr->AVdb, F_1_000);
        k->nmod = k->T0;
        if (fr->AVdb > 0)
            k->nmod >>= 1;
        k->amp_breth = db_gain(fr->Aturb, F_0_100);
        k->nopen = 4L * fr->Kopen;
        if (k->nopen >= k->T0 - 1)
            k->nopen = k->T0 - 2;
        if (k->nopen < 40)
            k->nopen = 40;
        /* pulse_shape_b = B0[nopen-40], pulse_shape_a = b * nopen * 0.333,
         * both scaled by the 0.028 output factor, Q12 */
        k->pulse_b = ((int32_t)kfx_b0tab[k->nopen - 40] * 29360L + 128L) >> 8;
        tmp = k->pulse_b * k->nopen;                  /* < 2^27 */
        k->pulse_a = (((tmp >> 5) * 333L) / 1000L) << 5;
        /* skew */
        temp = k->T0 - k->nopen;
        if (fr->Kskew > temp)
            fr->Kskew = (int16_t)temp;
        if (k->skew >= 0)
            k->skew = fr->Kskew;
        else
            k->skew = -fr->Kskew;
        k->T0 += k->skew;
        k->skew = -k->skew;
    } else {
        k->T0 = 4;
        k->amp_voice = 0;
        k->nmod = 4;
        k->amp_breth = 0;
        k->pulse_a = 0;
        k->pulse_b = 0;
    }
    if (k->T0 != 4 || k->ns == 0) {
        k->decay = (int32_t)fr->TLTdb * 541L;      /* 0.033 * 2^14 */
        if (k->decay > 0)
            k->onemd = 16384L - k->decay;
        else
            k->onemd = 16384L;
    }
}

/* One output sample, parwave's 16-bit scale. */
static KFX_INLINE int32_t kfx_sample(kfx_t *k)
{
    int32_t noise, frics, voice = 0, glotout, par_glotout, aspiration, sourc, out;
    int32_t x;
    int8_t n4;

    /* low-passed noise (nlast is Q4; noise_k = 12 is parwave's 0.75) */
    k->nrand = kfx_noise_next(k);
    k->nlast = k->nrand * 16 + ((k->nlast * k->noise_k) >> 4);
    noise = k->nlast >> 4;
    if (k->nper > k->nmod)
        noise >>= 1;
    frics = MUL32(k->amp_frica, noise) >> (12 + KFX_HEADROOM);

    /* glottal source: 4x oversampled (parwave) or stepped by 4 (SRC1X) */
    if (!(k->flags & KFX_LITE_SRC1X)) {
        for (n4 = 0; n4 < 4; n4++) {
            int32_t v;
            if (k->nper < k->nopen) {
                k->pulse_a -= k->pulse_b;
                k->vwave += k->pulse_a;
                v = k->vwave >> 12;
            } else {
                k->vwave = 0;
                v = 0;
            }
            if (k->nper >= k->T0) {
                k->nper = 0;
                pitch_synch_par_reset(k);
            }
            voice = resonator(&k->rlp, v);
            k->nper++;
        }
    } else {
        int32_t v;
        if (k->nper + 4 <= k->T0) {
            if (k->nper + 4 <= k->nopen) {
                /* four open-phase steps at once: a -= 4b, v += 4a' + 6b */
                k->pulse_a -= k->pulse_b * 4;
                k->vwave += k->pulse_a * 4 + k->pulse_b * 6;
                v = k->vwave >> 12;
                k->nper += 4;
            } else if (k->nper >= k->nopen) {
                k->vwave = 0;
                v = 0;
                k->nper += 4;
            } else {
                goto slow;
            }
        } else {
slow:       /* a phase boundary or period reset falls inside this sample */
            for (n4 = 0; n4 < 4; n4++) {
                if (k->nper < k->nopen) {
                    k->pulse_a -= k->pulse_b;
                    k->vwave += k->pulse_a;
                    v = k->vwave >> 12;
                } else {
                    k->vwave = 0;
                    v = 0;
                }
                if (k->nper >= k->T0) {
                    k->nper = 0;
                    pitch_synch_par_reset(k);
                }
                k->nper++;
            }
        }
        if (k->flags & KFX_LITE_SRC_DIFF) {
            /* first difference of the pulse: v = (vwave - vwave[-1]) * 2^sh */
            int32_t w = k->vwave;
            v = ((w - k->vwave_prev) * (1L << k->src_diff_sh)) >> 12;
            k->vwave_prev = w;
        }
        voice = resonator(&k->rlp, v);
    }

    /* spectral tilt */
    if (k->decay > 0)
        voice = (int32_t)((uint32_t)MUL32(voice, k->onemd) + (uint32_t)MUL32(k->vlast, k->decay)) >> 14;
    k->vlast = voice;

    /* breathiness during the open phase */
    if (k->nper < k->nopen && k->amp_breth)
        voice += MUL32(k->amp_breth, k->nrand) >> 12;

    aspiration = MUL32(k->amp_aspir, noise) >> (12 + KFX_HEADROOM);
    glotout = (MUL32(k->amp_voice, voice) >> (12 + KFX_HEADROOM)) + aspiration;
    par_glotout = aspiration;
    if (k->par_amp_voice)
        par_glotout += MUL32(k->par_amp_voice, voice) >> (12 + KFX_HEADROOM);

    /* cascade branch */
    x = antiresonator(&k->rnz, glotout);
    x = resonator(&k->rnpc, x);
    switch (k->nfcascade) {
    case 6: x = resonator(&k->r6c, x);
    case 5: x = resonator(&k->r5c, x);
    case 4: x = resonator(&k->r4c, x);
    case 3: x = resonator(&k->r3c, x);
    case 2: x = resonator(&k->r2c, x);
    default: break;
    }
    out = resonator(&k->r1c, x);

    /* parallel branch: skipped entirely while every section is idle */
    if (k->par_active) {
        if (k->rnpp_on) {
            out += resonator(&k->r1p, par_glotout);
            out += resonator(&k->rnpp, par_glotout);
        }
        sourc = frics + par_glotout - k->glotlast;
        if (k->r56_on) {
            out = resonator(&k->r6p, sourc) - out;
            out = resonator(&k->r5p, sourc) - out;
        }
        out = resonator(&k->r4p, sourc) - out;
        out = resonator(&k->r3p, sourc) - out;
        out = resonator(&k->r2p, sourc) - out;
        out = (MUL32(k->amp_bypas, sourc) >> 12) - out;
        if (!k->par_gain_on &&
            (k->r2p.p1 | k->r2p.p2 | k->r3p.p1 | k->r3p.p2 | k->r4p.p1 | k->r4p.p2 |
             k->r5p.p1 | k->r5p.p2 | k->r6p.p1 | k->r6p.p2 |
             k->r1p.p1 | k->r1p.p2 | k->rnpp.p1 | k->rnpp.p2) == 0)
            k->par_active = 0;    /* rung down: nothing left to compute */
    }
    k->glotlast = par_glotout;

    if (k->flags & KFX_LITE_OUT_FIR)
        out = antiresonator(&k->rout, out);
    else
        out = resonator(&k->rout, out);
    out = MUL32(out, k->amp_gain0) >> (12 - KFX_HEADROOM);
    if (out > OUT_MAX) out = OUT_MAX;
    if (out < OUT_MIN) out = OUT_MIN;
    k->ns++;
    return out;
}

void kfx_render16(kfx_t *k, int16_t *out, int16_t n)
{
    while (n-- > 0)
        *out++ = (int16_t)kfx_sample(k);
}

void kfx_render8(kfx_t *k, uint8_t *out, int16_t n)
{
    while (n-- > 0)
        *out++ = (uint8_t)((kfx_sample(k) >> 8) + 128);
}
