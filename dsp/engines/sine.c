/* sine.c — reference sine engine (see sine.h). Smallest example of an engine: per-voice DSP state,
 * vpoly callbacks, and an engine_ops_t. Copy this file to start a new engine. */
#include "sine.h"
#include "../core/fastmath.h"

#define SINE_VOICE_GAIN 0.125f /* -18 dBFS per voice */

const param_desc_t sine_params[SINE_P_COUNT] = {
    PARAM(SINE_P_ATTACK, "attack", "amp.attack", 0.0005f, 30.0f, 0.005f, PARAM_EXP, "s", 1, NULL, "attack time"),
    PARAM(SINE_P_DECAY, "decay", "amp.decay", 0.0005f, 30.0f, 0.3f, PARAM_EXP, "s", 1, NULL, "decay time to the sustain level"),
    PARAM(SINE_P_SUSTAIN, "sustain", "amp.sustain", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL, "sustain level"),
    PARAM(SINE_P_RELEASE, "release", "amp.release", 0.0005f, 30.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "release time"),
    PARAM(SINE_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume in dB"),
};

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    sine_engine_t *e = eng;
    sine_voice_t *v = &e->v[vi];
    (void)note;
    if (hard) {
        osc_reset(&v->osc, 0.0f);
        adsr_reset(&v->env);
    }
    v->velgain = vel;
    adsr_gate(&v->env, 1);
}

static void voice_release(void *eng, int vi) { adsr_gate(&((sine_engine_t *)eng)->v[vi].env, 0); }

static float voice_level(const void *eng, int vi)
{
    const sine_voice_t *v = &((const sine_engine_t *)eng)->v[vi];
    return v->env.level * v->velgain;
}

static void voice_silence(void *eng, int vi) { adsr_reset(&((sine_engine_t *)eng)->v[vi].env); }

static const vpoly_ops_t sine_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void sine_init(void *st, float sample_rate)
{
    sine_engine_t *e = st;
    e->sr = sample_rate;
    for (int i = 0; i < SINE_P_COUNT; i++)
        e->p[i] = sine_params[i].def;
    smooth_init(&e->vol, 0.005f, sample_rate / (float)SYNTH_BLOCK, fm_db2lin(e->p[SINE_P_VOLUME]));
    e->vol_prev = e->vol.y;
    e->bend_semi = 0.0f;
    e->env_dirty = 1;
    e->nan_count = 0;
    vpoly_init(&e->poly, SINE_VOICES, sample_rate, &sine_poly_ops, e);
    for (int i = 0; i < SINE_VOICES; i++) {
        osc_init(&e->v[i].osc, sample_rate);
        osc_set_wave(&e->v[i].osc, OSC_SINE);
        adsr_init(&e->v[i].env, sample_rate);
        e->v[i].velgain = 0.0f;
    }
}

static vpoly_t *sine_poly(void *st) { return &((sine_engine_t *)st)->poly; }

static void sine_set_param(void *st, int id, float norm)
{
    sine_engine_t *e = st;
    if (id < 0 || id >= SINE_P_COUNT)
        return;
    e->p[id] = param_from_norm(&sine_params[id], norm);
    if (id != SINE_P_VOLUME)
        e->env_dirty = 1;
}

static void sine_pitch_bend(void *st, float bend) { ((sine_engine_t *)st)->bend_semi = 2.0f * bend; }

static DSP_FAST_CODE void sine_render(void *st, float *L, float *R)
{
    sine_engine_t *e = st;
    const int n = SYNTH_BLOCK;
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK];

    if (e->env_dirty) {
        for (int i = 0; i < SINE_VOICES; i++)
            adsr_set(&e->v[i].env, e->p[SINE_P_ATTACK], e->p[SINE_P_DECAY], e->p[SINE_P_SUSTAIN],
                     e->p[SINE_P_RELEASE]);
        e->env_dirty = 0;
    }
    const float vol0 = e->vol_prev;
    const float vol1 = smooth_next(&e->vol, fm_db2lin(e->p[SINE_P_VOLUME]));
    e->vol_prev = vol1;

    for (int vi = 0; vi < SINE_VOICES; vi++) {
        if (!vpoly_active(&e->poly, vi))
            continue;
        sine_voice_t *v = &e->v[vi];
        const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
        osc_set(&v->osc, fm_mtof((float)vpoly_note(&e->poly, vi) + e->bend_semi + vm[VM_PITCH]), 0.5f);
        osc_process(&v->osc, x, n);
        adsr_process(&v->env, env, n);
        for (int i = 0; i < n; i++)
            x[i] *= env[i];
        dsp_ramp_mul(x, SINE_VOICE_GAIN * v->velgain * vol0, SINE_VOICE_GAIN * v->velgain * vol1, n);
        voice_out(&e->poly, vi, x, n, VOICE_CENTER, VOICE_CENTER, L, R, &e->nan_count);
        vpoly_end_block(&e->poly, vi, adsr_active(&v->env));
    }
}

const engine_ops_t sine_engine = {
    .name = "sine",
    .description = "reference polyphonic sine with one ADSR (8 voices)",
    .params = sine_params,
    .param_count = SINE_P_COUNT,
    .voices = SINE_VOICES,
    .init = sine_init,
    .poly = sine_poly,
    .set_param = sine_set_param,
    .pitch_bend = sine_pitch_bend,
    .mod_wheel = NULL,
    .render = sine_render,
};
