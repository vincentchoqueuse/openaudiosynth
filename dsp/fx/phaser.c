/* phaser.c — stereo phaser (see phaser.h). Each stage is a TPT one-pole: v = (x - s) G, lp = v + s, s = lp + v,
 * all-pass = 2 lp - x, with G = g / (1 + g), g = tan(pi fc / sr), computed at each block's end and ramped. The
 * feedback is softly bounded (tanh) and the wet level scaled by sqrt(1 - fb^2) so the resonance does not jump out. */
#include "phaser.h"
#include "../core/fastmath.h"

void phaser_init(phaser_t *p, float sr)
{
    p->sr = sr;
    p->inv_sr = 1.0f / sr;
    p->phase = 0.0f;
    phaser_set(p, 0.0f, 0.3f, 700.0f, 0.6f, 0.4f, 4);
    p->mix_prev = 0.0f;
    phaser_reset(p);
}

static float coef(const phaser_t *p, float lfo01)
{
    const float fc = dsp_clampf(p->centre * fm_exp2(p->octaves * (lfo01 - 0.5f)), 20.0f, 0.45f * p->sr);
    const float g = fm_tan_pi(fc * p->inv_sr);
    return g / (1.0f + g);
}

void phaser_reset(phaser_t *p)
{
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < PHASER_MAX_STAGES; k++)
            p->s[c][k] = 0.0f;
        p->fb[c] = 0.0f;
        p->G_prev[c] = coef(p, 0.5f + 0.5f * fm_sin(p->phase + 0.25f * (float)c));
    }
}

void phaser_set(phaser_t *p, float mix, float rate_hz, float centre_hz, float depth, float feedback, int stages)
{
    p->mix = dsp_clampf(mix, 0.0f, 1.0f);
    p->inc = dsp_clampf(rate_hz, 0.0f, 20.0f) * p->inv_sr;
    p->centre = dsp_clampf(centre_hz, 20.0f, 0.4f * p->sr);
    p->octaves = 4.0f * dsp_clampf(depth, 0.0f, 1.0f);
    p->feedback = dsp_clampf(feedback, -0.9f, 0.9f);
    p->comp = fm_exp2(0.5f * fm_log2(1.0f - p->feedback * p->feedback)); /* sqrt, fb^2 <= 0.81 */
    p->stages = dsp_clampi(stages & ~1, 2, PHASER_MAX_STAGES);
}

DSP_FAST_CODE void phaser_process(phaser_t *p, float *L, float *R, int n)
{
    const float m0 = p->mix_prev, dm = (p->mix - m0) / (float)n;
    p->mix_prev = p->mix;
    if (m0 <= 0.0f && p->mix <= 0.0f)
        return;
    if (m0 <= 0.0f) /* starting from off: no stale state */
        phaser_reset(p);
    p->phase = fm_frac(p->phase + p->inc * (float)n);
    float *io[2] = {L, R};
    for (int c = 0; c < 2; c++) {
        const float G0 = p->G_prev[c], G1 = coef(p, 0.5f + 0.5f * fm_sin(p->phase + 0.25f * (float)c));
        const float dG = (G1 - G0) / (float)n;
        p->G_prev[c] = G1;
        float *x = io[c], *s = p->s[c], fb = p->fb[c];
        for (int i = 0; i < n; i++) {
            const float G = G0 + dG * (float)(i + 1);
            float y = x[i] + p->feedback * fm_tanh(fb);
            for (int k = 0; k < p->stages; k++) {
                const float v = (y - s[k]) * G, lp = v + s[k];
                s[k] = lp + v;
                y = 2.0f * lp - y;
            }
            fb = y;
            const float m = m0 + dm * (float)(i + 1);
            x[i] = x[i] * (1.0f - 0.5f * m) + y * (0.5f * m * p->comp);
        }
        p->fb[c] = fb;
    }
}
