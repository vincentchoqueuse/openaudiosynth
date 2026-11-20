/* splay.c — sample playback with band-limited interpolation (splay.h). Each output sample is the sum of the
 * source samples around the read position weighted by the windowed sinc stretched to the cutoff fc:
 * y = fc * sum_j x[j] k(fc |pos - j|), k tabulated once (splay_table.c) and read with linear interpolation.
 * The two sides of the position are summed apart so no absolute value or bound test sits in the inner loop.
 * Reference: J. O. Smith, "Digital Audio Resampling Home Page" (bandlimited interpolation). */
#include "splay.h"

#include "../core/dsp.h"

void splay_init(splay_t *p)
{
    p->data = 0;
    p->frames = p->start = p->end = p->i = 0;
    p->frac = 0.0f;
    p->dir = 1;
    p->loop = 0;
    p->active = 0;
}

void splay_start(splay_t *p, const int16_t *data, int32_t frames, float start, float end, int reverse, int loop)
{
    splay_init(p);
    if (!data || frames < 2)
        return;
    const float f = (float)frames;
    int32_t s = (int32_t)(dsp_clampf(start, 0.0f, 1.0f) * f), e = (int32_t)(dsp_clampf(end, 0.0f, 1.0f) * f);
    if (s > frames - 1)
        s = frames - 1;
    if (e < s + 1)
        e = s + 1;
    if (e > frames)
        e = frames;
    p->data = data;
    p->frames = frames;
    p->start = s;
    p->end = e;
    p->dir = reverse ? -1 : 1;
    p->i = reverse ? e - 1 : s;
    p->loop = (int8_t)(loop != 0);
    p->active = 1;
}

/* the kernel at m crossings-scaled steps from d0: sum of x[j] k(d * scale) for j walking away from the position */
static inline float side(const int16_t *x, int32_t j, int step, int count, float d0, float scale)
{
    float acc = 0.0f, idx = d0 * scale;
    for (int m = 0; m < count; m++, j += step, idx += scale) {
        const int k = (int)idx;
        if (k >= SPLAY_ZC * SPLAY_RES)
            break;
        const float t = idx - (float)k;
        acc += (float)x[j] * (splay_kernel[k] + (splay_kernel[k + 1] - splay_kernel[k]) * t);
    }
    return acc;
}

DSP_FAST_CODE int splay_process(splay_t *p, float *out, int n, float rate0, float rate1)
{
    int s = 0;
    if (p->active) {
        rate0 = dsp_clampf(rate0, 0.0f, SPLAY_RATE_MAX);
        rate1 = dsp_clampf(rate1, 0.0f, SPLAY_RATE_MAX);
        const float rmax = rate0 > rate1 ? rate0 : rate1;
        const float fc = rmax > 1.0f ? SPLAY_BW / rmax : SPLAY_BW;
        const int taps = (int)((float)SPLAY_ZC / fc) + 1; /* per side */
        const float scale = fc * (float)SPLAY_RES, gain = fc * (1.0f / 32768.0f);
        const float dr = (rate1 - rate0) / (float)n, inv_fade = 1.0f / (float)SPLAY_FADE;
        const int32_t last = p->frames - 1;
        float r = rate0;
        for (; s < n; s++) {
            const int32_t i = p->i;
            const float frac = p->frac;
            /* left side: j = i, i-1, ... (distance frac, frac+1, ...); right: j = i+1, ... (1-frac, ...) */
            const int nl = i + 1 < taps ? i + 1 : taps;
            const int nr = last - i < taps ? last - i : taps;
            float y = (nl > 0 ? side(p->data, i, -1, nl, frac, scale) : 0.0f) +
                      (nr > 0 ? side(p->data, i + 1, 1, nr, 1.0f - frac, scale) : 0.0f);
            y *= gain;
            if (!p->loop) { /* one-shot: fade out before the end of the region */
                const float left = p->dir > 0 ? (float)(p->end - i) - frac : (float)(i - p->start) + frac;
                if (left < (float)SPLAY_FADE)
                    y *= left > 0.0f ? left * inv_fade : 0.0f;
            }
            out[s] = y;

            r += dr;
            const float f = frac + (float)p->dir * r;
            int32_t k = (int32_t)f;
            if (f < (float)k)
                k--;
            p->frac = f - (float)k;
            p->i = i + k;
            const int32_t len = p->end - p->start;
            if (p->dir > 0 && p->i >= p->end) {
                if (!p->loop)
                    break;
                p->i -= len * ((p->i - p->start) / len);
            } else if (p->dir < 0 && p->i < p->start) {
                if (!p->loop)
                    break;
                p->i += len * ((p->end - 1 - p->i) / len);
            }
        }
        if (s < n) { /* reached the end */
            p->active = 0;
            s++;
        }
    }
    for (; s < n; s++)
        out[s] = 0.0f;
    return p->active;
}
