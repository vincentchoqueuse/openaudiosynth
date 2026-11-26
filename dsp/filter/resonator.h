/* resonator.h — complex one-pole resonator: z <- (c + i s) z + g x, output = Im(z). A rotation by the mode
 * frequency times a decay radius per sample, so frequency and T60 are exact and it never goes unstable
 * (|c + i s| < 1). One mode of a struck or bowed object; banks of them make bells, bars, strings, drums.
 * Reference: J.-M. Adrien, "The missing link: modal synthesis" (1991); M. Mathews & J. Smith, "Methods for
 * synthesizing very high Q parametrically well behaved two pole filters" (SMAC 2003). */
#ifndef RESONATOR_H
#define RESONATOR_H

#include "../core/fastmath.h"

typedef struct {
    float zr, zi; /* complex state */
    float c, s;   /* radius x (cos, sin) of the rotation per sample; may be set directly by an engine */
} reso_t;

static inline void reso_clear(reso_t *r) { r->zr = r->zi = r->c = r->s = 0.0f; }

/* Frequency in Hz (clamped to 0.45 x SR) and decay time to -60 dB in seconds. Keeps the state. */
static inline void reso_tune(reso_t *r, float freq_hz, float t60_s, float sample_rate)
{
    const float radius = fm_exp2(-9.96578428f / (t60_s * sample_rate)); /* log2(1000) */
    const float ph = dsp_minf(freq_hz, 0.45f * sample_rate) / sample_rate;
    r->c = radius * fm_cos(ph);
    r->s = radius * fm_sin(ph);
}

/* Instant excitation: the mode starts ringing with amplitude amp (a sine starting at 0). */
static inline void reso_strike(reso_t *r, float amp)
{
    r->zr = amp;
    r->zi = 0.0f;
}

/* Squared amplitude of the ringing (|z|^2). */
static inline float reso_energy(const reso_t *r) { return r->zr * r->zr + r->zi * r->zi; }

/* Free ringing: adds n samples to out. */
static inline void reso_ring(reso_t *r, float *restrict out, int n)
{
    const float c = r->c, s = r->s;
    float zr = r->zr, zi = r->zi;
    for (int i = 0; i < n; i++) {
        const float nr = c * zr - s * zi;
        zi = s * zr + c * zi;
        zr = nr;
        out[i] += zi;
    }
    r->zr = zr, r->zi = zi;
}

/* Driven by g x[i]: adds n samples to out. */
static inline void reso_drive(reso_t *r, const float *restrict x, float g, float *restrict out, int n)
{
    const float c = r->c, s = r->s;
    float zr = r->zr, zi = r->zi;
    for (int i = 0; i < n; i++) {
        const float nr = c * zr - s * zi + g * x[i];
        zi = s * zr + c * zi;
        zr = nr;
        out[i] += zi;
    }
    r->zr = zr, r->zi = zi;
}

/* Driven by two inputs (e.g. strike and bow): g1 x1[i] + g2 x2[i]. */
static inline void reso_drive2(reso_t *r, const float *restrict x1, float g1, const float *restrict x2, float g2,
                               float *restrict out, int n)
{
    const float c = r->c, s = r->s;
    float zr = r->zr, zi = r->zi;
    for (int i = 0; i < n; i++) {
        const float nr = c * zr - s * zi + g1 * x1[i] + g2 * x2[i];
        zi = s * zr + c * zi;
        zr = nr;
        out[i] += zi;
    }
    r->zr = zr, r->zi = zi;
}

/* Two modes in one pass: the same as one mode then the other (each mode's arithmetic is unchanged, and they are
 * added to out in that order), but their two chains overlap in the FPU. g1/g2: each mode's gain on x1 and x2 (a mode
 * that is not driven by an input passes NULL for it). */
static inline void reso_pair(reso_t *a, reso_t *b, const float *restrict x1, float g1a, float g1b,
                             const float *restrict x2, float g2a, float g2b, float *restrict out, int n)
{
    const float ca = a->c, sa = a->s, cb = b->c, sb = b->s;
    float ar = a->zr, ai = a->zi, br = b->zr, bi = b->zi;
    if (x1 && x2)
        for (int i = 0; i < n; i++) {
            const float na = ca * ar - sa * ai + g1a * x1[i] + g2a * x2[i];
            const float nb = cb * br - sb * bi + g1b * x1[i] + g2b * x2[i];
            ai = sa * ar + ca * ai;
            bi = sb * br + cb * bi;
            ar = na, br = nb;
            out[i] += ai;
            out[i] += bi;
        }
    else if (x1)
        for (int i = 0; i < n; i++) {
            const float na = ca * ar - sa * ai + g1a * x1[i];
            const float nb = cb * br - sb * bi + g1b * x1[i];
            ai = sa * ar + ca * ai;
            bi = sb * br + cb * bi;
            ar = na, br = nb;
            out[i] += ai;
            out[i] += bi;
        }
    else
        for (int i = 0; i < n; i++) {
            const float na = ca * ar - sa * ai;
            const float nb = cb * br - sb * bi;
            ai = sa * ar + ca * ai;
            bi = sb * br + cb * bi;
            ar = na, br = nb;
            out[i] += ai;
            out[i] += bi;
        }
    a->zr = ar, a->zi = ai, b->zr = br, b->zi = bi;
}

#endif /* RESONATOR_H */
