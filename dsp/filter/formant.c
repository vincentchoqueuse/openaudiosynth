/* formant.c — vowel filter (see formant.h). */
#include "formant.h"
#include "../core/fastmath.h"

static const float F[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
static const float A[5][3] = {{1.0f, 0.50f, 0.25f}, {1.0f, 0.45f, 0.35f}, {1.0f, 0.30f, 0.30f}, {1.0f, 0.55f, 0.20f},
                              {1.0f, 0.40f, 0.15f}};

void formant_init(formant_t *f, float sample_rate)
{
    for (int k = 0; k < 3; k++) {
        svf_init(&f->bp[k], sample_rate);
        svf_set_mode(&f->bp[k], SVF_BP);
        f->gain[k] = 1.0f;
    }
    formant_set(f, 0.0f, 0.0f, 6.0f);
}

void formant_reset(formant_t *f)
{
    for (int k = 0; k < 3; k++)
        svf_reset(&f->bp[k]);
}

void formant_set(formant_t *f, float vowel, float shift_oct, float q)
{
    const float x = dsp_clampf(vowel, 0.0f, 1.0f) * 4.0f;
    const int i = x >= 4.0f ? 3 : (int)x;
    const float m = x - (float)i;
    const float shift = fm_exp2(dsp_clampf(shift_oct, -2.0f, 2.0f));
    for (int k = 0; k < 3; k++) {
        const float hz = ((1.0f - m) * F[i][k] + m * F[i + 1][k]) * shift;
        svf_set(&f->bp[k], hz, dsp_clampf(q, 0.7f, 30.0f));
        f->gain[k] = (1.0f - m) * A[i][k] + m * A[i + 1][k];
    }
}

void formant_process(formant_t *f, const float *in, float *out, int n)
{
    float acc[SYNTH_BLOCK], tmp[SYNTH_BLOCK];
    for (int s = 0; s < n; s += SYNTH_BLOCK) {
        const int m = n - s < SYNTH_BLOCK ? n - s : SYNTH_BLOCK;
        for (int i = 0; i < m; i++)
            acc[i] = 0.0f;
        for (int k = 0; k < 3; k++) {
            svf_process(&f->bp[k], in + s, tmp, m);
            for (int i = 0; i < m; i++)
                acc[i] += f->gain[k] * tmp[i];
        }
        for (int i = 0; i < m; i++)
            out[s + i] = acc[i];
    }
}
