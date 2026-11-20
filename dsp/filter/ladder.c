/* ladder.c — ZDF ladder (see ladder.h).
 * One-pole TPT stage: y = G x + (1 - G) s, s <- 2y - s, with G = g/(1+g), g = tan(pi fc / fs).
 * Cascade: y4 = G^4 u + S, S = (1-G)(G^3 s1 + G^2 s2 + G s3 + s4); feedback u = x - k y4 solved
 * without delay: u = (x - k S) d, d = 1/(1 + k G^4). The input then goes through 2 tanh(u/2): unity
 * gain for small signals, bounded for large ones (this also bounds self-oscillation).
 * G, k and d are interpolated linearly per sample over the block (no division in the loop). */
#include "ladder.h"
#include "../core/fastmath.h"


void ladder_init(ladder_t *l, float sr)
{
    l->sr = sr;
    l->comp_amount = 0.35f;
    l->mode = LADDER_LP24;
    l->oversample = 1;
    ladder_reset(l);
    ladder_set(l, 1000.0f, 0.0f, 0.0f);
    l->snap = 1;
}

void ladder_reset(ladder_t *l)
{
    for (int i = 0; i < 4; i++)
        l->s[i] = 0.0f;
    hb4x_reset(&l->hb);
    l->snap = 1;
}

void ladder_set_mode(ladder_t *l, ladder_mode_t mode)
{
    l->mode = (unsigned)mode < (unsigned)LADDER_MODE_COUNT ? (int)mode : LADDER_LP24;
}

void ladder_set_oversampling(ladder_t *l, int factor)
{
    factor = hb_factor(factor);
    if (factor != l->oversample) {
        l->oversample = factor;
        hb4x_reset(&l->hb);
        l->snap = 1;
    }
}

void ladder_set(ladder_t *l, float cutoff_hz, float resonance, float drive)
{
    const float fs = (float)l->oversample * l->sr;
    const float fc = dsp_clampf(cutoff_hz, 10.0f, 0.45f * l->sr);
    const float g = fm_tan_pi(fc / fs);
    const float G = g / (1.0f + g);
    const float k = 4.2f * dsp_clampf(resonance, 0.0f, 1.0f);
    const float G2 = G * G;
    l->tG = G;
    l->tk = k;
    l->td = 1.0f / (1.0f + k * G2 * G2);
    l->drive = 1.0f + 6.0f * dsp_clampf(drive, 0.0f, 1.0f);
    l->inv_drive = 1.0f / l->drive;
    l->comp = 1.0f + l->comp_amount * k; /* partial passband compensation for the resonance */
    if (l->snap) {
        l->G = l->tG, l->k = l->tk, l->d = l->td;
        l->snap = 0;
    }
}

/* One sample of the ladder at the internal rate. */
static inline float tick(ladder_t *l, float x, float G, float k, float d)
{
    const float b = 1.0f - G, G2 = G * G;
    const float S = b * (G2 * G * l->s[0] + G2 * l->s[1] + G * l->s[2] + l->s[3]);
    float u = (x * l->drive - k * S) * d;
    u = 2.0f * fm_tanh(0.5f * u); /* saturating input stage */
    float y[4], v = u;
    for (int i = 0; i < 4; i++) {
        y[i] = G * v + b * l->s[i];
        l->s[i] = 2.0f * y[i] - l->s[i];
        v = y[i];
    }
    const float out = l->mode == LADDER_LP24 ? y[3] : l->mode == LADDER_LP18 ? y[2] : y[1];
    return out * l->inv_drive * l->comp;
}

/* One input sample through the filter at the oversampling factor. */
static inline float run(ladder_t *l, float x, float G, float k, float d)
{
    if (l->oversample == 1)
        return tick(l, x, G, k, d);
    if (l->oversample == 2) {
        float a, b;
        hb2x_up(&l->hb.outer, x, &a, &b);
        const float ya = tick(l, a, G, k, d), yb = tick(l, b, G, k, d);
        return hb2x_down(&l->hb.outer, ya, yb);
    }
    float u[4], y[4];
    hb4x_up(&l->hb, x, u);
    for (int j = 0; j < 4; j++)
        y[j] = tick(l, u[j], G, k, d);
    return hb4x_down(&l->hb, y);
}

DSP_FAST_CODE void ladder_process(ladder_t *l, const float *in, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dG = (l->tG - l->G) * inv_n, dk = (l->tk - l->k) * inv_n, dd = (l->td - l->d) * inv_n;
    float G = l->G, k = l->k, d = l->d;
    for (int i = 0; i < n; i++) {
        G += dG, k += dk, d += dd;
        out[i] = run(l, in[i], G, k, d);
    }
    l->G = l->tG, l->k = l->tk, l->d = l->td;
}

void ladder_set_compensation(ladder_t *l, float amount) { l->comp_amount = dsp_clampf(amount, 0.0f, 1.0f); }

DSP_FAST_CODE void ladder_process_fb(ladder_t *l, const float *in, const float *post, float fb, float hp_coef,
                                     float *state, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dG = (l->tG - l->G) * inv_n, dk = (l->tk - l->k) * inv_n, dd = (l->td - l->d) * inv_n;
    float G = l->G, k = l->k, d = l->d;
    float last = state[0], hp = state[1];
    for (int i = 0; i < n; i++) {
        G += dG, k += dk, d += dd;
        hp += (last - hp) * hp_coef;
        const float x = in[i] + fb * (last - hp);
        const float y = run(l, x, G, k, d);
        out[i] = y;
        last = y * post[i];
    }
    state[0] = last, state[1] = hp;
    l->G = l->tG, l->k = l->tk, l->d = l->td;
}
