/* ortho.h — a sine that turns into noise by quadrature modulation: y = w_d sin(ph) + w_r g (a sin(ph) + b cos(ph)),
 * a and b two independent random signals of a chosen half-width W (Hz). Around the frequency f of the phase they
 * make a band of noise from f - W to f + W, centred exactly on f whatever W; the deterministic part keeps the sine.
 * One amount (0..1) does the whole way: the random weight w_r = sin(amount pi/2) against w_d = cos(amount pi/2)
 * (constant power), and the half-width W from 2 Hz to 12 kHz on an exponential scale. Three kinds of random signal:
 * smooth (white noise in two one-pole low-passes in series: a breath), stepped (sample-and-hold every 1 / 2W s: a
 * digital grain, clicks at a steady rate) and sparse (impulses of random sign at W per second: a dry crackle). Each is
 * scaled to unit variance, so the level stays the sine's at every amount (sparse: up to a
 * gain of 4, a thin crackle is quieter). Header-only: the voice keeps the state and
 * calls ortho_next per sample. Reference: narrowband noise as quadrature modulation of a carrier (Rice, 1944). */
#ifndef ORTHO_H
#define ORTHO_H

#include "../core/fastmath.h"

enum { ORTHO_SMOOTH = 0, ORTHO_STEPPED, ORTHO_SPARSE, ORTHO_MODE_COUNT };

typedef struct {
    float a1, a2, b1, b2; /* the two random signals, through their two low-pass stages */
    float hold;           /* stepped: samples left before the next value */
    uint32_t rng;
} ortho_t;

/* what a block of a voice needs, computed once per block from the amount, the mode and the sample rate */
typedef struct {
    float wd, wr; /* weights of the sine and of the random part (wr already scaled to unit variance) */
    float c;      /* smooth: the low-pass coefficient; stepped: samples per step; sparse: impulses per sample */
    int mode, on;
} ortho_blk_t;

/* A new note: the same draws every time (seed), the random part from rest. */
static inline void ortho_reset(ortho_t *o, uint32_t seed)
{
    o->a1 = o->a2 = o->b1 = o->b2 = 0.0f;
    o->hold = 0.0f;
    o->rng = dsp_seed(seed);
}

/* The half-width (Hz) of the band of noise for an amount 0..1. */
static inline float ortho_width(float amount) { return fm_exp2(1.0f + 12.55f * dsp_clampf(amount, 0.0f, 1.0f)); }

/* A block's settings from the random part's weight (0 = the sine, 1 = noise alone, constant power between) and the
 * half-width W in Hz; ortho_block below derives both from the amount. */
static inline ortho_blk_t ortho_block_at(float mix, float width_hz, int mode, float sample_rate)
{
    ortho_blk_t k;
    mix = dsp_clampf(mix, 0.0f, 1.0f);
    k.mode = dsp_clampi(mode, 0, ORTHO_MODE_COUNT - 1);
    k.on = mix > 0.0f;
    k.wd = fm_cos(0.25f * mix);
    float wr = fm_sin(0.25f * mix);
    const float w = dsp_clampf(width_hz, 0.1f, 0.45f * sample_rate);
    if (k.mode == ORTHO_SMOOTH) {
        /* one-pole stage y += c (x - y) at 1.554 W (two in series: -3 dB at W); white in [-1, 1) has variance 1/3 and
         * the pair passes c^4 (1 + r^2) / (1 - r^2)^3 of it, r = 1 - c */
        const float c = 1.0f - fm_exp2(-FM_LOG2E * FM_TWO_PI * 1.554f * w / sample_rate);
        const float r = 1.0f - c, q = 1.0f - r * r;
        const float var = (1.0f / 3.0f) * c * c * c * c * (1.0f + r * r) / (q * q * q);
        k.c = c;
        wr *= fm_exp2(-0.5f * fm_log2(var));
    } else if (k.mode == ORTHO_STEPPED) {
        k.c = dsp_maxf(1.0f, sample_rate / (2.0f * w)); /* samples per held value */
        wr *= 1.7320508f;                                /* uniform [-1, 1): variance 1/3 */
    } else {
        k.c = dsp_minf(1.0f, w / sample_rate); /* impulses per sample */
        /* +-1 with probability c: variance c; the gain stops at 4 so a thin crackle stays a crackle, quieter than
         * the sine, instead of rare spikes of x20 */
        wr *= dsp_minf(4.0f, fm_exp2(-0.5f * fm_log2(k.c)));
    }
    k.wr = wr * 0.70710678f; /* a sin + b cos with unit-variance a, b has variance 1: the sine's is 1/2 */
    return k;
}

static inline ortho_blk_t ortho_block(float amount, int mode, float sample_rate)
{
    return ortho_block_at(amount, ortho_width(amount), mode, sample_rate);
}

/* One sample of the generator at phase ph (cycles, [0, 1)). */
static inline float ortho_next(ortho_t *o, const ortho_blk_t *k, float ph)
{
    const float s = fm_sin_pos(ph);
    if (!k->on)
        return s;
    float a, b;
    if (k->mode == ORTHO_SMOOTH) {
        o->a1 += k->c * (dsp_rand_bipolar(&o->rng) - o->a1);
        o->b1 += k->c * (dsp_rand_bipolar(&o->rng) - o->b1);
        o->a2 += k->c * (o->a1 - o->a2);
        o->b2 += k->c * (o->b1 - o->b2);
        a = o->a2, b = o->b2;
    } else if (k->mode == ORTHO_STEPPED) {
        if (o->hold <= 0.0f) {
            o->a2 = dsp_rand_bipolar(&o->rng);
            o->b2 = dsp_rand_bipolar(&o->rng);
            o->hold += k->c;
        }
        o->hold -= 1.0f;
        a = o->a2, b = o->b2;
    } else {
        /* an impulse with probability c, its sign from where the draw fell below c (two independent draws) */
        const float u = dsp_rand_unipolar(&o->rng), v = dsp_rand_unipolar(&o->rng);
        a = u < k->c ? (u < 0.5f * k->c ? 1.0f : -1.0f) : 0.0f;
        b = v < k->c ? (v < 0.5f * k->c ? 1.0f : -1.0f) : 0.0f;
    }
    const float c = fm_sin_pos(ph + 0.25f >= 1.0f ? ph - 0.75f : ph + 0.25f);
    return k->wd * s + k->wr * (a * s + b * c);
}

#endif /* ORTHO_H */
