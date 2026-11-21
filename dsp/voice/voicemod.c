/* voicemod.c — per-voice modulation (see voicemod.h). */
#include "voicemod.h"
#include "../core/fastmath.h"

static const float dst_scale[VM_DST_COUNT] = {VM_PITCH_RANGE, VM_LEVEL_RANGE, 1.0f, VM_BRIGHT_RANGE, 1.0f};

void voicemod_init(voicemod_t *m, uint32_t seed)
{
    for (int i = 0; i < VM_SRC_COUNT; i++)
        m->src[i] = 0.0f;
    for (int i = 0; i < VM_DST_COUNT; i++)
        m->out[i] = 0.0f;
    m->lfo_phase = m->lfo_sh = m->tune = m->bend = 0.0f;
    m->gain_prev = 1.0f;
    m->pan_prev = 0.0f;
    m->fresh = 1;
    m->rng = dsp_seed(seed);
    m->env.seg = 1, m->env.pos = 0.0f, m->env.from = m->env.value = 0.0f, m->env.gate = 0;
}

int voicemod_cfg_active(const voicemod_cfg_t *c) { return c && (c->routes > 0 || c->tuning || c->mpe); }

void voicemod_note(voicemod_t *m, const voicemod_cfg_t *c, int note)
{
    m->src[VM_SRC_NOTE] = ((float)note - 60.0f) * (1.0f / 60.0f);
    m->tune = c && c->tuning ? c->tuning[((note - c->tuning_root) % 12 + 12) % 12] : 0.0f;
}

void voicemod_start(voicemod_t *m, const voicemod_cfg_t *c, int note, float velocity, int hard)
{
    voicemod_note(m, c, note);
    m->src[VM_SRC_VELOCITY] = velocity;
    m->src[VM_SRC_RANDOM] = dsp_rand_bipolar(&m->rng);
    if (hard) {
        m->bend = m->src[VM_SRC_PRESSURE] = m->src[VM_SRC_SLIDE] = 0.0f;
        m->lfo_phase = 0.0f;
        m->fresh = 1;
        if (c)
            mseg_reset(&m->env, &c->shape);
    }
    if (c)
        mseg_gate(&m->env, &c->shape, 1);
}

void voicemod_release(voicemod_t *m, const voicemod_cfg_t *c)
{
    if (c)
        mseg_gate(&m->env, &c->shape, 0);
}

void voicemod_block(voicemod_t *m, const voicemod_cfg_t *c)
{
    for (int i = 0; i < VM_DST_COUNT; i++)
        m->out[i] = 0.0f;
    if (!voicemod_cfg_active(c))
        return;
    if (c->routes > 0) {
        m->src[VM_SRC_MSEG] = mseg_advance(&m->env, &c->shape, c->dt);
        const float prev = m->lfo_phase;
        const float hz = c->lfo_div > 0 ? c->beats_per_s / c->lfo_rate : c->lfo_rate; /* lfo_rate holds beats when synced */
        m->lfo_phase = fm_frac(m->lfo_phase + hz * c->dt);
        if (m->lfo_phase < prev)
            m->lfo_sh = dsp_rand_bipolar(&m->rng);
        const float ph = m->lfo_phase;
        float l;
        switch (c->lfo_wave) {
        case 1: l = 1.0f - 4.0f * dsp_absf(ph - 0.5f); break;
        case 2: l = 2.0f * ph - 1.0f; break;
        case 3: l = ph < 0.5f ? 1.0f : -1.0f; break;
        case 4: l = m->lfo_sh; break;
        case 5: l = voicemod_fourier(c->fourier, ph); break;
        case 6: l = voicemod_wiener(c->wiener, ph); break;
        default: l = fm_sin(ph);
        }
        m->src[VM_SRC_LFO] = l;
        for (int r = 0; r < VM_ROUTES; r++) {
            const int s = c->src[r], d = c->dst[r];
            if (s > 0 && s < VM_SRC_COUNT && d >= 0 && d < VM_DST_COUNT)
                m->out[d] += c->amount[r] * m->src[s] * dst_scale[d];
        }
    }
    m->out[VM_PITCH] += m->tune + (c->mpe ? m->bend * c->mpe_bend_range : 0.0f);
}
