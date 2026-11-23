/* flanger.c — stereo flanger (see flanger.h). The delay follows the LFO computed at each block's end and ramped per
 * sample; the delayed sample is read with 4-point Hermite interpolation, fed back (softly bounded by tanh) and
 * mixed with the dry signal, the wet level scaled by sqrt(1 - fb^2) so the resonances do not jump out. */
#include "flanger.h"
#include "../core/fastmath.h"

void flanger_init(flanger_t *f, float sr)
{
    f->sr = sr;
    f->phase = 0.0f;
    flanger_set(f, 0.0f, 0.2f, 4.0f, 0.8f, 0.5f);
    f->mix_prev = 0.0f;
    f->pol_prev = f->pol;
    flanger_reset(f);
}

static float delay_at(const flanger_t *f, float phase)
{
    const float sweep = 0.5f + 0.5f * fm_sin(phase); /* 0..1 */
    return dsp_maxf(f->delay * (1.0f - 0.95f * f->depth * sweep), 2.0f); /* the read needs one newer sample */
}

void flanger_reset(flanger_t *f)
{
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < FLANGER_BUF; i++)
            f->buf[c][i] = 0.0f;
        f->fb[c] = 0.0f;
        f->d_prev[c] = delay_at(f, f->phase + 0.25f * (float)c);
    }
    f->pos = 0;
}

void flanger_set(flanger_t *f, float mix, float rate_hz, float delay_ms, float depth, float feedback)
{
    f->mix = dsp_clampf(mix, 0.0f, 1.0f);
    f->inc = dsp_clampf(rate_hz, 0.0f, 20.0f) / f->sr;
    f->delay = dsp_clampf(delay_ms * 0.001f * f->sr, 2.0f, (float)(FLANGER_BUF - 4));
    f->depth = dsp_clampf(depth, 0.0f, 1.0f);
    f->feedback = dsp_clampf(feedback, -0.9f, 0.9f);
    f->comp = fm_exp2(0.5f * fm_log2(1.0f - f->feedback * f->feedback)); /* sqrt, fb^2 <= 0.81 */
    f->pol = f->feedback < 0.0f ? -1.0f : 1.0f;
}

/* 4-point Hermite read, d samples back from the last write (d >= 1) */
static inline float read(const float *b, int pos, float d)
{
    const int di = (int)d;
    const float t = d - (float)di;
    const float y0 = b[(pos - di + 1) & (FLANGER_BUF - 1)], y1 = b[(pos - di) & (FLANGER_BUF - 1)];
    const float y2 = b[(pos - di - 1) & (FLANGER_BUF - 1)], y3 = b[(pos - di - 2) & (FLANGER_BUF - 1)];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

DSP_FAST_CODE void flanger_process(flanger_t *f, float *L, float *R, int n)
{
    const float m0 = f->mix_prev, dm = (f->mix - m0) / (float)n;
    const float p0 = f->pol_prev, dp = (f->pol - p0) / (float)n;
    f->mix_prev = f->mix;
    f->pol_prev = f->pol;
    if (m0 <= 0.0f && f->mix <= 0.0f)
        return;
    if (m0 <= 0.0f) /* starting from off: no stale echoes */
        flanger_reset(f);
    f->phase = fm_frac(f->phase + f->inc * (float)n);
    float *io[2] = {L, R};
    const int pos0 = f->pos;
    for (int c = 0; c < 2; c++) {
        const float d0 = f->d_prev[c], d1 = delay_at(f, f->phase + 0.25f * (float)c);
        const float dd = (d1 - d0) / (float)n;
        f->d_prev[c] = d1;
        float *x = io[c], *b = f->buf[c], fb = f->fb[c];
        int pos = pos0;
        for (int i = 0; i < n; i++) {
            pos = (pos + 1) & (FLANGER_BUF - 1);
            b[pos] = x[i] + f->feedback * fm_tanh(fb);
            const float y = read(b, pos, d0 + dd * (float)(i + 1));
            fb = y;
            const float m = m0 + dm * (float)(i + 1), pol = p0 + dp * (float)(i + 1);
            x[i] = x[i] * (1.0f - 0.5f * m) + y * (0.5f * m * f->comp * pol);
        }
        f->fb[c] = fb;
        if (c == 1)
            f->pos = pos;
    }
}
