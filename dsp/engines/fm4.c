/* fm4.c — 4-operator FM engine on a modulation matrix (see fm4.h).
 * Per sample and voice the operators are computed from 4 down to 1: a link from a higher-numbered operator to a
 * lower one uses its output of this sample (as in every classic algorithm, so stacks have no delay); a link the
 * other way, and the feedback on the diagonal, use the source's output averaged over its last two samples (one
 * sample late, which keeps loops stable and tames their noise). The matrix, output levels and operator levels are
 * computed per block and ramped per sample; the envelopes run per sample. */
#include "fm4.h"
#include "../core/fastmath.h"

#define FM4_GAIN 0.125f /* one carrier at full level: -18 dBFS */

typedef struct {
    float m, dm;      /* phase deviation per unit of the source, ramped per sample */
    int s;            /* source operator */
    const float *src; /* its output this sample (a higher operator) or its late one */
} fm4_link_t;

static const char *const mode_labels[] = {"ratio", "fixed", NULL};
static const char *const filter_labels[] = {"off", "lp", "bp", "hp", "notch", "formant", "scream", NULL};

#define OPP(N, K, name, role, mn, mx, df, curve, unit, rt, labels, help) \
    PARAM(FM4_OP(N - 1, K), "op" #N "_" name, "fm.op" #N "." role, mn, mx, df, curve, unit, rt, labels, help)
#define OP(N, VEL)                                                                                                 \
    OPP(N, FM4_COARSE, "coarse", "coarse", 0.0f, 32.0f, 1.0f, PARAM_INT, "", 1, NULL,                              \
        "frequency ratio to the note (0 = 0.5, 1 = unison, 2 = octave, 3 = octave + fifth...)"),                   \
    OPP(N, FM4_FINE, "fine", "fine", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,                                     \
        "fine ratio: ratio x (1 + fine); non-integer ratios give inharmonic, bell-like spectra"),                  \
    OPP(N, FM4_DETUNE, "detune", "detune", -50.0f, 50.0f, 0.0f, PARAM_LIN, "cent", 1, NULL,                        \
        "small detune: beating against the other operators"),                                                     \
    OPP(N, FM4_MODE, "mode", "mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, mode_labels,                              \
        "ratio = the frequency follows the note; fixed = the frequency set below, whatever the note"),            \
    OPP(N, FM4_FREQ, "freq", "fixed_freq", 1.0f, 20000.0f, 440.0f, PARAM_EXP, "Hz", 1, NULL,                       \
        "frequency in fixed mode"),                                                                                \
    OPP(N, FM4_ATTACK, "attack", "attack", 0.001f, 10.0f, 0.002f, PARAM_EXP, "s", 1, NULL, "envelope attack"),     \
    OPP(N, FM4_DECAY, "decay", "decay", 0.005f, 20.0f, 1.0f, PARAM_EXP, "s", 1, NULL, "envelope decay"),           \
    OPP(N, FM4_SUSTAIN, "sustain", "sustain", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL, "envelope sustain level"), \
    OPP(N, FM4_RELEASE, "release", "release", 0.005f, 20.0f, 0.3f, PARAM_EXP, "s", 1, NULL, "envelope release"),   \
    OPP(N, FM4_VEL_SENS, "velocity", "velocity", 0.0f, 1.0f, VEL, PARAM_LIN, "", 1, NULL,                          \
        "velocity sensitivity (up to 24 dB): on a modulator, harder = brighter"),                                  \
    OPP(N, FM4_KEY_SCALE, "key_scale", "key_scaling", -12.0f, 12.0f, 0.0f, PARAM_LIN, "dB/oct", 1, NULL,           \
        "level change per octave from middle C: negative on a modulator keeps high notes from getting harsh")

#define MOD(S, D, df)                                                                                              \
    PARAM(FM4_P_MOD(S - 1, D - 1), "op" #S "_to_op" #D, "fm.op" #S ".mod" #D ".amount", 0.0f, 1.0f, df, PARAM_LIN,  \
          "", 1, NULL, "how much operator " #S " modulates operator " #D " (squared: 1 = a deviation of 2 cycles)")
#define FB(S)                                                                                                      \
    PARAM(FM4_P_MOD(S - 1, S - 1), "op" #S "_feedback", "fm.op" #S ".feedback", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, \
          NULL, "operator " #S " modulates itself: towards a saw, then noise")
#define OUT(S, df)                                                                                                 \
    PARAM(FM4_P_OUT(S - 1), "op" #S "_out", "fm.op" #S ".output", 0.0f, 1.0f, df, PARAM_LIN, "", 1, NULL,           \
          "how loud operator " #S " is heard (0 = a pure modulator)")

const param_desc_t fm4_params[FM4_P_COUNT] = {
    OP(1, 0.0f), OP(2, 0.5f), OP(3, 0.5f), OP(4, 0.5f),
    FB(1), MOD(1, 2, 0.0f), MOD(1, 3, 0.0f), MOD(1, 4, 0.0f),
    MOD(2, 1, 0.35f), FB(2), MOD(2, 3, 0.0f), MOD(2, 4, 0.0f),
    MOD(3, 1, 0.0f), MOD(3, 2, 0.0f), FB(3), MOD(3, 4, 0.0f),
    MOD(4, 1, 0.0f), MOD(4, 2, 0.0f), MOD(4, 3, 0.0f), FB(4),
    OUT(1, 1.0f), OUT(2, 0.0f), OUT(3, 0.0f), OUT(4, 0.0f),
    PARAM(FM4_P_PITCH_AMOUNT, "pitch_amount", "pitch_env.amount", -48.0f, 48.0f, 0.0f, PARAM_LIN, "st", 1, NULL,
          "pitch envelope: the note starts this far off and glides back (all operators in ratio mode)"),
    PARAM(FM4_P_PITCH_DECAY, "pitch_decay", "pitch_env.decay", 0.005f, 5.0f, 0.1f, PARAM_EXP, "s", 1, NULL,
          "time for the pitch envelope to come back (to 1/1000 of its depth)"),
    PARAM(FM4_P_PHASE_RESET, "phase_reset", "fm.osc_sync", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, param_onoff_labels,
          "on = the operators' phases restart on each note (the same attack every time); off = free running"),
    PARAM(FM4_P_FILTER_TYPE, "filter_type", "filter.type", 0.0f, 6.0f, 0.0f, PARAM_INT, "", 0, filter_labels,
          "off, 12 dB state-variable (lp/bp/hp/notch), formant (vowels) or scream (2-pole with saturating feedback)"),
    PARAM(FM4_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 20000.0f, 8000.0f, PARAM_EXP, "Hz", 1, NULL, "filter cutoff"),
    PARAM(FM4_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL, "filter resonance"),
    PARAM(FM4_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "cutoff follows the keyboard"),
    PARAM(FM4_P_FENV_AMT, "fenv_amount", "filter.envelope_amount", -4.0f, 6.0f, 0.0f, PARAM_LIN, "oct", 1, NULL,
          "filter envelope depth"),
    PARAM(FM4_P_FENV_A, "fenv_attack", "filter_env.attack", 0.001f, 10.0f, 0.005f, PARAM_EXP, "s", 1, NULL, "filter envelope attack"),
    PARAM(FM4_P_FENV_D, "fenv_decay", "filter_env.decay", 0.005f, 10.0f, 0.5f, PARAM_EXP, "s", 1, NULL, "filter envelope decay"),
    PARAM(FM4_P_FENV_S, "fenv_sustain", "filter_env.sustain", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL, "filter envelope sustain"),
    PARAM(FM4_P_FENV_R, "fenv_release", "filter_env.release", 0.005f, 10.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "filter envelope release"),
    PARAM_OVERSAMPLE(FM4_P_OVERSAMPLE, 1.0f, "oversampling of the scream filter: 1x lightest, 2x default, 4x cleanest"),
    PARAM(FM4_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(FM4_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 0, NULL,
          "random stereo position per note"),
    PARAM(FM4_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 12.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
};

/* ---------------- helpers ---------------- */

static void set_envelopes(fm4_t *e, fm4_voice_t *v)
{
    const float *p = e->p;
    for (int k = 0; k < FM4_OPS; k++) {
        const float *op = &p[FM4_OP(k, 0)];
        adsr_set(&v->eg[k], op[FM4_ATTACK], op[FM4_DECAY], op[FM4_SUSTAIN], op[FM4_RELEASE]);
    }
    adsr_set(&v->fenv, p[FM4_P_FENV_A], p[FM4_P_FENV_D], p[FM4_P_FENV_S], p[FM4_P_FENV_R]);
}

/* Operator frequency (Hz) for a pitch in semitones. */
static float op_freq(const float *op, float pitch)
{
    const float detune = fm_exp2(op[FM4_DETUNE] * (1.0f / 1200.0f));
    if ((int)op[FM4_MODE] == 1)
        return op[FM4_FREQ] * detune;
    const float coarse = op[FM4_COARSE] < 0.5f ? 0.5f : op[FM4_COARSE];
    return fm_mtof(pitch) * coarse * (1.0f + op[FM4_FINE]) * detune;
}

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    fm4_t *e = eng;
    fm4_voice_t *v = &e->v[vi];
    v->note = note;
    v->vel = dsp_clampf(vel, 0.0f, 1.0f);
    v->penv = 1.0f;
    /* phases restart only from silence: resetting a sounding voice would click */
    if (hard) {
        for (int k = 0; k < FM4_OPS; k++) {
            fm_op_reset(&v->op[k], (int)e->p[FM4_P_PHASE_RESET] ? 0.0f : dsp_rand_unipolar(&e->rng));
            v->o1[k] = v->o2[k] = 0.0f;
        }
        voice_pan(e->p[FM4_P_SPREAD] * dsp_rand_bipolar(&e->rng), &v->pan_l, &v->pan_r);
    }
    for (int k = 0; k < FM4_OPS; k++)
        adsr_gate(&v->eg[k], 1);
    adsr_gate(&v->fenv, 1);
}

static void voice_release(void *eng, int vi)
{
    fm4_voice_t *v = &((fm4_t *)eng)->v[vi];
    for (int k = 0; k < FM4_OPS; k++)
        adsr_gate(&v->eg[k], 0);
    adsr_gate(&v->fenv, 0);
}

static float voice_level(const void *eng, int vi)
{
    const fm4_t *e = eng;
    const fm4_voice_t *v = &e->v[vi];
    float m = 0.0f;
    for (int k = 0; k < FM4_OPS; k++)
        m = dsp_maxf(m, v->out_prev[k] * v->lvl_prev[k] * v->eg[k].level);
    return m;
}

static void voice_silence(void *eng, int vi)
{
    fm4_voice_t *v = &((fm4_t *)eng)->v[vi];
    for (int k = 0; k < FM4_OPS; k++) {
        adsr_reset(&v->eg[k]);
        v->o1[k] = v->o2[k] = 0.0f;
    }
    adsr_reset(&v->fenv);
}

static const vpoly_ops_t fm4_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void fm4_init(void *st, float sr)
{
    fm4_t *e = st;
    e->sr = sr;
    for (int i = 0; i < FM4_P_COUNT; i++)
        e->p[i] = fm4_params[i].def;
    e->bend = 0.0f;
    e->rng = dsp_seed(0xF4u);
    e->nan_count = 0;
    for (int i = 0; i < FM4_VOICES; i++) {
        fm4_voice_t *v = &e->v[i];
        for (int k = 0; k < FM4_OPS; k++) {
            adsr_init(&v->eg[k], sr);
            fm_op_reset(&v->op[k], 0.0f);
            v->out_prev[k] = v->lvl_prev[k] = 0.0f;
        }
        for (int k = 0; k < FM4_OPS * FM4_OPS; k++)
            v->m_prev[k] = 0.0f;
        adsr_init(&v->fenv, sr);
        vfilter_init(&v->vf, sr);
        v->filter_on = 0;
        v->pan_l = v->pan_r = 0.70710678f;
        v->gain_prev = 0.0f;
        v->penv = 0.0f;
        v->note = 60;
        v->vel = 1.0f;
        set_envelopes(e, v);
        voice_silence(e, i);
    }
    vpoly_init(&e->poly, FM4_VOICES, sr, &fm4_poly_ops, e);
}

static vpoly_t *fm4_poly(void *st) { return &((fm4_t *)st)->poly; }

static void fm4_set_param(void *st, int id, float norm)
{
    fm4_t *e = st;
    if (id < 0 || id >= FM4_P_COUNT)
        return;
    e->p[id] = param_from_norm(&fm4_params[id], norm);
    const int envelope = (id < FM4_P_MATRIX && (id % FM4_OP_PARAMS) >= FM4_ATTACK && (id % FM4_OP_PARAMS) <= FM4_RELEASE) ||
                         (id >= FM4_P_FENV_A && id <= FM4_P_FENV_R);
    if (envelope)
        for (int i = 0; i < FM4_VOICES; i++)
            set_envelopes(e, &e->v[i]);
    if (id == FM4_P_OVERSAMPLE)
        for (int i = 0; i < FM4_VOICES; i++)
            vfilter_set_oversampling(&e->v[i].vf, 1 << (int)e->p[id]);
}

static void fm4_pitch_bend(void *st, float bend) { ((fm4_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

static DSP_FAST_CODE void voice_render(fm4_t *e, int vi, float *L, float *R)
{
    enum { N = SYNTH_BLOCK, K = FM4_OPS };
    const float *p = e->p;
    fm4_voice_t *v = &e->v[vi];
    const float dt = (float)N / e->sr, inv_n = 1.0f / (float)N;
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */

    /* pitch: the note, bend, voice modulation and the pitch envelope (falls by 2^-10 over its decay time) */
    v->penv *= fm_exp2(-10.0f * dt / p[FM4_P_PITCH_DECAY]);
    const float pitch = (float)v->note + e->bend * p[FM4_P_BEND_RANGE] + p[FM4_P_PITCH_AMOUNT] * v->penv + vm[VM_PITCH];

    /* per operator: frequency, envelope (per sample), level (velocity and keyboard scaling, per block) */
    float env[K][N], lvl0[K], dlvl[K], out0[K], dout[K];
    int sounding = 0;
    for (int k = 0; k < K; k++) {
        const float *op = &p[FM4_OP(k, 0)];
        fm_op_block(&v->op[k], dsp_minf(op_freq(op, pitch) / e->sr, 0.45f), 1.0f, inv_n);
        adsr_process(&v->eg[k], env[k], N);
        const float db = -op[FM4_VEL_SENS] * (1.0f - v->vel) * 24.0f +
                         dsp_clampf(op[FM4_KEY_SCALE] * (float)(v->note - 60) * (1.0f / 12.0f), -48.0f, 12.0f);
        const float l1 = fm_db2lin(db), o1 = p[FM4_P_OUT(k)];
        lvl0[k] = v->lvl_prev[k], dlvl[k] = (l1 - lvl0[k]) * inv_n, v->lvl_prev[k] = l1;
        out0[k] = v->out_prev[k], dout[k] = (o1 - out0[k]) * inv_n, v->out_prev[k] = o1;
        if (o1 > 0.0f && adsr_active(&v->eg[k]))
            sounding = 1;
    }

    /* the matrix as phase deviations (cycles per unit of the source): cells squared, brightness (voice modulation)
     * scales every modulation by 6 dB per unit */
    const float bright = vm[VM_BRIGHT] != 0.0f ? fm_db2lin(6.0f * vm[VM_BRIGHT]) : 1.0f;
    /* only the links that are or were set this block are computed, per destination in source order */
    fm4_link_t lk[K][K];
    int nl[K] = {0};
    for (int d = 0; d < K; d++)
        for (int j = 0; j < K; j++) {
            /* the late sources first (operators d down to 1, known before the sample starts), then the ones of this
             * sample (above d, from the nearest): the sum waits as little as possible on the operators above */
            const int s = j <= d ? d - j : j;
            const float c = p[FM4_P_MOD(s, d)], t = c * c * (s == d ? FM4_FB_MAX : FM4_MOD_MAX) * bright;
            const float m0 = v->m_prev[s * K + d];
            v->m_prev[s * K + d] = t;
            if (t != 0.0f || m0 != 0.0f)
                lk[d][nl[d]++] = (fm4_link_t){m0, (t - m0) * inv_n, s, NULL};
        }
    /* and only the operators heard, or modulating one that is (directly or through others) */
    int need[K];
    for (int k = 0; k < K; k++)
        need[k] = out0[k] != 0.0f || out0[k] + dout[k] * (float)N != 0.0f;
    for (int pass = 0; pass < K - 1; pass++)
        for (int d = 0; d < K; d++)
            if (need[d])
                for (int j = 0; j < nl[d]; j++)
                    need[lk[d][j].s] = 1;

    /* audio: operators 4 -> 1 */
    float x[N];
    float o1[K], o2[K];
    for (int k = 0; k < K; k++) {
        o1[k] = v->o1[k], o2[k] = v->o2[k];
        if (!need[k]) { /* idle: its phase runs on (the same place when it is needed again), its output is 0 */
            fm_op_t *op = &v->op[k];
            op->phase = fm_frac(op->phase + op->inc * (float)N + op->dinc * (float)(N * (N + 1) / 2));
            op->inc += op->dinc * (float)N;
            o1[k] = o2[k] = 0.0f;
        }
    }
    float o[K] = {0.0f}, late[K];
    for (int d = 0; d < K; d++)
        for (int j = 0; j < nl[d]; j++)
            lk[d][j].src = lk[d][j].s > d ? &o[lk[d][j].s] : &late[lk[d][j].s];
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < K; k++)
            late[k] = 0.5f * (o1[k] + o2[k]);
        float y = 0.0f;
        for (int d = K - 1; d >= 0; d--) {
            if (!need[d])
                continue;
            float pm = 0.0f;
            fm4_link_t *l = lk[d];
            for (int j = 0; j < nl[d]; j++) {
                l[j].m += l[j].dm;
                pm += l[j].m * *l[j].src;
            }
            const float a = env[d][i] * (lvl0[d] + dlvl[d] * (float)(i + 1));
            o[d] = fm_op_tick(&v->op[d], pm) * a;
            y += o[d] * (out0[d] + dout[d] * (float)(i + 1));
        }
        for (int k = 0; k < K; k++)
            o2[k] = o1[k], o1[k] = o[k];
        x[i] = y;
    }
    for (int k = 0; k < K; k++)
        v->o1[k] = o1[k], v->o2[k] = o2[k];

    /* filter */
    float fenv[N];
    adsr_process(&v->fenv, fenv, N);
    const int ftype = (int)p[FM4_P_FILTER_TYPE];
    if (ftype > 0) {
        if (!v->filter_on)
            vfilter_reset(&v->vf);
        static const int types[6] = {VF_LP, VF_BP, VF_HP, VF_NOTCH, VF_FORMANT, VF_SCREAM};
        const float oct = fm_log2(p[FM4_P_CUTOFF]) + p[FM4_P_FENV_AMT] * fenv[N - 1] +
                          p[FM4_P_KEYTRACK] * (pitch - 60.0f) * (1.0f / 12.0f);
        const float cut = fm_exp2(dsp_clampf(oct, 4.3f, 14.3f)), reso = p[FM4_P_RESO];
        vfilter_set(&v->vf, types[dsp_clampi(ftype, 1, 6) - 1], cut, 0.5f * fm_exp2(5.3f * reso), reso, 0.5f);
        vfilter_process(&v->vf, x, x, N);
    }
    v->filter_on = ftype > 0;

    const float g1 = FM4_GAIN * fm_db2lin(p[FM4_P_VOLUME]);
    dsp_ramp_mul(x, v->gain_prev, g1, N);
    v->gain_prev = g1;
    voice_out(&e->poly, vi, x, N, v->pan_l, v->pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, sounding);
}

static DSP_FAST_CODE void fm4_render(void *st, float *L, float *R)
{
    fm4_t *e = st;
    for (int vi = 0; vi < FM4_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* the signal chain for interfaces (engine_stage_t): the four operators, the matrix that wires them, filter, amp */
#define OPSTAGE(N)                                                                                                 \
    {"op" #N, "Op " #N, "source",                                                                                  \
     "op" #N "_coarse op" #N "_fine op" #N "_detune op" #N "_mode op" #N "_freq op" #N "_attack op" #N "_decay op" #N \
     "_sustain op" #N "_release op" #N "_velocity op" #N "_key_scale",                                            \
     "", NULL, NULL, 0.0f, 0.0f}
static const engine_stage_t fm4_stages[] = {
    OPSTAGE(1), OPSTAGE(2), OPSTAGE(3), OPSTAGE(4),
    {"matrix", "Matrix", "matrix",
     "op1_feedback op1_to_op2 op1_to_op3 op1_to_op4 op2_to_op1 op2_feedback op2_to_op3 op2_to_op4 op3_to_op1 "
     "op3_to_op2 op3_feedback op3_to_op4 op4_to_op1 op4_to_op2 op4_to_op3 op4_feedback op1_out op2_out op3_out op4_out",
     "op1 op2 op3 op4", NULL, NULL, 0.0f, 0.0f},
    {"filter", "Filter", "filter", "filter_type cutoff resonance keytrack fenv_amount oversample", "matrix", NULL,
     "filter_type", 0.0f, 1.0f},
    {"amp", "Amp", "amp", "volume", "filter", NULL, NULL, 0.0f, 0.0f},
    {"penv", "Pitch env", "mod", "pitch_amount pitch_decay", "", "op1 op2 op3 op4", NULL, 0.0f, 0.0f},
    {"fenv", "Filter env", "mod", "fenv_attack fenv_decay fenv_sustain fenv_release", "", "filter", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "phase_reset pan_spread bend_range", "", "", NULL, 0.0f, 0.0f},
};

const engine_ops_t fm4_engine = {
    .name = "fm4",
    .description = "4-operator FM on a free modulation matrix (every operator modulates any other and itself, "
                   "outputs per operator; the classic algorithms are matrix presets): ratio/fixed frequencies, "
                   "ADSR per operator, velocity and keyboard scaling, pitch envelope, filter (16 voices)",
    .params = fm4_params,
    .param_count = FM4_P_COUNT,
    .voices = FM4_VOICES,
    .init = fm4_init,
    .poly = fm4_poly,
    .set_param = fm4_set_param,
    .pitch_bend = fm4_pitch_bend,
    .render = fm4_render,
    .stages = fm4_stages,
    .stage_count = (int)(sizeof fm4_stages / sizeof fm4_stages[0]),
};
