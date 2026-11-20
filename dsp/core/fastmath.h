/* fastmath.h — fast math functions for the audio path, with no libm calls.
 * Sine: 2048-point table + linear interpolation (error < 1.2e-6).
 * exp2: rounding + 6th-order Taylor on [-0.5, 0.5] (relative error < 3e-7); log2: atanh series;
 * tanh: (3,2) Pade approximant, clamped to +/-1 beyond |x| = 3. */
#ifndef FASTMATH_H
#define FASTMATH_H

#include "dsp.h"

#define FM_SIN_BITS 11
#define FM_SIN_SIZE (1 << FM_SIN_BITS)
extern const float fm_sin_table[FM_SIN_SIZE + 1];

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

/* Fractional part in [0, 1). Assumes |x| < 2^31. */
static inline float fm_frac(float x)
{
    float f = x - (float)(int32_t)x;
    return f < 0.0f ? f + 1.0f : f;
}

/* sin(2*pi*phase), phase expressed in cycles (any value). */
static inline float fm_sin(float phase)
{
    const float pos = fm_frac(phase) * (float)FM_SIN_SIZE;
    int32_t i = (int32_t)pos;
    const float fr = pos - (float)i;
    i &= FM_SIN_SIZE - 1; /* fm_frac may return exactly 1.0 due to rounding */
    const float a = fm_sin_table[i];
    return a + (fm_sin_table[i + 1] - a) * fr;
}
/* fm_sin for a phase known to be >= 0 (|phase| < 2^31): the same value, without fm_frac's negative branch. */
static inline float fm_sin_pos(float phase)
{
    const float pos = (phase - (float)(int32_t)phase) * (float)FM_SIN_SIZE;
    int32_t i = (int32_t)pos;
    const float fr = pos - (float)i;
    i &= FM_SIN_SIZE - 1;
    const float a = fm_sin_table[i];
    return a + (fm_sin_table[i + 1] - a) * fr;
}
static inline float fm_cos(float phase) { return fm_sin(phase + 0.25f); }

/* 2^x, x clamped to [-126, 126]. */
static inline float fm_exp2(float x)
{
    x = dsp_clampf(x, -126.0f, 126.0f);
    const int32_t i = (int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f); /* round to nearest */
    const float t = (x - (float)i) * FM_LN2;                       /* |t| <= 0.347 */
    const float p = 1.0f + t * (1.0f + t * (0.5f + t * (1.6666667e-1f +
                    t * (4.1666668e-2f + t * (8.3333338e-3f + t * 1.3888889e-3f)))));
    dsp_bits_t b;
    b.u = (uint32_t)(i + 127) << 23;
    return p * b.f;
}

/* log2(x) for x > 0 (returns -126 otherwise). */
static inline float fm_log2(float x)
{
    if (!(x > 1.0e-37f))
        return -126.0f;
    dsp_bits_t b;
    b.f = x;
    int32_t e = (int32_t)((b.u >> 23) & 0xFFu) - 127;
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u; /* mantissa in [1, 2) */
    float m = b.f;
    if (m > 1.41421356f) {
        m *= 0.5f;
        e += 1;
    }
    /* log2(m) = (2/ln2)*atanh(s), s = (m-1)/(m+1), |s| <= 0.172 */
    const float s = (m - 1.0f) / (m + 1.0f);
    const float s2 = s * s;
    const float p = s * (2.88539008f + s2 * (0.961796694f + s2 * (0.577078016f + s2 * 0.412198583f)));
    return (float)e + p;
}

static inline float fm_exp(float x) { return fm_exp2(x * FM_LOG2E); }

/* MIDI note number (fractional) -> frequency in Hz (A4 = 69 = 440 Hz). */
static inline float fm_mtof(float note) { return 440.0f * fm_exp2((note - 69.0f) * (1.0f / 12.0f)); }

/* dB -> linear gain, and the inverse. */
static inline float fm_db2lin(float db) { return fm_exp2(db * 0.166096405f); }
static inline float fm_lin2db(float g) { return 6.02059991f * fm_log2(g); }

/* tan(pi*x) for x in [0, 0.49] (prewarping for TPT filters). */
static inline float fm_tan_pi(float x)
{
    return fm_sin(0.5f * x) / fm_sin(0.5f * x + 0.25f);
}

/* tanh(x): (3,2) Pade, continuous and with zero derivative at |x| = 3. */
static inline float fm_tanh(float x)
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    const float x2 = x * x;
    /* the curve is flat near |x| = 3: clamp the 1-ulp rounding overshoot */
    return dsp_clampf(x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
}

#endif /* FASTMATH_H */
