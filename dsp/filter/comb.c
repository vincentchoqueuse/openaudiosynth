/* comb.c — feedback comb filter (see comb.h). */
#include "comb.h"

void comb_init(comb_t *c, float sample_rate)
{
    c->sr = sample_rate;
    c->d = c->td = 100.0f;
    c->g = c->tg = 0.0f;
    c->a = c->ta = 0.0f;
    comb_reset(c);
}

void comb_reset(comb_t *c)
{
    for (int i = 0; i < COMB_SIZE; i++)
        c->buf[i] = 0.0f;
    c->w = 0;
    c->lp = 0.0f;
    c->snap = 1;
}

void comb_set(comb_t *c, float freq_hz, float feedback, float damping)
{
    float d = c->sr / dsp_clampf(freq_hz, 20.0f, 0.45f * c->sr);
    while (d > (float)(COMB_SIZE - 4))
        d *= 0.5f; /* an octave up: same harmonics */
    c->td = dsp_maxf(d, 2.0f);
    c->tg = dsp_clampf(feedback, -0.98f, 0.98f);
    c->ta = 0.95f * dsp_clampf(damping, 0.0f, 1.0f);
    if (c->snap) {
        c->d = c->td, c->g = c->tg, c->a = c->ta;
        c->snap = 0;
    }
}

DSP_FAST_CODE void comb_process(comb_t *c, const float *in, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dd = (c->td - c->d) * inv_n, dg = (c->tg - c->g) * inv_n, da = (c->ta - c->a) * inv_n;
    float d = c->d, g = c->g, a = c->a, lp = c->lp;
    int w = c->w;
    const int mask = COMB_SIZE - 1;
    for (int i = 0; i < n; i++) {
        d += dd, g += dg, a += da;
        /* read y[n - d] with 4-point Hermite interpolation around the fractional position */
        const float pos = (float)(w + COMB_SIZE) - d; /* > 0: d < COMB_SIZE - 4 */
        const int i0 = (int)pos;
        const float t = pos - (float)i0;
        const float ym1 = c->buf[(i0 - 1) & mask], y0 = c->buf[i0 & mask], y1 = c->buf[(i0 + 1) & mask],
                    y2 = c->buf[(i0 + 2) & mask];
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2,
                    c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        const float delayed = ((c3 * t + c2) * t + c1) * t + y0;
        lp += (1.0f - a) * (delayed - lp); /* damping in the loop */
        const float y = in[i] + g * lp;
        c->buf[w] = y;
        w = (w + 1) & mask;
        out[i] = y * (1.0f - dsp_absf(g)); /* peak gain ~1 whatever the feedback */
    }
    c->d = d, c->g = g, c->a = a, c->lp = lp, c->w = w;
}
