/* scream.c — 2-pole low-pass with a saturating feedback node (see scream.h).
 * One-pole TPT stage: v = G (x - s), y = v + s, s <- y + v, with G = g / (1 + g), g = tan(pi fc / fs).
 * Structure: y1 = LP1(x); the feedback sum S = K (1-G)^2 s2 - (1-G) s3 (states of LP2 and of the feedback
 * high-pass) is solved without delay: u = a0 (y1 + S), a0 = 1 / (1 - K G + K G^2); u saturates, y = LP2(u),
 * the high-pass follows K y. G, K and a0 are interpolated per sample over the block (no division per sample). */
#include "scream.h"
#include "../core/fastmath.h"

void scream_init(scream_t *f, float sample_rate)
{
    f->sr = sample_rate;
    f->oversample = 1;
    scream_reset(f);
    scream_set(f, 1000.0f, 0.0f, 0.0f);
}

void scream_reset(scream_t *f)
{
    f->z1 = f->z2 = f->z3 = 0.0f;
    hb4x_reset(&f->hb);
    f->snap = 1;
}

void scream_set_oversampling(scream_t *f, int factor)
{
    factor = hb_factor(factor);
    if (factor != f->oversample) {
        f->oversample = factor;
        hb4x_reset(&f->hb);
        f->snap = 1;
    }
}

void scream_set(scream_t *f, float cutoff_hz, float resonance, float drive)
{
    const float fs = (float)f->oversample * f->sr;
    const float fc = dsp_clampf(cutoff_hz, 20.0f, 0.45f * f->sr);
    const float g = fm_tan_pi(fc / fs);
    f->tG = g / (1.0f + g);
    f->tk = 0.01f + 2.19f * dsp_clampf(resonance, 0.0f, 1.0f); /* loop gain K/2: oscillates from ~0.91 */
    f->drive = 1.0f + 6.0f * dsp_clampf(drive, 0.0f, 1.0f);
    if (f->snap) {
        f->G = f->tG, f->k = f->tk;
        f->snap = 0;
    }
}

static inline float tick(scream_t *f, float x, float G, float k, float a0, float drive, float inv_drive)
{
    const float b = 1.0f - G;
    float v = G * (x - f->z1);
    const float y1 = v + f->z1;
    f->z1 = y1 + v;
    float u = a0 * (y1 + k * b * b * f->z2 - b * f->z3);
    u = fm_tanh(drive * u) * inv_drive; /* the saturating node: screams instead of exploding */
    v = G * (u - f->z2);
    const float lp = v + f->z2;
    f->z2 = lp + v;
    const float y = k * lp;
    v = G * (y - f->z3);
    f->z3 = v + f->z3 + v;
    return lp;
}

DSP_FAST_CODE void scream_process(scream_t *f, const float *in, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float a00 = 1.0f / (1.0f - f->k * f->G + f->k * f->G * f->G);
    const float a01 = 1.0f / (1.0f - f->tk * f->tG + f->tk * f->tG * f->tG);
    const float dG = (f->tG - f->G) * inv_n, dk = (f->tk - f->k) * inv_n, da = (a01 - a00) * inv_n;
    const float drive = f->drive, inv_drive = 1.0f / drive;
    float G = f->G, k = f->k, a0 = a00;
    for (int i = 0; i < n; i++) {
        G += dG, k += dk, a0 += da;
        if (f->oversample == 1) {
            out[i] = tick(f, in[i], G, k, a0, drive, inv_drive);
        } else if (f->oversample == 2) {
            float a, b;
            hb2x_up(&f->hb.outer, in[i], &a, &b);
            const float ya = tick(f, a, G, k, a0, drive, inv_drive), yb = tick(f, b, G, k, a0, drive, inv_drive);
            out[i] = hb2x_down(&f->hb.outer, ya, yb);
        } else {
            float u[4], y[4];
            hb4x_up(&f->hb, in[i], u);
            for (int j = 0; j < 4; j++)
                y[j] = tick(f, u[j], G, k, a0, drive, inv_drive);
            out[i] = hb4x_down(&f->hb, y);
        }
    }
    f->G = f->tG, f->k = f->tk;
}
