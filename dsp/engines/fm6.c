/* fm6.c — 6-operator FM engine (see fm6.h).
 * Per sample and voice, operators are computed from 6 down to 1 (in every algorithm a modulator has a
 * higher number than what it modulates): out_k = sin(2 pi (phase_k + mod_k)) * amp_k, mod_k = MOD_INDEX x
 * (sum of its modulators' outputs) + feedback. Envelopes, levels and frequencies are computed per block in
 * the instrument's dB-like domain (0.75 dB per level step) and interpolated per sample. */
#include "fm6.h"
#include "../core/fastmath.h"

#define FM6_GAIN 0.125f
#define MOD_INDEX 2.0f   /* phase deviation (cycles) of a modulator at full level: ~12.6 rad */
#define DB_PER_STEP 0.75f
#define SILENT_DB -96.0f

const fm6_algorithm_t fm6_algorithms[32] = {
    {{0x02, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x05, 5, 5}, /*  1 */
    {{0x02, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x05, 1, 1}, /*  2 */
    {{0x02, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x09, 5, 5}, /*  3 */
    {{0x02, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x09, 3, 5}, /*  4 */
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 5, 5}, /*  5 */
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 4, 5}, /*  6 */
    {{0x02, 0x00, 0x18, 0x00, 0x20, 0x00}, 0x05, 5, 5}, /*  7 */
    {{0x02, 0x00, 0x18, 0x00, 0x20, 0x00}, 0x05, 3, 3}, /*  8 */
    {{0x02, 0x00, 0x18, 0x00, 0x20, 0x00}, 0x05, 1, 1}, /*  9 */
    {{0x02, 0x04, 0x00, 0x30, 0x00, 0x00}, 0x09, 2, 2}, /* 10 */
    {{0x02, 0x04, 0x00, 0x30, 0x00, 0x00}, 0x09, 5, 5}, /* 11 */
    {{0x02, 0x00, 0x38, 0x00, 0x00, 0x00}, 0x05, 1, 1}, /* 12 */
    {{0x02, 0x00, 0x38, 0x00, 0x00, 0x00}, 0x05, 5, 5}, /* 13 */
    {{0x02, 0x00, 0x08, 0x30, 0x00, 0x00}, 0x05, 5, 5}, /* 14 */
    {{0x02, 0x00, 0x08, 0x30, 0x00, 0x00}, 0x05, 1, 1}, /* 15 */
    {{0x16, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x01, 5, 5}, /* 16 */
    {{0x16, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x01, 1, 1}, /* 17 */
    {{0x0E, 0x00, 0x00, 0x10, 0x20, 0x00}, 0x01, 2, 2}, /* 18 */
    {{0x02, 0x04, 0x00, 0x20, 0x20, 0x00}, 0x19, 5, 5}, /* 19 */
    {{0x04, 0x04, 0x00, 0x30, 0x00, 0x00}, 0x0B, 2, 2}, /* 20 */
    {{0x04, 0x04, 0x00, 0x20, 0x20, 0x00}, 0x1B, 2, 2}, /* 21 */
    {{0x02, 0x00, 0x20, 0x20, 0x20, 0x00}, 0x1D, 5, 5}, /* 22 */
    {{0x00, 0x04, 0x00, 0x20, 0x20, 0x00}, 0x1B, 5, 5}, /* 23 */
    {{0x00, 0x00, 0x20, 0x20, 0x20, 0x00}, 0x1F, 5, 5}, /* 24 */
    {{0x00, 0x00, 0x00, 0x20, 0x20, 0x00}, 0x1F, 5, 5}, /* 25 */
    {{0x00, 0x04, 0x00, 0x30, 0x00, 0x00}, 0x0B, 5, 5}, /* 26 */
    {{0x00, 0x04, 0x00, 0x30, 0x00, 0x00}, 0x0B, 2, 2}, /* 27 */
    {{0x02, 0x00, 0x08, 0x10, 0x00, 0x00}, 0x25, 4, 4}, /* 28 */
    {{0x00, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x17, 5, 5}, /* 29 */
    {{0x00, 0x00, 0x08, 0x10, 0x00, 0x00}, 0x27, 4, 4}, /* 30 */
    {{0x00, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x1F, 5, 5}, /* 31 */
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0x3F, 5, 5}, /* 32 */
};

static const char *const mode_labels[] = {"ratio", "fixed", NULL};
static const char *const curve_labels[] = {"neg_lin", "neg_exp", "pos_exp", "pos_lin", NULL};
static const char *const lfo_labels[] = {"triangle", "saw_down", "saw_up", "square", "sine", "sample_hold", NULL};

#define OPP(N, K, name, role, mn, mx, df, curve, rt, labels, help) \
    PARAM(FM6_OP(N - 1, K), "op" #N "_" name, "fm.op" #N "." role, mn, mx, df, curve, "", rt, labels, help)
#define OP(N, LEVEL)                                                                                              \
    OPP(N, FM6_R1, "r1", "rate1", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope rate 1 (attack speed, 99 = instant)"), \
    OPP(N, FM6_R2, "r2", "rate2", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope rate 2 (first decay speed)"),          \
    OPP(N, FM6_R3, "r3", "rate3", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope rate 3 (second decay speed, to the sustain level)"), \
    OPP(N, FM6_R4, "r4", "rate4", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope rate 4 (release speed)"),              \
    OPP(N, FM6_L1, "l1", "level1", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope level 1 (attack peak)"),               \
    OPP(N, FM6_L2, "l2", "level2", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope level 2"),                             \
    OPP(N, FM6_L3, "l3", "level3", 0.0f, 99.0f, 99.0f, PARAM_INT, 1, NULL, "envelope level 3 (sustain)"),                   \
    OPP(N, FM6_L4, "l4", "level4", 0.0f, 99.0f, 0.0f, PARAM_INT, 1, NULL, "envelope level 4 (start and end level)"),        \
    OPP(N, FM6_LEVEL, "level", "output", 0.0f, 99.0f, LEVEL, PARAM_INT, 1, NULL,                                            \
        "output level (0.75 dB per step): carrier = loudness, modulator = brightness / modulation index"),                \
    OPP(N, FM6_MODE, "mode", "mode", 0.0f, 1.0f, 0.0f, PARAM_INT, 0, mode_labels,                                          \
        "ratio = frequency follows the note x coarse x fine; fixed = constant frequency (1/10/100/1000 Hz x fine)"),       \
    OPP(N, FM6_COARSE, "coarse", "coarse", 0.0f, 31.0f, 1.0f, PARAM_INT, 0, NULL,                                         \
        "frequency ratio (0 = 0.5, 1 = unison, 2 = octave, 3 = octave+fifth...) or fixed range"),                         \
    OPP(N, FM6_FINE, "fine", "fine", 0.0f, 99.0f, 0.0f, PARAM_INT, 1, NULL, "fine frequency: ratio x (1 + fine/100)"),      \
    OPP(N, FM6_DETUNE, "detune", "detune", 0.0f, 14.0f, 7.0f, PARAM_INT, 1, NULL, "detune (7 = none, about 1 cent per step): beating"), \
    OPP(N, FM6_VEL_SENS, "velocity", "velocity", 0.0f, 7.0f, 0.0f, PARAM_INT, 1, NULL,                                    \
        "velocity sensitivity of this operator (modulators: harder = brighter)"),                                        \
    OPP(N, FM6_RATE_SCALE, "rate_scale", "rate_scaling", 0.0f, 7.0f, 0.0f, PARAM_INT, 1, NULL, "higher notes have faster envelopes"), \
    OPP(N, FM6_BREAKPOINT, "breakpoint", "breakpoint", 0.0f, 99.0f, 39.0f, PARAM_INT, 1, NULL,                            \
        "keyboard level scaling break point (0 = A-1, 39 = C3 i.e. middle C, 99 = C8)"),                                 \
    OPP(N, FM6_LDEPTH, "left_depth", "left_depth", 0.0f, 99.0f, 0.0f, PARAM_INT, 1, NULL, "level scaling depth below the break point"), \
    OPP(N, FM6_RDEPTH, "right_depth", "right_depth", 0.0f, 99.0f, 0.0f, PARAM_INT, 1, NULL, "level scaling depth above the break point"), \
    OPP(N, FM6_LCURVE, "left_curve", "left_curve", 0.0f, 3.0f, 0.0f, PARAM_INT, 0, curve_labels, "curve below the break point"),  \
    OPP(N, FM6_RCURVE, "right_curve", "right_curve", 0.0f, 3.0f, 0.0f, PARAM_INT, 0, curve_labels, "curve above the break point"), \
    OPP(N, FM6_AMP_SENS, "amp_mod", "amp_mod", 0.0f, 3.0f, 0.0f, PARAM_INT, 1, NULL, "LFO amplitude modulation sensitivity")

const param_desc_t fm6_params[FM6_P_COUNT] = {
    OP(1, 99.0f), OP(2, 0.0f), OP(3, 0.0f), OP(4, 0.0f), OP(5, 0.0f), OP(6, 0.0f),
    PARAM(FM6_P_ALGORITHM, "algorithm", "fm.algorithm", 1.0f, 32.0f, 1.0f, PARAM_INT, "", 0, NULL,
          "which operators modulate which (the instrument's chart): 1-4 stacks, 5-6 three pairs (electric piano), "
          "16-18 one carrier with many modulators (brass, complex), 19-31 mostly carriers (organs), 32 = additive"),
    PARAM(FM6_P_FEEDBACK, "feedback", "fm.feedback", 0.0f, 7.0f, 0.0f, PARAM_INT, "", 1, NULL,
          "self modulation of the feedback operator: 3-5 saw-like brightness, 6-7 noisy"),
    PARAM(FM6_P_OSC_SYNC, "osc_sync", "fm.osc_sync", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, param_onoff_labels,
          "on = operator phases restart on each note (consistent attacks); off = free running"),
    PARAM(FM6_P_TRANSPOSE, "transpose", "voice.transpose", 0.0f, 48.0f, 24.0f, PARAM_INT, "", 0, NULL,
          "transposition in semitones, 24 = none (the instrument's C3)"),
    PARAM(FM6_P_PR1, "pitch_r1", "pitch_env.rate1", 0.0f, 99.0f, 99.0f, PARAM_INT, "", 1, NULL, "pitch envelope rate 1"),
    PARAM(FM6_P_PR2, "pitch_r2", "pitch_env.rate2", 0.0f, 99.0f, 99.0f, PARAM_INT, "", 1, NULL, "pitch envelope rate 2"),
    PARAM(FM6_P_PR3, "pitch_r3", "pitch_env.rate3", 0.0f, 99.0f, 99.0f, PARAM_INT, "", 1, NULL, "pitch envelope rate 3"),
    PARAM(FM6_P_PR4, "pitch_r4", "pitch_env.rate4", 0.0f, 99.0f, 99.0f, PARAM_INT, "", 1, NULL, "pitch envelope rate 4"),
    PARAM(FM6_P_PL1, "pitch_l1", "pitch_env.level1", 0.0f, 99.0f, 50.0f, PARAM_INT, "", 1, NULL,
          "pitch envelope level 1 (50 = no change, 0/99 = -/+ 4 octaves)"),
    PARAM(FM6_P_PL2, "pitch_l2", "pitch_env.level2", 0.0f, 99.0f, 50.0f, PARAM_INT, "", 1, NULL, "pitch envelope level 2"),
    PARAM(FM6_P_PL3, "pitch_l3", "pitch_env.level3", 0.0f, 99.0f, 50.0f, PARAM_INT, "", 1, NULL, "pitch envelope level 3 (sustain)"),
    PARAM(FM6_P_PL4, "pitch_l4", "pitch_env.level4", 0.0f, 99.0f, 50.0f, PARAM_INT, "", 1, NULL, "pitch envelope level 4 (start/end)"),
    PARAM(FM6_P_LFO_SPEED, "lfo_speed", "lfo.rate", 0.0f, 99.0f, 35.0f, PARAM_INT, "", 1, NULL, "LFO speed (0 = 0.06 Hz .. 99 = ~47 Hz)"),
    PARAM(FM6_P_LFO_DELAY, "lfo_delay", "lfo.delay", 0.0f, 99.0f, 0.0f, PARAM_INT, "", 1, NULL, "LFO fade-in delay after each note"),
    PARAM(FM6_P_LFO_PMD, "lfo_pmd", "lfo.pitch_depth", 0.0f, 99.0f, 0.0f, PARAM_INT, "", 1, NULL,
          "LFO pitch modulation depth (the mod wheel adds to it)"),
    PARAM(FM6_P_LFO_AMD, "lfo_amd", "lfo.amp_depth", 0.0f, 99.0f, 0.0f, PARAM_INT, "", 1, NULL, "LFO amplitude modulation depth"),
    PARAM(FM6_P_LFO_SYNC, "lfo_sync", "lfo.sync", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, param_onoff_labels, "LFO restarts on each note"),
    PARAM(FM6_P_LFO_WAVE, "lfo_wave", "lfo.waveform", 0.0f, 5.0f, 0.0f, PARAM_INT, "", 0, lfo_labels, "LFO shape"),
    PARAM(FM6_P_PMS, "pitch_mod_sens", "lfo.pitch_sensitivity", 0.0f, 7.0f, 3.0f, PARAM_INT, "", 1, NULL,
          "pitch modulation sensitivity (scales LFO pitch depth: 3 = vibrato, 7 = +/- an octave)"),
    PARAM(FM6_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(FM6_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 12.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
    PARAM(FM6_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 0, NULL,
          "random stereo position per note (the original was mono)"),
};

/* ---------------- tables and conversions ---------------- */

static float level_db(float l) { return l <= 0.0f ? SILENT_DB : (l - 99.0f) * DB_PER_STEP; }

/* Keyboard level scaling (dB) for a note, given break point, depths and curves. */
static float level_scaling_db(const float *op, int note)
{
    const int bp = (int)op[FM6_BREAKPOINT] + 21;
    if (note == bp)
        return 0.0f;
    const int right = note > bp;
    const float depth = (right ? op[FM6_RDEPTH] : op[FM6_LDEPTH]) * (1.0f / 99.0f);
    const int curve = (int)(right ? op[FM6_RCURVE] : op[FM6_LCURVE]);
    const float oct = (float)(right ? note - bp : bp - note) * (1.0f / 12.0f);
    const float shape = (curve == 0 || curve == 3) ? 9.0f * oct : 4.5f * (fm_exp2(oct) - 1.0f);
    const float db = depth * shape * (curve <= 1 ? -1.0f : 1.0f);
    return dsp_clampf(db, -60.0f, 30.0f);
}

/* Operator frequency (Hz) for a pitch in semitones. */
static float op_freq(const float *op, float pitch)
{
    const float detune = fm_exp2((op[FM6_DETUNE] - 7.0f) * (1.0f / 1200.0f));
    if ((int)op[FM6_MODE] == 1) { /* fixed */
        static const float range[4] = {1.0f, 10.0f, 100.0f, 1000.0f};
        return range[(int)op[FM6_COARSE] & 3] * fm_exp2(op[FM6_FINE] * 0.0332193f) * detune; /* x 10^(fine/100) */
    }
    const float coarse = op[FM6_COARSE] < 0.5f ? 0.5f : op[FM6_COARSE];
    return fm_mtof(pitch) * coarse * (1.0f + op[FM6_FINE] * 0.01f) * detune;
}

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    fm6_t *e = eng;
    fm6_voice_t *v = &e->v[vi];
    const float *p = e->p;
    v->note = note;
    v->vel = dsp_clampf(vel, 0.0f, 1.0f);
    v->released = 0;
    /* key sync restarts the phases only when the voice starts from silence: resetting a sounding voice
     * (same note struck again) would click */
    if (hard) {
        for (int k = 0; k < FM6_OPS; k++)
            fm_op_reset(&v->op[k], (int)p[FM6_P_OSC_SYNC] ? 0.0f : dsp_rand_unipolar(&e->rng));
        v->fb1 = v->fb2 = 0.0f;
    }
    for (int k = 0; k < FM6_OPS; k++) {
        if (hard)
            v->eg[k].level = level_db(p[FM6_OP(k, FM6_L4)]);
        env_rl_gate(&v->eg[k], 1);
    }
    if (hard) {
        v->peg.level = (p[FM6_P_PL4] - 50.0f) * (48.0f / 49.0f);
        const float pos = p[FM6_P_SPREAD] * dsp_rand_bipolar(&e->rng);
        voice_pan(pos, &v->pan_l, &v->pan_r);
        v->gain_prev = FM6_GAIN * fm_db2lin(p[FM6_P_VOLUME]);
    }
    env_rl_gate(&v->peg, 1);
    /* LFO delay: 0..99 -> 0..~5 s of silence, then the LFO fades in over half that time (no pitch step) */
    const float d = p[FM6_P_LFO_DELAY] * (1.0f / 99.0f);
    v->lfo_delay_left = 5.0f * d * d;
    v->lfo_fade = v->lfo_delay_left > 0.0f ? 0.0f : 1.0f;
    v->lfo_fade_rate = 1.0f / dsp_maxf(0.5f * v->lfo_delay_left, 0.02f);
    if ((int)p[FM6_P_LFO_SYNC])
        e->lfo_phase = 0.0f;
}

static void voice_release(void *eng, int vi)
{
    fm6_voice_t *v = &((fm6_t *)eng)->v[vi];
    v->released = 1;
    for (int k = 0; k < FM6_OPS; k++)
        env_rl_gate(&v->eg[k], 0);
    env_rl_gate(&v->peg, 0);
}

static float voice_level(const void *eng, int vi)
{
    const fm6_t *e = eng;
    const fm6_voice_t *v = &e->v[vi];
    const uint8_t car = fm6_algorithms[dsp_clampi((int)e->p[FM6_P_ALGORITHM], 1, 32) - 1].carriers;
    float m = 0.0f;
    for (int k = 0; k < FM6_OPS; k++)
        if (car & (1u << k))
            m = dsp_maxf(m, v->op[k].amp_end);
    return m;
}

static void voice_silence(void *eng, int vi)
{
    fm6_voice_t *v = &((fm6_t *)eng)->v[vi];
    for (int k = 0; k < FM6_OPS; k++) {
        v->eg[k].stage = ENV_RL_IDLE;
        v->eg[k].level = SILENT_DB;
        v->op[k].amp_end = 0.0f;
    }
}

static const vpoly_ops_t fm6_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void fm6_init(void *st, float sr)
{
    fm6_t *e = st;
    e->sr = sr;
    for (int i = 0; i < FM6_P_COUNT; i++)
        e->p[i] = fm6_params[i].def;
    e->lfo_phase = 0.0f;
    e->lfo_sh = 0.0f;
    e->bend = e->modwheel = 0.0f;
    e->rng = dsp_seed(0xD7u);
    e->nan_count = 0;
    for (int i = 0; i < FM6_VOICES; i++) {
        voice_silence(e, i);
        e->v[i].peg.level = 0.0f;
        e->v[i].peg.stage = ENV_RL_IDLE;
        e->v[i].pan_l = e->v[i].pan_r = 0.70710678f;
        e->v[i].gain_prev = 0.0f;
        e->v[i].note = 60;
    }
    vpoly_init(&e->poly, FM6_VOICES, sr, &fm6_poly_ops, e);
}

static vpoly_t *fm6_poly(void *st) { return &((fm6_t *)st)->poly; }

static void fm6_set_param(void *st, int id, float norm)
{
    fm6_t *e = st;
    if (id >= 0 && id < FM6_P_COUNT)
        e->p[id] = param_from_norm(&fm6_params[id], norm);
}

static void fm6_pitch_bend(void *st, float bend) { ((fm6_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }
static void fm6_mod_wheel(void *st, float v) { ((fm6_t *)st)->modwheel = dsp_clampf(v, 0.0f, 1.0f); }

static DSP_FAST_CODE void voice_render(fm6_t *e, int vi, float lfo, float lfo_uni, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    fm6_voice_t *v = &e->v[vi];
    const float dt = (float)n / e->sr;
    const fm6_algorithm_t *alg = &fm6_algorithms[dsp_clampi((int)p[FM6_P_ALGORITHM], 1, 32) - 1];

    /* pitch envelope (semitones) and LFO (fades in after its delay) */
    const float prates[4] = {p[FM6_P_PR1], p[FM6_P_PR2], p[FM6_P_PR3], p[FM6_P_PR4]};
    const float plevels[4] = {(p[FM6_P_PL1] - 50.0f) * (48.0f / 49.0f), (p[FM6_P_PL2] - 50.0f) * (48.0f / 49.0f),
                              (p[FM6_P_PL3] - 50.0f) * (48.0f / 49.0f), (p[FM6_P_PL4] - 50.0f) * (48.0f / 49.0f)};
    env_rl_step(&v->peg, prates, plevels, 0.0f, 96.0f, dt);
    if (v->lfo_delay_left > 0.0f)
        v->lfo_delay_left -= dt;
    else if (v->lfo_fade < 1.0f)
        v->lfo_fade = dsp_minf(1.0f, v->lfo_fade + v->lfo_fade_rate * dt);
    const float lfo_fade = v->lfo_fade;
    static const float pms_st[8] = {0.0f, 0.1f, 0.2f, 0.35f, 0.6f, 1.0f, 2.0f, 12.0f};
    const float pmd = dsp_minf(1.0f, p[FM6_P_LFO_PMD] * (1.0f / 99.0f) + e->modwheel);
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float pitch = (float)v->note + (p[FM6_P_TRANSPOSE] - 24.0f) + e->bend * p[FM6_P_BEND_RANGE] + v->peg.level +
                        lfo * lfo_fade * pmd * pms_st[dsp_clampi((int)p[FM6_P_PMS], 0, 7)] + vm[VM_PITCH];

    /* per operator: envelope (dB), level, scaling, velocity, LFO amp mod -> target amplitude; frequency */
    int sounding = 0;
    static const float ams_db[4] = {0.0f, 6.0f, 18.0f, 48.0f};
    const float inv_n = 1.0f / (float)n;
    for (int k = 0; k < FM6_OPS; k++) {
        const float *op = &p[FM6_OP(k, 0)];
        const float rates[4] = {op[FM6_R1], op[FM6_R2], op[FM6_R3], op[FM6_R4]};
        const float targets[4] = {level_db(op[FM6_L1]), level_db(op[FM6_L2]), level_db(op[FM6_L3]), level_db(op[FM6_L4])};
        const float rate_add = op[FM6_RATE_SCALE] * (float)dsp_clampi(v->note - 21, 0, 88) * (6.0f / 88.0f);
        env_rl_step(&v->eg[k], rates, targets, rate_add, 96.0f, dt);
        const float db = v->eg[k].level + level_db(op[FM6_LEVEL]) + level_scaling_db(op, v->note) -
                         op[FM6_VEL_SENS] * (1.0f / 7.0f) * (1.0f - v->vel) * 30.0f -
                         lfo_uni * lfo_fade * p[FM6_P_LFO_AMD] * (1.0f / 99.0f) * ams_db[dsp_clampi((int)op[FM6_AMP_SENS], 0, 3)] +
                         ((alg->carriers & (1u << k)) ? 0.0f : 6.0f * vm[VM_BRIGHT]); /* brightness: modulator index */
        const float a1 = db <= -90.0f ? 0.0f : fm_exp2(db * (1.0f / 6.0206f));
        fm_op_block(&v->op[k], dsp_minf(op_freq(op, pitch) / e->sr, 0.45f), a1, inv_n);
        if ((alg->carriers & (1u << k)) && (v->eg[k].stage < ENV_RL_IDLE || a1 > 0.0f))
            sounding = 1;
    }

    /* audio: operators 6 -> 1 */
    static const float fb_scale[8] = {0.0f, 0.0078f, 0.0156f, 0.0313f, 0.0625f, 0.125f, 0.25f, 0.5f};
    const float fb = fb_scale[dsp_clampi((int)p[FM6_P_FEEDBACK], 0, 7)];
    float carriers = 0.0f;
    for (int k = 0; k < FM6_OPS; k++)
        carriers += (alg->carriers >> k) & 1u;
    /* carriers add mostly incoherently: 1/sqrt(n) keeps the loudness of 1..6 carriers comparable */
    static const float inv_sqrt[7] = {1.0f, 1.0f, 0.70710678f, 0.57735027f, 0.5f, 0.44721360f, 0.40824829f};
    const float out_gain = inv_sqrt[(int)carriers];
    float x[SYNTH_BLOCK];
    float fb1 = v->fb1, fb2 = v->fb2;
    for (int i = 0; i < n; i++) {
        float out[FM6_OPS];
        float y = 0.0f;
        for (int k = FM6_OPS - 1; k >= 0; k--) {
            float mod = 0.0f;
            for (uint8_t m = alg->mod[k], j = 0; m; m >>= 1, j++)
                if (m & 1u)
                    mod += out[j];
            mod *= MOD_INDEX;
            if (k == alg->fb_dst)
                mod += fb * 0.5f * (fb1 + fb2); /* averaged over 2 samples, max +/- 0.5 cycle */
            out[k] = fm_op_tick(&v->op[k], mod);
            if (k == alg->fb_src) {
                fb2 = fb1;
                fb1 = out[k];
            }
            if (alg->carriers & (1u << k))
                y += out[k];
        }
        x[i] = y * out_gain;
    }
    v->fb1 = fb1, v->fb2 = fb2;

    const float g1 = FM6_GAIN * fm_db2lin(p[FM6_P_VOLUME]);
    dsp_ramp_mul(x, v->gain_prev, g1, n);
    v->gain_prev = g1;
    voice_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, sounding);
}

static DSP_FAST_CODE void fm6_render(void *st, float *L, float *R)
{
    fm6_t *e = st;
    const float *p = e->p;
    /* LFO: speed 0..99 -> 0.06..47 Hz, 6 shapes; bipolar value for pitch, unipolar for amplitude */
    const float hz = 0.062f * fm_exp2(p[FM6_P_LFO_SPEED] * 0.0968f);
    e->lfo_phase += hz * (float)SYNTH_BLOCK / e->sr;
    if (e->lfo_phase >= 1.0f) {
        e->lfo_phase -= (float)(int)e->lfo_phase;
        e->lfo_sh = dsp_rand_bipolar(&e->rng);
    }
    const float ph = e->lfo_phase;
    float lfo;
    switch ((int)p[FM6_P_LFO_WAVE]) {
    case 1: lfo = 1.0f - 2.0f * ph; break;            /* saw down */
    case 2: lfo = 2.0f * ph - 1.0f; break;            /* saw up */
    case 3: lfo = ph < 0.5f ? 1.0f : -1.0f; break;    /* square */
    case 4: lfo = fm_sin(ph); break;                  /* sine */
    case 5: lfo = e->lfo_sh; break;                   /* sample and hold */
    default: lfo = 1.0f - 4.0f * dsp_absf(ph - 0.5f); /* triangle */
    }
    const float lfo_uni = 0.5f * (lfo + 1.0f);
    for (int vi = 0; vi < FM6_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, lfo, lfo_uni, L, R);
}

const engine_ops_t fm6_engine = {
    .name = "fm6",
    .description = "6-operator FM (1983 digital FM spirit): 32 algorithms, rate/level envelopes, ratio/fixed "
                   "frequencies, keyboard level/rate scaling, velocity, feedback, pitch envelope, LFO; imports the "
                   "instrument's documented sysex voices (16 voices)",
    .params = fm6_params,
    .param_count = FM6_P_COUNT,
    .voices = FM6_VOICES,
    .init = fm6_init,
    .poly = fm6_poly,
    .set_param = fm6_set_param,
    .pitch_bend = fm6_pitch_bend,
    .mod_wheel = fm6_mod_wheel,
    .render = fm6_render,
};
