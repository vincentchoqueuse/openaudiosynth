/* halfband.h — 2x and 4x oversampling for nonlinear stages: polyphase half-band FIR up (one input sample -> two
 * at the double rate) and down (two -> one), 47 taps, Kaiser beta 8, about -56 dB stopband; 11.5 samples of
 * latency each way. 4x cascades two 2x stages (the same filter works at any rate: its cutoff is a quarter of
 * its own rate); the inner stage adds 5.75 base samples each way. Used by the ladder and the screaming 2-pole filter
 * around their saturating feedback.
 * Reference: polyphase half-band decimation/interpolation (e.g. R. Lyons, "Understanding DSP", ch. 10). */
#ifndef HALFBAND_H
#define HALFBAND_H

#include "../core/dsp.h"

#define HB_N 24 /* history length of each polyphase branch */

typedef struct {
    float up[2 * HB_N]; /* double-length circular buffers: contiguous reads without wrapping */
    float dn_e[2 * HB_N], dn_o[2 * HB_N];
    int up_pos, dn_pos;
} hb2x_t;

/* Side coefficients c_j for offsets +/-(2j+1); centre tap 0.5. */
static const float hb2x_c[12] = {3.160629362e-01f, -9.953458363e-02f, 5.323959922e-02f, -3.190621204e-02f,
                                 1.951168259e-02f, -1.168538411e-02f, 6.670847582e-03f, -3.539467847e-03f,
                                 1.690651032e-03f, -6.900036008e-04f, 2.146042569e-04f, -3.236808785e-05f};

static inline void hb2x_reset(hb2x_t *h)
{
    for (int i = 0; i < 2 * HB_N; i++)
        h->up[i] = h->dn_e[i] = h->dn_o[i] = 0.0f;
    h->up_pos = h->dn_pos = 0;
}

/* One input sample -> two samples at 2x: *a first, then *b. */
static inline void hb2x_up(hb2x_t *h, float in, float *a, float *b)
{
    const int p = h->up_pos;
    h->up[p] = h->up[p + HB_N] = in;
    const float *x = &h->up[p + HB_N]; /* x[-m] = input m samples ago */
    float even = 0.0f;
    for (int j = 0; j < 12; j++)
        even += hb2x_c[j] * (x[-(11 - j)] + x[-(12 + j)]);
    *a = 2.0f * even;
    *b = x[-11];
    h->up_pos = (p + 1) % HB_N;
}

/* Two samples at 2x (ya first) -> one output sample. */
static inline float hb2x_down(hb2x_t *h, float ya, float yb)
{
    const int q = h->dn_pos;
    h->dn_e[q] = h->dn_e[q + HB_N] = ya;
    h->dn_o[q] = h->dn_o[q + HB_N] = yb;
    const float *e = &h->dn_e[q + HB_N], *o = &h->dn_o[q + HB_N];
    float z = 0.0f;
    for (int j = 0; j < 12; j++)
        z += hb2x_c[j] * (e[-(11 - j)] + e[-(12 + j)]);
    z += 0.5f * o[-12];
    h->dn_pos = (q + 1) % HB_N;
    return z;
}

/* 4x: outer stage 1x <-> 2x, inner stage 2x <-> 4x. A 2x user takes only the outer stage. */
typedef struct {
    hb2x_t outer, inner;
} hb4x_t;

static inline void hb4x_reset(hb4x_t *h)
{
    hb2x_reset(&h->outer);
    hb2x_reset(&h->inner);
}

/* One input sample -> four at 4x, in order. */
static inline void hb4x_up(hb4x_t *h, float in, float out[4])
{
    float a, b;
    hb2x_up(&h->outer, in, &a, &b);
    hb2x_up(&h->inner, a, &out[0], &out[1]);
    hb2x_up(&h->inner, b, &out[2], &out[3]);
}

/* Four samples at 4x -> one output sample. */
static inline float hb4x_down(hb4x_t *h, const float y[4])
{
    const float ya = hb2x_down(&h->inner, y[0], y[1]), yb = hb2x_down(&h->inner, y[2], y[3]);
    return hb2x_down(&h->outer, ya, yb);
}

/* The oversampling factor a caller asks for, made valid: 1, 2 or 4. */
static inline int hb_factor(int factor) { return factor >= 4 ? 4 : factor >= 2 ? 2 : 1; }

#endif /* HALFBAND_H */
