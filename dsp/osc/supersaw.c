/* supersaw.c — detuned saw stack (see supersaw.h).
 * Saw k sits at offset[k] * detune cents (+ its own LFO), offsets irregular and slightly asymmetric.
 * Gains: centre 1, sides `side`, both scaled by 1/sqrt(1 + (count-1)·side²) so the incoherent sum keeps
 * a roughly constant loudness when the mix changes. */
#include "supersaw.h"
#include "../core/fastmath.h"

/* Index 0 is the centre; then pairs from inner to outer. Irregular spacing avoids a common period. */
static const float offset[SUPERSAW_MAX] = {0.0f, -0.18f, 0.19f, -0.57f, 0.56f, -1.0f, 0.98f};

void supersaw_init(supersaw_t *s, float sample_rate, uint32_t seed)
{
    for (int k = 0; k < SUPERSAW_MAX; k++) {
        osc_init(&s->saw[k], sample_rate);
        osc_set_wave(&s->saw[k], OSC_SAW);
    }
    s->rng = dsp_seed(seed);
    s->count = SUPERSAW_MAX;
    s->gain_center = s->g_center_prev = 1.0f;
    s->gain_side = s->g_side_prev = 0.0f;
    supersaw_reset(s);
}

void supersaw_reset(supersaw_t *s)
{
    for (int k = 0; k < SUPERSAW_MAX; k++) {
        osc_reset(&s->saw[k], dsp_rand_unipolar(&s->rng));
        s->lfo_phase[k] = dsp_rand_unipolar(&s->rng);
        s->lfo_factor[k] = 0.6f + 0.8f * dsp_rand_unipolar(&s->rng);
    }
    s->g_center_prev = s->gain_center;
    s->g_side_prev = s->gain_side;
}

void supersaw_set(supersaw_t *s, float freq_hz, float detune_cents, float side, float motion_cents, float motion_hz,
                  int count, int block)
{
    count = dsp_clampi(count | 1, 1, SUPERSAW_MAX); /* odd: centre + pairs */
    s->count = count;
    side = dsp_clampf(side, 0.0f, 1.0f);
    /* loudness normalization 1/sqrt(pw), once per block: 2^(-log2(pw)/2) */
    const float pw = 1.0f + (float)(count - 1) * side * side;
    const float g = fm_exp2(-0.5f * fm_log2(pw));
    s->g_center_prev = s->gain_center;
    s->g_side_prev = s->gain_side;
    s->gain_center = g;
    s->gain_side = g * side;

    for (int k = 0; k < count; k++) {
        float cents = offset[k] * detune_cents;
        if (motion_cents > 0.0f) {
            s->lfo_phase[k] += motion_hz * s->lfo_factor[k] * (float)block * s->saw[k].inv_sr;
            if (s->lfo_phase[k] >= 1.0f)
                s->lfo_phase[k] -= (float)(int)s->lfo_phase[k];
            cents += motion_cents * fm_sin(s->lfo_phase[k]);
        }
        osc_set(&s->saw[k], freq_hz * fm_exp2(cents * (1.0f / 1200.0f)), 0.5f);
    }
}

DSP_FAST_CODE void supersaw_process(supersaw_t *s, float *restrict out, int n)
{
    float t[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        out[i] = 0.0f;
    for (int k = 0; k < s->count; k++) {
        const float g0 = k == 0 ? s->g_center_prev : s->g_side_prev;
        const float g1 = k == 0 ? s->gain_center : s->gain_side;
        /* n may exceed a block (tests): process in SYNTH_BLOCK chunks with the same gain ramp */
        for (int done = 0; done < n; done += SYNTH_BLOCK) {
            const int m = n - done < SYNTH_BLOCK ? n - done : SYNTH_BLOCK;
            const float ga = g0 + (g1 - g0) * (float)done / (float)n;
            const float gb = g0 + (g1 - g0) * (float)(done + m) / (float)n;
            osc_process(&s->saw[k], t, m);
            dsp_ramp_mac(out + done, t, ga, gb, m);
        }
    }
}
