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
    /* both channels in one loop, their stages side by side: two independent chains the FPU can overlap (the same
     * operations in the same order per channel, so the output is unchanged) */
    const float Ga0 = p->G_prev[0], Ga1 = coef(p, 0.5f + 0.5f * fm_sin(p->phase));
    const float Gb0 = p->G_prev[1], Gb1 = coef(p, 0.5f + 0.5f * fm_sin(p->phase + 0.25f));
    const float dGa = (Ga1 - Ga0) / (float)n, dGb = (Gb1 - Gb0) / (float)n;
    p->G_prev[0] = Ga1;
    p->G_prev[1] = Gb1;
    const int st = p->stages;
    const float feedback = p->feedback, comp = p->comp;
    float sa[PHASER_MAX_STAGES], sb[PHASER_MAX_STAGES];
    for (int k = 0; k < st; k++) {
        sa[k] = p->s[0][k];
        sb[k] = p->s[1][k];
    }
    float fba = p->fb[0], fbb = p->fb[1];
    for (int i = 0; i < n; i++) {
        const float Ga = Ga0 + dGa * (float)(i + 1), Gb = Gb0 + dGb * (float)(i + 1);
        float ya = L[i] + feedback * fm_tanh(fba), yb = R[i] + feedback * fm_tanh(fbb);
        for (int k = 0; k < st; k++) {
            const float va = (ya - sa[k]) * Ga, lpa = va + sa[k];
            const float vb = (yb - sb[k]) * Gb, lpb = vb + sb[k];
            sa[k] = lpa + va;
            sb[k] = lpb + vb;
            ya = 2.0f * lpa - ya;
            yb = 2.0f * lpb - yb;
        }
        fba = ya;
        fbb = yb;
        const float m = m0 + dm * (float)(i + 1);
        L[i] = L[i] * (1.0f - 0.5f * m) + ya * (0.5f * m * comp);
        R[i] = R[i] * (1.0f - 0.5f * m) + yb * (0.5f * m * comp);
    }
    for (int k = 0; k < st; k++) {
        p->s[0][k] = sa[k];
        p->s[1][k] = sb[k];
    }
    p->fb[0] = fba;
    p->fb[1] = fbb;
}
