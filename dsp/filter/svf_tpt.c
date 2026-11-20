/* svf_tpt.c — TPT SVF (see svf_tpt.h).
 * g = tan(pi*fc/SR) (bilinear prewarping: exact cutoff), k = 1/Q,
 * a1 = 1/(1 + g(g+k)), a2 = g*a1, a3 = g*a2. */
#include "svf_tpt.h"
#include "../core/fastmath.h"

void svf_init(svf_t *s, float sample_rate)
{
    s->inv_sr = 1.0f / sample_rate;
    s->fmax = 0.45f * sample_rate;
    s->mode = SVF_LP;
    svf_reset(s);
    svf_set(s, 1000.0f, 0.7071f);
    s->snap = 1; /* the first svf_set after init must not glide from these defaults */
}

void svf_reset(svf_t *s)
{
    s->ic1 = s->ic2 = 0.0f;
    s->snap = 1;
}

void svf_set_mode(svf_t *s, svf_mode_t mode)
{
    s->mode = ((unsigned)mode < (unsigned)SVF_MODE_COUNT) ? (int)mode : SVF_LP;
}

void svf_set(svf_t *s, float cutoff_hz, float q)
{
    const float fc = dsp_clampf(cutoff_hz, 5.0f, s->fmax);
    q = dsp_clampf(q, 0.1f, 50.0f);
    const float g = fm_tan_pi(fc * s->inv_sr);
    const float k = 1.0f / q;
    s->ta1 = 1.0f / (1.0f + g * (g + k));
    s->ta2 = g * s->ta1;
    s->ta3 = g * s->ta2;
    s->tk = k;
    if (s->snap) {
        s->a1 = s->ta1;
        s->a2 = s->ta2;
        s->a3 = s->ta3;
        s->k = s->tk;
        s->snap = 0;
    }
}

DSP_FAST_CODE void svf_process(svf_t *s, const float *in, float *out, int n)
{
    /* output = m0*v0 + m1*k*v1 + m2*v2 */
    static const float mix[SVF_MODE_COUNT][3] = {
        {0.0f, 0.0f, 1.0f},   /* LP */
        {0.0f, 1.0f, 0.0f},   /* normalized BP */
        {1.0f, -1.0f, -1.0f}, /* HP */
        {1.0f, -1.0f, 0.0f},  /* notch */
    };
    const float m0 = mix[s->mode][0], m1 = mix[s->mode][1], m2 = mix[s->mode][2];
    const float inv_n = 1.0f / (float)n;
    const float da1 = (s->ta1 - s->a1) * inv_n, da2 = (s->ta2 - s->a2) * inv_n;
    const float da3 = (s->ta3 - s->a3) * inv_n, dk = (s->tk - s->k) * inv_n;
    float a1 = s->a1, a2 = s->a2, a3 = s->a3, k = s->k;
    float ic1 = s->ic1, ic2 = s->ic2;

    for (int i = 0; i < n; i++) {
        a1 += da1;
        a2 += da2;
        a3 += da3;
        k += dk;
        const float v0 = in[i];
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        out[i] = m0 * v0 + m1 * k * v1 + m2 * v2;
    }
    s->ic1 = ic1;
    s->ic2 = ic2;
    s->a1 = s->ta1;
    s->a2 = s->ta2;
    s->a3 = s->ta3;
    s->k = s->tk;
}
