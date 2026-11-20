/* dsp.h — definitions shared by the whole engine: block size, memory placement
 * (DTCM/ITCM on STM32), small inline helpers (clamping, xorshift random, ramps, NaN guard).
 * No dependency beyond <stdint.h> and <stddef.h>. */
#ifndef DSP_H
#define DSP_H

#include <stddef.h>
#include <stdint.h>

/* Block size: modulations are computed once per block (control rate = SR/32). */
#define SYNTH_BLOCK 32

/* Memory placement. The target defines them (section attribute from its linker script);
 * on the host they stay empty. */
#ifndef DSP_FAST_DATA
#define DSP_FAST_DATA
#endif
#ifndef DSP_FAST_CODE
#define DSP_FAST_CODE
#endif

static inline float dsp_minf(float a, float b) { return a < b ? a : b; }
static inline float dsp_maxf(float a, float b) { return a > b ? a : b; }
static inline float dsp_absf(float x) { return x < 0.0f ? -x : x; }
/* a NaN comes out as lo (never propagated into indices or filter coefficients) */
static inline float dsp_clampf(float x, float lo, float hi) { return !(x >= lo) ? lo : (x > hi ? hi : x); }
static inline int dsp_clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* Access to the bits of a float (type punning through a union is defined in C11). */
typedef union {
    float f;
    uint32_t u;
} dsp_bits_t;

/* True if x is neither NaN nor infinite (exponent not 255). */
static inline int dsp_isfinite(float x)
{
    dsp_bits_t b;
    b.f = x;
    return (b.u & 0x7F800000u) != 0x7F800000u;
}

/* ---- Deterministic random: xorshift32 (Marsaglia). The state must never be 0. ---- */
static inline uint32_t dsp_rand_u32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
/* Value in [-1, 1). */
static inline float dsp_rand_bipolar(uint32_t *s)
{
    return (float)(int32_t)dsp_rand_u32(s) * 4.6566128730773926e-10f; /* 2^-31 */
}
/* Value in [0, 1). */
static inline float dsp_rand_unipolar(uint32_t *s)
{
    return (float)(dsp_rand_u32(s) >> 8) * 5.9604644775390625e-8f; /* 2^-24 */
}
/* Mixes an arbitrary seed into a valid (non-zero) xorshift state. */
static inline uint32_t dsp_seed(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x ? x : 0x9E3779B9u;
}

/* ---- Per-sample linear ramps (interpolation of control values) ---- */
/* x[i] *= g, g going from g0 (excluded) to g1 (reached on the last sample). */
static inline void dsp_ramp_mul(float *restrict x, float g0, float g1, int n)
{
    const float dg = (g1 - g0) / (float)n;
    float g = g0;
    for (int i = 0; i < n; i++) {
        g += dg;
        x[i] *= g;
    }
}
/* acc[i] += x[i] * g, same ramp. */
static inline void dsp_ramp_mac(float *restrict acc, const float *restrict x, float g0, float g1, int n)
{
    const float dg = (g1 - g0) / (float)n;
    float g = g0;
    for (int i = 0; i < n; i++) {
        g += dg;
        acc[i] += x[i] * g;
    }
}

/* ---- NaN/Inf guard (debug build only: SYNTH_NAN_GUARD) ----
 * Host: immediate stop (__builtin_trap) to catch the bug in the debugger.
 * Board: zero the block and increment the counter. */
static inline void dsp_guard_block(float *x, int n, uint32_t *count)
{
#if defined(SYNTH_NAN_GUARD)
    int bad = 0;
    for (int i = 0; i < n; i++)
        bad |= !dsp_isfinite(x[i]);
    if (bad) {
#if defined(DSP_HOST)
        __builtin_trap();
#endif
        for (int i = 0; i < n; i++)
            x[i] = 0.0f;
        (*count)++;
    }
#else
    (void)x;
    (void)n;
    (void)count;
#endif
}

#endif /* DSP_H */
