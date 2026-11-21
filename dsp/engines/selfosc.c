/* selfosc.c — self-oscillating engine (see selfosc.h). The natural frequency is the note's, detuned, divided by the
 * measured pitch ratio of the free cycle at this drive: the oscillator plays in tune at any drive. */
#include "selfosc.h"
#include "../core/fastmath.h"

#define SO_GAIN 0.12f

static const char *const filter_labels[] = {"off", "lp", "bp", "hp", "notch", "formant", "scream", "comb", "comb_neg", NULL};

const param_desc_t selfosc_params[SO_P_COUNT] = {
    PARAM(SO_P_DRIVE, "drive", "selfosc.drive", 0.0f, 5.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "nonlinearity: 0 = pure sine, 1 = round, 2-3 = reedy relaxation, 5 = square / pulse-like"),
    PARAM(SO_P_STIFFNESS, "stiffness", "selfosc.stiffness", 0.0f, 2.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "Duffing term: the pitch follows the amplitude (> 0 sharper and brighter when loud); with low entrain it "
          "bends the note"),
    PARAM(SO_P_ENTRAIN, "entrain", "selfosc.entrain", 0.0f, 2.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "how hard the note forces the oscillator: high = locked on the note, low + detune = it beats against the "
          "note, slips, can turn quasi-periodic"),
    PARAM(SO_P_DETUNE, "detune", "selfosc.detune", -100.0f, 100.0f, 0.0f, PARAM_LIN, "cent", 1, NULL,
          "natural frequency against the note (the forcing stays on the note)"),
    PARAM(SO_P_VEL_DRIVE, "velocity_drive", "selfosc.velocity_drive", 0.0f, 3.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "drive added by a hard key (soft notes rounder, hard notes reedier)"),
    PARAM(SO_P_FILTER, "filter_type", "filter.type", 0.0f, 8.0f, 0.0f, PARAM_INT, "", 0, filter_labels,
          "off, 12 dB state-variable (lp/bp/hp/notch), formant (character = vowel), scream (character = drive), or a "
          "feedback comb: cutoff = its tuning (keytrack 1 = on the note), resonance = feedback, character = damping "
          "of the repeats; comb = all harmonics, comb_neg = hollow. Tuned off the note, a wavering sine flickers "
          "through its teeth and every slide rings"),
    PARAM(SO_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 20000.0f, 4000.0f, PARAM_EXP, "Hz", 1, NULL, "filter cutoff"),
    PARAM(SO_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL, "filter resonance"),
    PARAM(SO_P_CHARACTER, "filter_character", "filter.character", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "formant: vowel; scream: drive; comb: damping of the repeats (0 = bright, metallic)"),
    PARAM(SO_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "cutoff follows the keys"),
    PARAM(SO_P_ATTACK, "amp_attack", "amp.attack", 0.0005f, 30.0f, 0.01f, PARAM_EXP, "s", 1, NULL, "amp attack"),
    PARAM(SO_P_DECAY, "amp_decay", "amp.decay", 0.005f, 30.0f, 0.5f, PARAM_EXP, "s", 1, NULL, "amp decay"),
    PARAM(SO_P_SUSTAIN, "amp_sustain", "amp.sustain", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL, "amp sustain"),
    PARAM(SO_P_RELEASE, "amp_release", "amp.release", 0.005f, 30.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "amp release"),
    PARAM(SO_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(SO_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 0, NULL,
          "random stereo position per note"),
    PARAM(SO_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
};

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    selfosc_engine_t *e = eng;
    so_voice_t *v = &e->v[vi];
    v->note = note;
    v->vel = vel;
    if (hard) {
        selfosc_reset(&v->osc, 0.1f + 0.2f * dsp_rand_unipolar(&e->rng)); /* a small kick starts the cycle */
        vfilter_reset(&v->filter);
        comb_reset(&v->comb);
        adsr_reset(&v->env);
        voice_pan(e->p[SO_P_SPREAD] * dsp_rand_bipolar(&e->rng), &v->pan_l, &v->pan_r);
        v->gain_prev = 0.0f;
    }
    adsr_gate(&v->env, 1);
}

static void voice_release(void *eng, int vi) { adsr_gate(&((selfosc_engine_t *)eng)->v[vi].env, 0); }
static float voice_level(const void *eng, int vi) { return ((const selfosc_engine_t *)eng)->v[vi].env.level; }
static void voice_silence(void *eng, int vi) { adsr_reset(&((selfosc_engine_t *)eng)->v[vi].env); }

static const vpoly_ops_t so_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

static void so_init(void *st, float sr)
{
    selfosc_engine_t *e = st;
    e->sr = sr;
    for (int i = 0; i < SO_P_COUNT; i++)
        e->p[i] = selfosc_params[i].def;
    e->bend = 0.0f;
    e->env_dirty = 1;
    e->rng = dsp_seed(0x50C0u);
    e->nan_count = 0;
    for (int i = 0; i < SO_VOICES; i++) {
        so_voice_t *v = &e->v[i];
        selfosc_reset(&v->osc, 0.1f);
        vfilter_init(&v->filter, sr);
        comb_init(&v->comb, sr);
        v->comb_on = 0;
        adsr_init(&v->env, sr);
        v->vel = 1.0f;
        v->gain_prev = 0.0f;
        v->pan_l = v->pan_r = VOICE_CENTER;
        v->note = 60;
    }
    vpoly_init(&e->poly, SO_VOICES, sr, &so_poly_ops, e);
}

static vpoly_t *so_poly(void *st) { return &((selfosc_engine_t *)st)->poly; }

static void so_set_param(void *st, int id, float norm)
{
    selfosc_engine_t *e = st;
    if (id < 0 || id >= SO_P_COUNT)
        return;
    e->p[id] = param_from_norm(&selfosc_params[id], norm);
    if (id >= SO_P_ATTACK && id <= SO_P_RELEASE)
        e->env_dirty = 1;
}

static void so_pitch_bend(void *st, float bend) { ((selfosc_engine_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

static DSP_FAST_CODE void voice_render(selfosc_engine_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    so_voice_t *v = &e->v[vi];
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK];
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float pitch = (float)v->note + e->bend * p[SO_P_BEND_RANGE] + vm[VM_PITCH];
    const float f_note = fm_mtof(pitch);
    const float mu = dsp_clampf(p[SO_P_DRIVE] + p[SO_P_VEL_DRIVE] * (v->vel - 0.5f) + 2.0f * vm[VM_TIMBRE], 0.0f, SELFOSC_MU_MAX);
    const float f_nat = f_note * fm_exp2(p[SO_P_DETUNE] * (1.0f / 1200.0f)) / selfosc_ratio(&selfosc_table, mu);
    selfosc_process(&v->osc, e->sr, f_nat, f_note, mu, p[SO_P_STIFFNESS], p[SO_P_ENTRAIN], x, n);

    const int ftype = (int)p[SO_P_FILTER];
    if (ftype > 0) {
        const float oct = fm_log2(p[SO_P_CUTOFF]) + p[SO_P_KEYTRACK] * (pitch - 60.0f) * (1.0f / 12.0f) + vm[VM_BRIGHT];
        const float reso = p[SO_P_RESO];
        const float cut = fm_exp2(dsp_clampf(oct, 4.3f, 14.3f));
        if (ftype >= 7) { /* comb, comb_neg */
            if (!v->comb_on)
                comb_reset(&v->comb);
            comb_set(&v->comb, cut, (ftype == 8 ? -0.98f : 0.98f) * reso, p[SO_P_CHARACTER]);
            comb_process(&v->comb, x, x, n);
        } else {
            vfilter_set(&v->filter, ftype - 1, cut, 0.5f * fm_exp2(5.3f * reso), reso, p[SO_P_CHARACTER]);
            vfilter_process(&v->filter, x, x, n);
        }
    }
    v->comb_on = ftype >= 7;
    adsr_process(&v->env, env, n);
    const float g1 = SO_GAIN * fm_db2lin(p[SO_P_VOLUME]) * (0.5f + 0.5f * v->vel);
    dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
    v->gain_prev = g1;
    voice_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, adsr_active(&v->env));
}

static DSP_FAST_CODE void so_render(void *st, float *L, float *R)
{
    selfosc_engine_t *e = st;
    if (e->env_dirty) {
        for (int i = 0; i < SO_VOICES; i++)
            adsr_set(&e->v[i].env, e->p[SO_P_ATTACK], e->p[SO_P_DECAY], e->p[SO_P_SUSTAIN], e->p[SO_P_RELEASE]);
        e->env_dirty = 0;
    }
    for (int vi = 0; vi < SO_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* signal chain, for UIs (engine.h) */
static const engine_stage_t so_stages[] = {
    {"osc", "Self-oscillator", "source", "drive stiffness entrain detune velocity_drive", "", NULL, NULL, 0.0f, 0.0f},
    {"filter", "Filter / comb", "filter", "filter_type cutoff resonance filter_character keytrack", "osc", NULL,
     "filter_type", 0.0f, 7.0f},
    {"amp", "Amp", "amp", "amp_ volume pan_spread", "filter", NULL, NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t selfosc_engine = {
    .name = "selfosc",
    .description = "self-oscillating nonlinear oscillators (Van der Pol / Duffing) forced by the note: drive morphs a "
                   "sine into a relaxation wave, stiffness bends the pitch with the amplitude, entrain locks or lets "
                   "it beat; formant / scream / state-variable filter or a resonant comb (8 voices)",
    .params = selfosc_params,
    .param_count = SO_P_COUNT,
    .voices = SO_VOICES,
    .init = so_init,
    .poly = so_poly,
    .set_param = so_set_param,
    .pitch_bend = so_pitch_bend,
    .render = so_render,
    .stages = so_stages,
    .stage_count = (int)(sizeof so_stages / sizeof so_stages[0]),
};
