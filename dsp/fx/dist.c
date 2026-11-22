/* dist.c — voice distortion (see dist.h): input -> 2x -> drive and shaper -> 1x -> level -> tone low-pass ->
 * dry/wet mix. The order of the operations is the one the acid engine's distortion has always had (it uses this
 * module), so its sound is unchanged to the bit. */
#include "dist.h"
#include "../core/fastmath.h"

static inline float shape(int mode, float u, float bias, float b0)
{
    switch (mode) {
    case DIST_SOFT:
        return fm_tanh(u);
    case DIST_TUBE:
        return fm_tanh(u + bias) - b0; /* biased: even harmonics; no offset at rest */
    case DIST_HARD: {
        const float c = dsp_clampf(u, -1.5f, 1.5f);
        return c - c * c * c * (1.0f / 6.75f); /* reaches +/-1 at +/-1.5 with a flat top */
    }
    case DIST_FOLD:
        return fm_sin(0.25f * u); /* sin(pi u / 2): folds back past +/-1 */
    default:
        return u;
    }
}

void dist_init(dist_t *d, float sample_rate)
{
    d->sr = sample_rate;
    d->mode = DIST_OFF;
    d->g = d->g_prev = 1.0f;
    d->comp = d->comp_prev = 1.0f;
    d->mix = d->mix_prev = 1.0f;
    d->bias = DIST_TUBE_BIAS;
    d->b0 = fm_tanh(d->bias);
    d->a = 1.0f;
    dist_reset(d);
}

void dist_reset(dist_t *d)
{
    hb2x_reset(&d->os);
    for (int i = 0; i < DIST_DRY_N; i++)
        d->dry[i] = 0.0f;
    d->dpos = 0;
    d->lp = 0.0f;
    d->g_prev = d->g;
    d->comp_prev = d->comp;
    d->mix_prev = d->mix;
}

void dist_set_bias(dist_t *d, float bias)
{
    d->bias = bias;
    d->b0 = fm_tanh(bias);
}

void dist_set(dist_t *d, dist_mode_t mode, float drive_db, float tone_hz, float mix)
{
    const int switched_on = d->mode == DIST_OFF && mode != DIST_OFF;
    d->mode = (int)mode;
    d->g = fm_db2lin(dsp_clampf(drive_db, 0.0f, 36.0f));
    /* level compensation: the shaper's gain at a typical peak of 0.7 (the biased tanh is measured unbiased); the
     * fold's output stays within +/-1 whatever the drive (it may pass through zero at 0.7): a fixed 0.7 */
    if (mode == DIST_FOLD)
        d->comp = 0.7f;
    else {
        const float ref = mode == DIST_HARD ? shape(DIST_HARD, 0.7f * d->g, 0.0f, 0.0f) : fm_tanh(0.7f * d->g);
        d->comp = 0.7f / dsp_maxf(dsp_absf(ref), 0.35f);
    }
    d->mix = dsp_clampf(mix, 0.0f, 1.0f);
    const float fc = dsp_clampf(tone_hz, 200.0f, 0.45f * d->sr);
    d->a = 1.0f - fm_exp2(-FM_LOG2E * FM_TWO_PI * fc / d->sr);
    if (switched_on)
        dist_reset(d); /* no stale history, no ramp from old settings */
}

void dist_process(dist_t *d, const float *in, float *out, int n)
{
    if (d->mode == DIST_OFF || n <= 0) {
        if (out != in)
            for (int i = 0; i < n; i++)
                out[i] = in[i];
        return;
    }
    const float inv_n = 1.0f / (float)n;
    const float dg = (d->g - d->g_prev) * inv_n, dc = (d->comp - d->comp_prev) * inv_n, dm = (d->mix - d->mix_prev) * inv_n;
    const int wet_only = d->mix >= 1.0f && d->mix_prev >= 1.0f;
    float g = d->g_prev, c = d->comp_prev, m = d->mix_prev, lp = d->lp;
    const float a = d->a, bias = d->bias, b0 = d->b0;
    const int mode = d->mode;
    for (int i = 0; i < n; i++) {
        g += dg, c += dc, m += dm;
        const float x = in[i];
        float u0, u1;
        hb2x_up(&d->os, x, &u0, &u1);
        u0 = shape(mode, g * u0, bias, b0);
        u1 = shape(mode, g * u1, bias, b0);
        const float y = hb2x_down(&d->os, u0, u1) * c;
        lp += a * (y - lp);
        /* the dry input DIST_LATENCY (11.5) samples ago: 4-point Lagrange at the half sample (10..13 ago) */
        d->dry[d->dpos] = x;
        const float *b = d->dry;
        const int q = d->dpos;
        const float dry = 0.5625f * (b[(q - 11) & (DIST_DRY_N - 1)] + b[(q - 12) & (DIST_DRY_N - 1)]) -
                          0.0625f * (b[(q - 10) & (DIST_DRY_N - 1)] + b[(q - 13) & (DIST_DRY_N - 1)]);
        d->dpos = (d->dpos + 1) & (DIST_DRY_N - 1);
        out[i] = wet_only ? lp : dry + m * (lp - dry);
    }
    d->lp = lp;
    d->g_prev = d->g, d->comp_prev = d->comp, d->mix_prev = d->mix;
}
