/* wt.c — wavetable engine (see wt.h). Per voice and block: envelopes and LFOs -> modulation matrix ->
 * sub, osc B (FM/ring source: the sub), osc A (FM/ring source: osc B) -> mix -> filter -> amp -> pan. */
#include "wt.h"
#include "../osc/wt_factory.h"

#define WT_GAIN 0.12f /* about -18 dBFS per voice */

static const char *const warp_labels[] = {"off", "sync", "bend", "mirror", "fm", "ring", NULL};
static const char *const filter_labels[] = {"off", "lp12", "bp12", "hp12", "notch", "lp24", "comb", "comb_neg", "formant",
                                            "scream", NULL};
static const char *const lfo_labels[] = {"sine", "tri", "saw", "square", "sh", NULL};
static const char *const voice_mode_labels[] = {"poly", "mono", "legato", NULL};
static const char *const src_labels[] = {"none", "env2", "amp_env", "lfo1", "lfo2", "velocity", "note", "mod_wheel",
                                         "random", NULL};
static const char *const dst_labels[] = {"none", "pitch", "a_position", "a_warp", "a_level", "b_position", "b_warp",
                                         "b_level", "cutoff", "resonance", "sub", "noise", "detune", "pan", "amp", NULL};
/* full-scale effect of amount 1 on each destination, in that destination's unit */
static const float dst_scale[WT_DST_COUNT] = {0.0f, 24.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                              8.0f, 1.0f, 1.0f, 1.0f, 50.0f, 1.0f, 1.0f};

#define OSC(N, X, LEVEL, POS)                                                                                       \
    PARAM(WT_OSC(N - 1, WT_O_TABLE), X "_table", "osc" #N ".wavetable", 0.0f, (float)(WT_FACTORY_COUNT - 1), 0.0f,   \
          PARAM_INT, "", 0, wt_factory_names,                                                                      \
          "wavetable: basic (sine>tri>saw>square), pwm, sync, harmonics (additive), vowel (a-e-i-o-u), fold, "     \
          "growl (phase modulation), resonant, digital (random spectra)"),                                         \
    PARAM(WT_OSC(N - 1, WT_O_POSITION), X "_position", "osc" #N ".position", 0.0f, 1.0f, POS, PARAM_LIN, "", 1,   \
          NULL, "position in the table: morphs smoothly between its frames (modulate it for movement)"),          \
    PARAM(WT_OSC(N - 1, WT_O_WARP_MODE), X "_warp_mode", "osc" #N ".warp_mode", 0.0f, 5.0f, 0.0f, PARAM_INT, "",  \
          0, warp_labels,                                                                                           \
          "phase warp: sync (hard sync, brighter), bend (squeezes the cycle), mirror (forward then backward), "     \
          "fm (phase modulated by osc B; on osc B: by the sub), ring (multiplied by the same source)"),            \
    PARAM(WT_OSC(N - 1, WT_O_WARP), X "_warp", "osc" #N ".warp", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,       \
          "warp amount"),                                                                                           \
    PARAM(WT_OSC(N - 1, WT_O_LEVEL), X "_level", "osc" #N ".level", 0.0f, 1.0f, LEVEL, PARAM_LIN, "", 1, NULL,    \
          "oscillator level in the mix (0 still works as an FM/ring source)"),                                    \
    PARAM(WT_OSC(N - 1, WT_O_OCTAVE), X "_octave", "osc" #N ".octave", -3.0f, 3.0f, 0.0f, PARAM_INT, "oct", 0,    \
          NULL, "octave"),                                                                                          \
    PARAM(WT_OSC(N - 1, WT_O_SEMI), X "_semi", "osc" #N ".semitone", -12.0f, 12.0f, 0.0f, PARAM_INT, "st", 0,     \
          NULL, "semitones (7 = fifth)"),                                                                           \
    PARAM(WT_OSC(N - 1, WT_O_FINE), X "_fine", "osc" #N ".detune", -100.0f, 100.0f, 0.0f, PARAM_LIN, "cent", 1,   \
          NULL, "fine tune"),                                                                                       \
    PARAM(WT_OSC(N - 1, WT_O_UNISON), X "_unison", "osc" #N ".unison_voices", 1.0f, (float)WT_UNISON_MAX, 1.0f,   \
          PARAM_INT, "", 0, NULL, "stacked detuned copies (thickness; cost grows with the count)"),               \
    PARAM(WT_OSC(N - 1, WT_O_DETUNE), X "_detune", "osc" #N ".unison_detune", 0.0f, 100.0f, 15.0f, PARAM_LIN,     \
          "cent", 1, NULL, "unison spread: outermost copies at +/- this (10-25 = lush, 50+ = huge)")

