/* voice.h — the few steps every engine voice shares: constant-power pan, glide coefficient,
 * envelope x gain ramp, and the voice output (steal fade, NaN guard, pan into the stereo bus).
 * Engines get it through engine.h; keeping these in one place keeps every voice tail identical. */
#ifndef VOICE_H
#define VOICE_H

#include "../core/fastmath.h"
#include "vpoly.h"

#define VOICE_CENTER 0.70710678f /* constant-power gain of a centred mono source */

/* Constant-power pan: pos -1 (left) .. +1 (right) -> gains for L and R (quarter of a sine cycle). */
static inline void voice_pan(float pos, float *pan_l, float *pan_r)
{
    const float theta = (pos + 1.0f) * 0.125f;
    *pan_l = fm_cos(theta);
    *pan_r = fm_sin(theta);
}

/* One-pole glide: fraction of the remaining distance covered in n samples for a glide time in
 * seconds (time constant = time / 3, i.e. ~95 % of the way after `time`). */
static inline float voice_glide_coef(float time_s, int n, float sample_rate)
{
    return 1.0f - fm_exp2(-FM_LOG2E * 3.0f * (float)n / (time_s * sample_rate));
}

/* One-pole glide that arrives: the time constant is time / 4.6, so the pitch is within 1 % of the interval after
 * `time` (an RC lag set by its arrival, as a player hears it) instead of still trailing behind. */
static inline float voice_glide_arrive_coef(float time_s, int n, float sample_rate)
{
    return 1.0f - fm_exp2(-FM_LOG2E * 4.6f * (float)n / (time_s * sample_rate));
}

/* x[i] *= env[i] * g, g ramping linearly from g0 (excluded) to g1 (reached on the last sample). */
static inline void dsp_ramp_env_mul(float *restrict x, const float *restrict env, float g0, float g1, int n)
{
    const float dg = (g1 - g0) / (float)n;
    float g = g0;
    for (int i = 0; i < n; i++) {
        g += dg;
        x[i] *= env[i] * g;
    }
}

/* Per-voice level (ramped over the block) of voice vi, and its pan offset at the start and at the end of the
 * block (balance on top of the voice's own pan); nothing when no per-voice modulation is configured (bit-exact
 * with the plain path). Use voice_vmod_out, which also pans. */
static inline void voice_vmod_apply(vpoly_t *p, int vi, float *x, int n, float *pan_from, float *pan_to)
{
    *pan_from = *pan_to = 0.0f;
    if (!voicemod_cfg_active(p->vm_cfg))
        return;
    const float *o = vpoly_vmod(p, vi);
    voicemod_t *m = &p->vm[vi];
    const float g1 = o[VM_LEVEL] != 0.0f ? fm_db2lin(dsp_clampf(o[VM_LEVEL], -96.0f, 24.0f)) : 1.0f;
    const float g0 = m->fresh ? g1 : m->gain_prev;
    if (g0 != 1.0f || g1 != 1.0f)
        dsp_ramp_mul(x, g0, g1, n);
    m->gain_prev = g1;
    const float d = dsp_clampf(o[VM_PAN], -1.0f, 1.0f);
    *pan_from = m->fresh ? d : m->pan_prev;
    *pan_to = d;
    m->pan_prev = d;
    m->fresh = 0;
}

/* Gains of a pan offset d (-1..1) applied to a voice's own pan: the far side is attenuated. */
static inline void voice_pan_offset(float d, float pan_l, float pan_r, float *l, float *r)
{
    *l = d > 0.0f ? pan_l * (1.0f - d) : pan_l;
    *r = d < 0.0f ? pan_r * (1.0f + d) : pan_r;
}

/* NaN guard then pan of a mono voice block into the stereo bus (L, R accumulate). */
static inline void voice_pan_out(float *x, int n, float pan_l, float pan_r, float *L, float *R, uint32_t *nan_count)
{
    dsp_guard_block(x, n, nan_count);
    for (int i = 0; i < n; i++) {
        L[i] += x[i] * pan_l;
        R[i] += x[i] * pan_r;
    }
}

/* Per-voice modulation of level and pan of voice vi, NaN guard, then pan into the stereo bus. */
static inline void voice_vmod_out(vpoly_t *p, int vi, float *x, int n, float pan_l, float pan_r, float *L, float *R,
                                  uint32_t *nan_count)
{
    float d0, d1;
    voice_vmod_apply(p, vi, x, n, &d0, &d1);
    if (d0 == d1) { /* the usual case: a constant pan */
        voice_pan_offset(d1, pan_l, pan_r, &pan_l, &pan_r);
        voice_pan_out(x, n, pan_l, pan_r, L, R, nan_count);
        return;
    }
    /* a modulated pan glides over the block (a step every block would add sidebands at the control rate) */
    float l0, r0, l1, r1;
    voice_pan_offset(d0, pan_l, pan_r, &l0, &r0);
    voice_pan_offset(d1, pan_l, pan_r, &l1, &r1);
    dsp_guard_block(x, n, nan_count);
    const float dl = (l1 - l0) / (float)n, dr = (r1 - r0) / (float)n;
    for (int i = 0; i < n; i++) {
        l0 += dl, r0 += dr;
        L[i] += x[i] * l0;
        R[i] += x[i] * r0;
    }
}

/* Output of voice vi: applies its steal fade (vpoly), then voice_vmod_out. */
static inline void voice_out(vpoly_t *p, int vi, float *x, int n, float pan_l, float pan_r, float *L, float *R,
                             uint32_t *nan_count)
{
    float fade[SYNTH_BLOCK];
    if (vpoly_fade(p, vi, fade, n))
        for (int i = 0; i < n; i++)
            x[i] *= fade[i];
    voice_vmod_out(p, vi, x, n, pan_l, pan_r, L, R, nan_count);
}

/* The same for a voice that is already a stereo pair (xl, xr): steal fade, per-voice level and pan modulation
 * (the pan offset ramped over the block), NaN guard, then added to the bus. */
static inline void voice_out_stereo(vpoly_t *p, int vi, float *xl, float *xr, int n, float *L, float *R,
                                    uint32_t *nan_count)
{
    float g[SYNTH_BLOCK];
    if (vpoly_fade(p, vi, g, n))
        for (int i = 0; i < n; i++)
            xl[i] *= g[i], xr[i] *= g[i];
    for (int i = 0; i < n; i++)
        g[i] = 1.0f;
    float d0, d1, l0, r0, l1, r1;
    voice_vmod_apply(p, vi, g, n, &d0, &d1); /* g: the level modulation ramp (left at 1 without one) */
    voice_pan_offset(d0, 1.0f, 1.0f, &l0, &r0);
    voice_pan_offset(d1, 1.0f, 1.0f, &l1, &r1);
    dsp_guard_block(xl, n, nan_count);
    dsp_guard_block(xr, n, nan_count);
    const float inv_n = 1.0f / (float)n, dl = (l1 - l0) * inv_n, dr = (r1 - r0) * inv_n;
    for (int i = 0; i < n; i++) {
        l0 += dl, r0 += dr;
        L[i] += xl[i] * g[i] * l0;
        R[i] += xr[i] * g[i] * r0;
    }
}

#endif /* VOICE_H */