#define SLOT(S, N)                                                                                                  \
    PARAM(WT_MOD(S, WT_M_SRC), "mod" #N "_source", "mod.slot" #N ".source", 0.0f, (float)(WT_SRC_COUNT - 1), 0.0f, \
          PARAM_INT, "", 0, src_labels, "modulation slot " #N ": source"),                                         \
    PARAM(WT_MOD(S, WT_M_DST), "mod" #N "_destination", "mod.slot" #N ".destination", 0.0f,                       \
          (float)(WT_DST_COUNT - 1), 0.0f, PARAM_INT, "", 0, dst_labels,                                           \
          "destination; full scale: pitch +/-24 st, cutoff +/-8 oct, detune +/-50 cents, others +/-1"),           \
    PARAM(WT_MOD(S, WT_M_AMOUNT), "mod" #N "_amount", "mod.slot" #N ".amount", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", \
          1, NULL, "amount (negative = inverted)"),                                                                 \
    PARAM(WT_MOD(S, WT_M_VIA), "mod" #N "_via", "mod.slot" #N ".via", 0.0f, (float)(WT_SRC_COUNT - 1), 0.0f,     \
          PARAM_INT, "", 0, src_labels, "optional second source scaling the amount (e.g. mod wheel)")

#define ENV(A, pre, role, a, d, s, r, what)                                                                        \
    PARAM(A, pre "_attack", role ".attack", 0.0005f, 30.0f, a, PARAM_EXP, "s", 1, NULL, what " attack"),          \
    PARAM(A + 1, pre "_decay", role ".decay", 0.005f, 30.0f, d, PARAM_EXP, "s", 1, NULL, what " decay"),          \
    PARAM(A + 2, pre "_sustain", role ".sustain", 0.0f, 1.0f, s, PARAM_LIN, "", 1, NULL, what " sustain"),        \
    PARAM(A + 3, pre "_release", role ".release", 0.005f, 30.0f, r, PARAM_EXP, "s", 1, NULL, what " release")

#define LFO(R, N, rate)                                                                                             \
    PARAM(R, "lfo" #N "_rate", "lfo" #N ".rate", 0.02f, 40.0f, rate, PARAM_EXP, "Hz", 1, NULL, "LFO " #N " rate"), \
    PARAM(R + 1, "lfo" #N "_wave", "lfo" #N ".waveform", 0.0f, 4.0f, 0.0f, PARAM_INT, "", 0, lfo_labels,          \
          "LFO " #N " shape (sh = random steps)"),                                                                  \
    PARAM(R + 2, "lfo" #N "_retrig", "lfo" #N ".retrigger", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0,                   \
          param_onoff_labels, "LFO " #N " restarts on each note (off = free running, voices drift apart)")

const param_desc_t wt_params[WT_P_COUNT] = {
    OSC(1, "a", 0.8f, 0.67f),
    OSC(2, "b", 0.0f, 0.0f),
    PARAM(WT_P_SUB, "sub", "sub.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "sine one octave below"),
    PARAM(WT_P_NOISE, "noise", "noise.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "white noise"),
    PARAM(WT_P_FILTER_TYPE, "filter_type", "filter.type", 0.0f, 9.0f, 1.0f, PARAM_INT, "", 0, filter_labels,
          "off, 12 dB state-variable (lp/bp/hp/notch), 24 dB ladder low-pass with drive, or a comb: cutoff = its "
          "tuning (keytrack 1 = on the note), resonance = feedback, drive = damping of the repeats; comb = all "
          "harmonics, comb_neg = hollow (odd harmonics an octave down). Move the cutoff slowly: the comb wanders. "
          "formant: vowels (cutoff 1 kHz = adult voice, drive = vowel a..u, resonance = sharpness); scream: screaming "
          "2-pole low-pass (drive = saturation)"),
    PARAM(WT_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 20000.0f, 8000.0f, PARAM_EXP, "Hz", 1, NULL, "filter cutoff"),
    PARAM(WT_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.1f, PARAM_LIN, "", 1, NULL, "resonance"),
    PARAM(WT_P_DRIVE, "drive", "filter.drive", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "input saturation of the lp24 ladder (2x oversampled)"),
    PARAM(WT_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "cutoff follows the keyboard"),
    ENV(WT_P_AMP_A, "amp", "amp", 0.003f, 0.3f, 0.8f, 0.3f, "amp envelope"),
    ENV(WT_P_ENV2_A, "env2", "env2", 0.003f, 0.5f, 0.0f, 0.5f, "modulation envelope (use it in the matrix)"),
    LFO(WT_P_LFO1_RATE, 1, 0.5f),
    LFO(WT_P_LFO2_RATE, 2, 5.0f),
    SLOT(0, 1), SLOT(1, 2), SLOT(2, 3), SLOT(3, 4), SLOT(4, 5), SLOT(5, 6),
    PARAM(WT_P_VOICE_MODE, "voice_mode", "voice.mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, voice_mode_labels,
          "poly; mono = last note, retriggers; legato = overlapping notes slide without retriggering"),
    PARAM(WT_P_GLIDE, "glide", "voice.glide", 0.0f, 2.0f, 0.0f, PARAM_LIN, "s", 1, NULL, "portamento time"),
    PARAM(WT_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "velocity -> loudness"),
    PARAM(WT_P_VOLUME, "volume", "output.volume", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(WT_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 0, NULL, "random stereo position per note"),
    PARAM(WT_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
    PARAM_OVERSAMPLE(WT_P_OVERSAMPLE, 1.0f, "oversampling of the nonlinear filters (lp24 ladder, scream): 1x lightest, "
                     "2x default, 4x cleanest (about twice the filter cost of 2x)"),
};

/* ---------------- voices ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    wt_t *e = eng;
    wt_voice_t *v = &e->v[vi];
    v->note = note;
    v->vel = vel;
    if (hard) {
        wt_osc_reset(&v->osc[0], &e->rng);
        wt_osc_reset(&v->osc[1], &e->rng);
        v->sub_phase = 0.0f;
        svf_reset(&v->svf);
        ladder_reset(&v->ladder);
        comb_reset(&v->comb);
        vfilter_reset(&v->vf);
        adsr_reset(&v->amp);
        adsr_reset(&v->env2);
        v->pitch = (e->p[WT_P_GLIDE] > 0.0f && e->has_last) ? e->last_note : (float)note;
        v->pan_pos = e->p[WT_P_SPREAD] * dsp_rand_bipolar(&e->rng);
        v->gain_prev = 0.0f;
        for (int k = 0; k < 4; k++)
            v->lvl_prev[k] = 0.0f;
    }
    v->rnd = dsp_rand_bipolar(&e->rng);
    if ((int)e->p[WT_P_LFO1_RETRIG])
        lfo_reset(&v->lfo[0], 0.0f);
    if ((int)e->p[WT_P_LFO2_RETRIG])
        lfo_reset(&v->lfo[1], 0.0f);
    adsr_gate(&v->amp, 1);
    adsr_gate(&v->env2, 1);
    e->last_note = (float)note;
    e->has_last = 1;
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    wt_t *e = eng;
    (void)vel;
    e->v[vi].note = note;
    if (e->p[WT_P_GLIDE] <= 0.0f)
        e->v[vi].pitch = (float)note;
    e->last_note = (float)note;
}

static void voice_release(void *eng, int vi)
{
    wt_voice_t *v = &((wt_t *)eng)->v[vi];
    adsr_gate(&v->amp, 0);
    adsr_gate(&v->env2, 0);
}

static float voice_level(const void *eng, int vi) { return ((const wt_t *)eng)->v[vi].amp.level; }

static void voice_silence(void *eng, int vi)
{
    wt_voice_t *v = &((wt_t *)eng)->v[vi];
    adsr_reset(&v->amp);
    adsr_reset(&v->env2);
}

static const vpoly_ops_t wt_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void update_slots(wt_t *e)
{
    for (int s = 0; s < WT_SLOTS; s++) {
        e->slots[s].src = (int)e->p[WT_MOD(s, WT_M_SRC)];
        e->slots[s].dst = (int)e->p[WT_MOD(s, WT_M_DST)];
        e->slots[s].via = (int)e->p[WT_MOD(s, WT_M_VIA)];
        e->slots[s].amount = e->p[WT_MOD(s, WT_M_AMOUNT)];
    }
}

static void wt_init(void *st, float sr)
{
    wt_t *e = st;
    e->sr = sr;
    for (int i = 0; i < WT_P_COUNT; i++)
        e->p[i] = wt_params[i].def;
    update_slots(e);
    e->bank = NULL;
    e->bank_count = 0;
    e->bend = e->modwheel = 0.0f;
    e->last_note = 60.0f;
    e->has_last = 0;
    e->env_dirty = 1;
    e->rng = dsp_seed(0x3A7Eu);
    e->nan_count = 0;
    for (int i = 0; i < WT_VOICES; i++) {
        wt_voice_t *v = &e->v[i];
        wt_osc_reset(&v->osc[0], &e->rng);
        wt_osc_reset(&v->osc[1], &e->rng);
        svf_init(&v->svf, sr);
        ladder_init(&v->ladder, sr);
        comb_init(&v->comb, sr);
        v->comb_on = 0;
        vfilter_init(&v->vf, sr);
        ladder_set_oversampling(&v->ladder, 2);
        adsr_init(&v->amp, sr);
        adsr_init(&v->env2, sr);
        lfo_init(&v->lfo[0], sr, 0x1F00u + (uint32_t)i);
        lfo_init(&v->lfo[1], sr, 0x2F00u + (uint32_t)i);
        lfo_reset(&v->lfo[1], dsp_rand_unipolar(&e->rng));
        v->pitch = 60.0f;
        v->note = 60;
        v->vel = 1.0f;
        v->rnd = 0.0f;
        v->pan_pos = 0.0f;
        v->gain_prev = 0.0f;
        v->sub_phase = 0.0f;
        for (int k = 0; k < 4; k++)
            v->lvl_prev[k] = 0.0f;
        v->rng = dsp_seed(0x4E00u + (uint32_t)i);
    }
    vpoly_init(&e->poly, WT_VOICES, sr, &wt_poly_ops, e);
}

static void wt_shared_build(float *mem, size_t floats) { wt_factory_build(mem, floats); }

static void wt_shared_attach(void *st, const float *mem, size_t floats)
{
    wt_t *e = st;
    e->bank = mem;
    e->bank_count = floats < WT_TABLE_FLOATS + WT_SCRATCH_FLOATS
                        ? 0
                        : dsp_clampi((int)((floats - WT_SCRATCH_FLOATS) / WT_TABLE_FLOATS), 0, WT_FACTORY_COUNT);
}

static vpoly_t *wt_poly(void *st) { return &((wt_t *)st)->poly; }

static void wt_set_param(void *st, int id, float norm)
{
    wt_t *e = st;
    if (id < 0 || id >= WT_P_COUNT)
        return;
    e->p[id] = param_from_norm(&wt_params[id], norm);
    if (id >= WT_P_MOD && id < WT_P_VOICE_MODE)
        update_slots(e);
    else if (id >= WT_P_AMP_A && id <= WT_P_ENV2_R)
        e->env_dirty = 1;
    else if (id == WT_P_VOICE_MODE)
        vpoly_set_mode(&e->poly, (vpoly_mode_t)(int)e->p[id]);
    else if (id == WT_P_OVERSAMPLE)
        for (int i = 0; i < WT_VOICES; i++) {
            ladder_set_oversampling(&e->v[i].ladder, 1 << (int)e->p[id]);
            vfilter_set_oversampling(&e->v[i].vf, 1 << (int)e->p[id]);
        }
}

static void wt_pitch_bend(void *st, float bend) { ((wt_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }
static void wt_mod_wheel(void *st, float v) { ((wt_t *)st)->modwheel = dsp_clampf(v, 0.0f, 1.0f); }

static wt_osc_set_t osc_settings(const float *p, int o, float pitch, const float *dst, int dpos, float timbre)
{
    wt_osc_set_t s;
    const float *q = &p[WT_OSC(o, 0)];
    s.freq_hz = fm_mtof(pitch + 12.0f * q[WT_O_OCTAVE] + q[WT_O_SEMI] + 0.01f * q[WT_O_FINE]);
    s.detune_cents = dsp_maxf(0.0f, q[WT_O_DETUNE] + dst[WT_DST_DETUNE]);
    s.position = q[WT_O_POSITION] + dst[dpos] + timbre;
    s.warp = q[WT_O_WARP] + dst[dpos + 1];
    s.level = 1.0f; /* mixed by the engine (the raw signal also feeds FM and ring) */
    s.unison = (int)q[WT_O_UNISON];
    s.warp_mode = (int)q[WT_O_WARP_MODE];
    return s;
}

static DSP_FAST_CODE void voice_render(wt_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    wt_voice_t *v = &e->v[vi];
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK], e2[SYNTH_BLOCK], sub[SYNTH_BLOCK], ob[SYNTH_BLOCK], oa[SYNTH_BLOCK];
    const float inv_sr = 1.0f / e->sr;

    /* control: envelopes, LFOs, matrix */
    adsr_process(&v->amp, env, n);
    adsr_process(&v->env2, e2, n);
    static const lfo_wave_t waves[5] = {LFO_SINE, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_SH};
    lfo_set(&v->lfo[0], p[WT_P_LFO1_RATE], waves[(int)p[WT_P_LFO1_WAVE]]);
    lfo_set(&v->lfo[1], p[WT_P_LFO2_RATE], waves[(int)p[WT_P_LFO2_WAVE]]);
    float src[WT_SRC_COUNT], dst[WT_DST_COUNT];
    src[WT_SRC_NONE] = 0.0f;
    src[WT_SRC_ENV2] = e2[n - 1];
    src[WT_SRC_AMP_ENV] = env[n - 1];
    src[WT_SRC_LFO1] = lfo_advance(&v->lfo[0], n);
    src[WT_SRC_LFO2] = lfo_advance(&v->lfo[1], n);
    src[WT_SRC_VELOCITY] = v->vel;
    src[WT_SRC_NOTE] = ((float)v->note - 60.0f) * (1.0f / 60.0f);
    src[WT_SRC_MODWHEEL] = e->modwheel;
    src[WT_SRC_RANDOM] = v->rnd;
    modmatrix_eval(e->slots, WT_SLOTS, src, WT_SRC_COUNT, dst, WT_DST_COUNT);
    for (int d = 1; d < WT_DST_COUNT; d++)
        dst[d] *= dst_scale[d];

    /* pitch */
    if (p[WT_P_GLIDE] > 0.0f)
        v->pitch += ((float)v->note - v->pitch) * voice_glide_coef(p[WT_P_GLIDE], n, e->sr);
    else
        v->pitch = (float)v->note;
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float pitch = v->pitch + e->bend * p[WT_P_BEND_RANGE] + dst[WT_DST_PITCH] + vm[VM_PITCH];

    /* mix levels for this block (ramped from the previous ones) */
    float lvl[4] = {p[WT_OSC(0, WT_O_LEVEL)] + dst[WT_DST_A_LEVEL], p[WT_OSC(1, WT_O_LEVEL)] + dst[WT_DST_B_LEVEL],
                    p[WT_P_SUB] + dst[WT_DST_SUB], p[WT_P_NOISE] + dst[WT_DST_NOISE]};
    for (int k = 0; k < 4; k++)
        lvl[k] = dsp_clampf(lvl[k], 0.0f, 1.0f);
    const int a_mode = (int)p[WT_OSC(0, WT_O_WARP_MODE)], b_mode = (int)p[WT_OSC(1, WT_O_WARP_MODE)];
    const int a_uses_b = a_mode == WT_WARP_FM || a_mode == WT_WARP_RING;
    const int b_uses_sub = b_mode == WT_WARP_FM || b_mode == WT_WARP_RING;
    const int need_b = lvl[1] > 0.0f || v->lvl_prev[1] > 0.0f || a_uses_b;
    const int need_sub = lvl[2] > 0.0f || v->lvl_prev[2] > 0.0f || (need_b && b_uses_sub);

    for (int i = 0; i < n; i++)
        x[i] = 0.0f;
    if (need_sub) {
        const float dt = fm_mtof(pitch - 12.0f) * inv_sr;
        float ph = v->sub_phase;
        for (int i = 0; i < n; i++) {
            ph += dt;
            if (ph >= 1.0f)
                ph -= 1.0f;
            sub[i] = fm_sin(ph);
        }
        v->sub_phase = ph;
        dsp_ramp_mac(x, sub, v->lvl_prev[2], lvl[2], n);
    }
    if (need_b) {
        const wt_osc_set_t s = osc_settings(p, 1, pitch, dst, WT_DST_B_POS, vm[VM_TIMBRE]);
        for (int i = 0; i < n; i++)
            ob[i] = 0.0f;
        wt_osc_process(&v->osc[1], wt_factory_table(e->bank, e->bank_count, (int)p[WT_OSC(1, WT_O_TABLE)]), &s, inv_sr,
                       sub, ob, n);
        dsp_ramp_mac(x, ob, v->lvl_prev[1], lvl[1], n);
    }
    if (lvl[0] > 0.0f || v->lvl_prev[0] > 0.0f) {
        const wt_osc_set_t s = osc_settings(p, 0, pitch, dst, WT_DST_A_POS, vm[VM_TIMBRE]);
        for (int i = 0; i < n; i++)
            oa[i] = 0.0f;
        wt_osc_process(&v->osc[0], wt_factory_table(e->bank, e->bank_count, (int)p[WT_OSC(0, WT_O_TABLE)]), &s, inv_sr,
                       ob, oa, n);
        dsp_ramp_mac(x, oa, v->lvl_prev[0], lvl[0], n);
    }
    if (lvl[3] > 0.0f || v->lvl_prev[3] > 0.0f) {
        const float d = (lvl[3] - v->lvl_prev[3]) / (float)n;
        float g = v->lvl_prev[3];
        for (int i = 0; i < n; i++) {
            g += d;
            x[i] += g * dsp_rand_bipolar(&v->rng);
        }
    }
    for (int k = 0; k < 4; k++)
        v->lvl_prev[k] = lvl[k];

    /* filter */
    const int ftype = (int)p[WT_P_FILTER_TYPE];
    const int combing = ftype == 6 || ftype == 7;
    if (ftype > 0) {
        const float oct = fm_log2(p[WT_P_CUTOFF]) + p[WT_P_KEYTRACK] * (pitch - 60.0f) * (1.0f / 12.0f) + dst[WT_DST_CUTOFF] +
                          vm[VM_BRIGHT];
        const float cut = fm_exp2(dsp_clampf(oct, 4.3f, 14.3f));
        const float reso = dsp_clampf(p[WT_P_RESO] + dst[WT_DST_RESO], 0.0f, 1.0f);
        if (ftype == 5) {
            ladder_set(&v->ladder, cut, reso, p[WT_P_DRIVE]);
            ladder_process(&v->ladder, x, x, n);
        } else if (ftype >= 8) {
            vfilter_set(&v->vf, ftype == 8 ? VF_FORMANT : VF_SCREAM, cut, 0.7f, reso, p[WT_P_DRIVE]);
            vfilter_process(&v->vf, x, x, n);
        } else if (ftype >= 6) {
            if (!v->comb_on)
                comb_reset(&v->comb);
            comb_set(&v->comb, cut, (ftype == 7 ? -0.98f : 0.98f) * reso, p[WT_P_DRIVE]);
            comb_process(&v->comb, x, x, n);
        } else {
            static const svf_mode_t modes[5] = {SVF_LP, SVF_LP, SVF_BP, SVF_HP, SVF_NOTCH};
            svf_set_mode(&v->svf, modes[ftype]);
            svf_set(&v->svf, cut, 0.5f * fm_exp2(5.3f * reso)); /* Q 0.5 .. 20 */
            svf_process(&v->svf, x, x, n);
        }
    }
    v->comb_on = combing;

    /* amp and output */
    const float velgain = 1.0f - p[WT_P_VELOCITY] * (1.0f - v->vel);
    const float g1 = WT_GAIN * velgain * fm_db2lin(p[WT_P_VOLUME]) * dsp_clampf(1.0f + dst[WT_DST_AMP], 0.0f, 2.0f);
    dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
    v->gain_prev = g1;
    float pan_l, pan_r;
    voice_pan(dsp_clampf(v->pan_pos + dst[WT_DST_PAN], -1.0f, 1.0f), &pan_l, &pan_r);
    voice_out(&e->poly, vi, x, n, pan_l, pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, adsr_active(&v->amp));
}

static DSP_FAST_CODE void wt_render(void *st, float *L, float *R)
{
    wt_t *e = st;
    if (!e->bank_count)
        return; /* no table memory: silent rather than reading nothing */
    if (e->env_dirty) {
        const float *p = e->p;
        for (int i = 0; i < WT_VOICES; i++) {
            adsr_set(&e->v[i].amp, p[WT_P_AMP_A], p[WT_P_AMP_D], p[WT_P_AMP_S], p[WT_P_AMP_R]);
            adsr_set(&e->v[i].env2, p[WT_P_ENV2_A], p[WT_P_ENV2_D], p[WT_P_ENV2_S], p[WT_P_ENV2_R]);
        }
        e->env_dirty = 0;
    }
    for (int vi = 0; vi < WT_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

const engine_ops_t wt_engine = {
    .name = "wt",
    .description = "wavetable synth (modern wavetable spirit): 2 wavetable oscillators with position morphing, "
                   "unison and warps (sync, bend, mirror, FM, ring), sub, noise, SVF or ladder filter, 2 envelopes, "
                   "2 LFOs, 6-slot modulation matrix, mono/legato/glide (8 voices)",
    .params = wt_params,
    .param_count = WT_P_COUNT,
    .voices = WT_VOICES,
    .init = wt_init,
    .poly = wt_poly,
    .set_param = wt_set_param,
    .pitch_bend = wt_pitch_bend,
    .mod_wheel = wt_mod_wheel,
    .render = wt_render,
    .shared_floats = WT_FACTORY_FLOATS,
    .shared_build = wt_shared_build,
    .shared_attach = wt_shared_attach,
};
