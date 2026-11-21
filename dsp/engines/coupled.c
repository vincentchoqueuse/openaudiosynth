/* coupled.c — coupled-oscillator engine (see coupled.h). Per block and voice, for partial k (harmonic number
 * h = k + 1, its neighbours j within `reach`, mean over them noted <>, d = theta_j - theta_k in cycles):
 *   r_k  += dt [ r_k (1 - r_k^2) + Ka <r_j cos 2 pi d>  (- min(Ka, 0.998) r_k when frozen: diffusive, never silent) ]
 *   f_k   = fn_k (1 - beta (r_k^2 - 1) + 0.03 Kp <r_j sin 2 pi d> / (r_k + 0.4))
 * with fn_k = f0 h sqrt(1 + B h^2) 2^(cents / 1200): the pull is relative (like the detune, in cents), so every
 * partial has the same capture range. Each partial is a sine at h theta, theta advancing by f_k / h per second,
 * at level r_k / h^tilt. */
#include "coupled.h"
#include "../core/fastmath.h"

#define CPL_GAIN 0.06f

const param_desc_t coupled_params[CPL_P_COUNT] = {
    PARAM(CPL_P_PARTIALS, "partials", "network.partials", 4.0f, (float)CPL_PARTIALS, 16.0f, PARAM_INT, "", 0, NULL,
          "number of partials in each note's network (cost grows with it)"),
    PARAM(CPL_P_STRETCH, "stretch", "network.stretch", 0.0f, 0.003f, 0.0003f, PARAM_LIN, "", 1, NULL,
          "inharmonicity B: partial h at h sqrt(1 + B h^2) (0 = harmonic, 0.001 = bell-like)"),
    PARAM(CPL_P_SPREAD, "spread", "network.spread", 0.0f, 100.0f, 15.0f, PARAM_LIN, "cent", 0, NULL,
          "random detune of each partial, new at every note: the disorder the coupling fights with"),
    PARAM(CPL_P_DRIFT, "drift", "network.drift", 0.0f, 60.0f, 0.0f, PARAM_LIN, "cent", 1, NULL,
          "slow random walk of each partial's natural pitch: keeps the network searching"),
    PARAM(CPL_P_DRIFT_TIME, "drift_time", "network.drift_time", 0.5f, 20.0f, 4.0f, PARAM_EXP, "s", 1, NULL,
          "time scale of the random walk"),
    PARAM(CPL_P_TILT, "tilt", "network.tilt", 0.0f, 2.0f, 0.9f, PARAM_LIN, "", 1, NULL,
          "spectral tilt: partial h at level 1 / h^tilt (0 = bright and glassy, 2 = dark)"),
    PARAM(CPL_P_AMP_COUPLING, "amp_coupling", "network.amp_coupling", -2.0f, 2.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "phase -> modulus: neighbours in step feed each other (> 0) or compete (< 0: winners flicker in and out)"),
    PARAM(CPL_P_PHASE_COUPLING, "phase_coupling", "network.phase_coupling", 0.0f, 3.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "phase -> pitch: partials pull each other towards the harmonic series (strong = they lock)"),
    PARAM(CPL_P_PITCH_BY_LEVEL, "pitch_by_level", "network.pitch_by_level", 0.0f, 0.15f, 0.03f, PARAM_LIN, "", 1, NULL,
          "modulus -> pitch: a partial gets flatter as it gets louder (0.03 ~ +/-50 cents of life, 0.1 = wild)"),
    PARAM(CPL_P_REACH, "reach", "network.reach", 1.0f, 4.0f, 1.0f, PARAM_INT, "", 0, NULL,
          "how many neighbours on each side a partial listens to"),
    PARAM(CPL_P_FREEZE, "freeze", "network.freeze", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "diffusive coupling: detuned partials extinguish each other, the spectrum freezes, sparse and glassy"),
    PARAM(CPL_P_ATTACK, "amp_attack", "amp.attack", 0.0005f, 30.0f, 0.5f, PARAM_EXP, "s", 1, NULL, "amp attack"),
    PARAM(CPL_P_DECAY, "amp_decay", "amp.decay", 0.005f, 30.0f, 1.0f, PARAM_EXP, "s", 1, NULL, "amp decay"),
    PARAM(CPL_P_SUSTAIN, "amp_sustain", "amp.sustain", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL, "amp sustain"),
    PARAM(CPL_P_RELEASE, "amp_release", "amp.release", 0.005f, 30.0f, 3.0f, PARAM_EXP, "s", 1, NULL, "amp release"),
    PARAM(CPL_P_VOLUME, "volume", "output.volume", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(CPL_P_SPREAD_PAN, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL,
          "random stereo position per note"),
    PARAM(CPL_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
};

/* ---------------- voices ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    coupled_t *e = eng;
    coupled_voice_t *v = &e->v[vi];
    v->note = note;
    if (hard) { /* a new network: random phases, moduli and detunes */
        for (int k = 0; k < CPL_PARTIALS; k++) {
            v->theta[k] = dsp_rand_unipolar(&v->rng);
            v->r[k] = 0.3f + 0.7f * dsp_rand_unipolar(&v->rng);
            v->detune[k] = e->p[CPL_P_SPREAD] * dsp_rand_bipolar(&v->rng);
            v->drift[k] = 0.0f;
            v->amp[k] = 0.0f;
        }
        adsr_reset(&v->env);
        voice_pan(e->p[CPL_P_SPREAD_PAN] * dsp_rand_bipolar(&e->rng), &v->pan_l, &v->pan_r);
        v->gain_prev = 0.0f;
        v->fresh = 1;
    }
    v->vel = vel;
    adsr_gate(&v->env, 1);
}

static void voice_release(void *eng, int vi) { adsr_gate(&((coupled_t *)eng)->v[vi].env, 0); }
static float voice_level(const void *eng, int vi) { return ((const coupled_t *)eng)->v[vi].env.level; }
static void voice_silence(void *eng, int vi) { adsr_reset(&((coupled_t *)eng)->v[vi].env); }

static const vpoly_ops_t coupled_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void coupled_init(void *st, float sr)
{
    coupled_t *e = st;
    e->sr = sr;
    for (int i = 0; i < CPL_P_COUNT; i++)
        e->p[i] = coupled_params[i].def;
    e->bend = 0.0f;
    e->env_dirty = 1;
    e->rng = dsp_seed(0xC0C0u);
    e->nan_count = 0;
    for (int i = 0; i < CPL_VOICES; i++) {
        coupled_voice_t *v = &e->v[i];
        adsr_init(&v->env, sr);
        v->rng = dsp_seed(0xC100u + (uint32_t)i);
        v->gain_prev = 0.0f;
        v->pan_l = v->pan_r = VOICE_CENTER;
        v->note = 60;
        v->vel = 1.0f;
        v->fresh = 1;
        for (int k = 0; k < CPL_PARTIALS; k++)
            v->theta[k] = v->inc[k] = v->r[k] = v->amp[k] = v->detune[k] = v->drift[k] = 0.0f;
    }
    vpoly_init(&e->poly, CPL_VOICES, sr, &coupled_poly_ops, e);
}

static vpoly_t *coupled_poly(void *st) { return &((coupled_t *)st)->poly; }

static void coupled_set_param(void *st, int id, float norm)
{
    coupled_t *e = st;
    if (id < 0 || id >= CPL_P_COUNT)
        return;
    e->p[id] = param_from_norm(&coupled_params[id], norm);
    if (id >= CPL_P_ATTACK && id <= CPL_P_RELEASE)
        e->env_dirty = 1;
}

static void coupled_pitch_bend(void *st, float bend) { ((coupled_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

static DSP_FAST_CODE void voice_render(coupled_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    coupled_voice_t *v = &e->v[vi];
    const int np = dsp_clampi((int)p[CPL_P_PARTIALS], 1, CPL_PARTIALS);
    const int reach = dsp_clampi((int)p[CPL_P_REACH], 1, 4);
    const float dt = (float)n / e->sr, inv_sr = 1.0f / e->sr, inv_n = 1.0f / (float)n;
    const float ka = p[CPL_P_AMP_COUPLING], kp = 0.03f * p[CPL_P_PHASE_COUPLING];
    const float ka_diff = dsp_minf(ka, 0.998f); /* diffusive self term just below the self-excitation: incoherent partials sink to ~-30 dB, never to silence */
    const float beta = p[CPL_P_PITCH_BY_LEVEL], B = p[CPL_P_STRETCH];
    const int freeze = (int)p[CPL_P_FREEZE];
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float f0 = fm_mtof((float)v->note + e->bend * p[CPL_P_BEND_RANGE] + vm[VM_PITCH]);
    const float tilt = dsp_clampf(p[CPL_P_TILT] - 0.25f * vm[VM_BRIGHT], 0.0f, 3.0f);
    /* drift: Ornstein-Uhlenbeck walk, x += -a x + sigma sqrt(2 a) noise (uniform noise scaled to unit variance):
     * stationary deviation sigma = drift cents, time scale drift_time */
    const float drift = p[CPL_P_DRIFT];
    const float a_drift = dt / p[CPL_P_DRIFT_TIME];
    const float kick = drift * 1.7320508f * fm_exp2(0.5f * fm_log2(2.0f * a_drift));

    /* couplings (block rate) */
    float r_new[CPL_PARTIALS], inc1[CPL_PARTIALS], amp1[CPL_PARTIALS];
    for (int k = 0; k < np; k++) {
        float c = 0.0f, s = 0.0f;
        int deg = 0;
        for (int d = -reach; d <= reach; d++) {
            const int j = k + d;
            if (d == 0 || j < 0 || j >= np)
                continue;
            const float ph = v->theta[j] - v->theta[k];
            c += v->r[j] * fm_cos(ph);
            s += v->r[j] * fm_sin(ph);
            deg++;
        }
        const float r = v->r[k];
        const float inv_deg = deg > 0 ? 1.0f / (float)deg : 0.0f;
        c *= inv_deg, s *= inv_deg;
        float dr = r * (1.0f - r * r) + ka * c;
        if (freeze)
            dr -= ka_diff * r;
        r_new[k] = dsp_clampf(r + dt * dr, 1e-4f, 2.0f);
        if (drift > 0.0f)
            v->drift[k] += kick * dsp_rand_bipolar(&v->rng) - a_drift * v->drift[k];
        const float h = (float)(k + 1);
        float f = f0 * h * fm_exp2(0.5f * fm_log2(1.0f + B * h * h) + (v->detune[k] + v->drift[k]) * (1.0f / 1200.0f));
        f = f * (1.0f - beta * (r * r - 1.0f) + kp * s / (r + 0.4f));
        f = dsp_clampf(f, 0.0f, 0.45f * e->sr);
        inc1[k] = f * inv_sr / h; /* theta advances on the fundamental's scale */
        amp1[k] = f < 0.45f * e->sr - 1.0f ? r_new[k] * fm_exp2(-tilt * fm_log2(h)) : 0.0f;
    }

    /* audio: sum of sines at h theta, phase increment and level interpolated over the block */
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        x[i] = 0.0f;
    /* Two partials per pass: their two independent chains of operations overlap in the FPU's pipeline (the
     * Cortex-M7 waits on each result otherwise). Same operations, same order of the sum: same output. th >= 0
     * always (it starts in [0, 1), inc >= 0), hence fm_sin_pos. */
    int k = 0;
    for (; k + 1 < np; k += 2) {
        const float h0 = (float)(k + 1), h1 = (float)(k + 2);
        const float i00 = v->fresh ? inc1[k] : v->inc[k], a00 = v->fresh ? amp1[k] : v->amp[k];
        const float i01 = v->fresh ? inc1[k + 1] : v->inc[k + 1], a01 = v->fresh ? amp1[k + 1] : v->amp[k + 1];
        const float di0 = (inc1[k] - i00) * inv_n, da0 = (amp1[k] - a00) * inv_n;
        const float di1 = (inc1[k + 1] - i01) * inv_n, da1 = (amp1[k + 1] - a01) * inv_n;
        float th0 = v->theta[k], inc0 = i00, g0 = a00;
        float th1 = v->theta[k + 1], incb = i01, g1 = a01;
        for (int i = 0; i < n; i++) {
            inc0 += di0, g0 += da0;
            incb += di1, g1 += da1;
            th0 += inc0;
            th1 += incb;
            th0 -= (float)(int)th0; /* wrap to [0, 1) */
            th1 -= (float)(int)th1;
            const float s0 = g0 * fm_sin_pos(h0 * th0), s1 = g1 * fm_sin_pos(h1 * th1);
            x[i] = (x[i] + s0) + s1;
        }
        v->theta[k] = th0, v->inc[k] = inc1[k], v->amp[k] = amp1[k], v->r[k] = r_new[k];
        v->theta[k + 1] = th1, v->inc[k + 1] = inc1[k + 1], v->amp[k + 1] = amp1[k + 1], v->r[k + 1] = r_new[k + 1];
    }
    for (; k < np; k++) {
        const float h = (float)(k + 1);
        const float i0 = v->fresh ? inc1[k] : v->inc[k], a0 = v->fresh ? amp1[k] : v->amp[k];
        const float di = (inc1[k] - i0) * inv_n, da = (amp1[k] - a0) * inv_n;
        float th = v->theta[k], inc = i0, a = a0;
        for (int i = 0; i < n; i++) {
            inc += di, a += da;
            th += inc;
            th -= (float)(int)th; /* wrap to [0, 1) */
            x[i] += a * fm_sin_pos(h * th);
        }
        v->theta[k] = th, v->inc[k] = inc1[k], v->amp[k] = amp1[k], v->r[k] = r_new[k];
    }
    v->fresh = 0;

    adsr_process(&v->env, env, n);
    const float g1 = CPL_GAIN * fm_db2lin(p[CPL_P_VOLUME]) * (0.4f + 0.6f * v->vel);
    dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
    v->gain_prev = g1;
    voice_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, adsr_active(&v->env));
}

static DSP_FAST_CODE void coupled_render(void *st, float *L, float *R)
{
    coupled_t *e = st;
    if (e->env_dirty) {
        for (int i = 0; i < CPL_VOICES; i++)
            adsr_set(&e->v[i].env, e->p[CPL_P_ATTACK], e->p[CPL_P_DECAY], e->p[CPL_P_SUSTAIN], e->p[CPL_P_RELEASE]);
        e->env_dirty = 0;
    }
    for (int vi = 0; vi < CPL_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* the signal chain for interfaces (engine_stage_t): sources, then filter and amp, modulators below */
static const engine_stage_t coupled_stages[] = {
    {"network", "Partials", "source", "partials stretch spread tilt", "", NULL, NULL, 0.0f, 0.0f},
    {"coupling", "Coupling", "filter", "amp_coupling phase_coupling pitch_by_level reach freeze", "network", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "amp_ volume pan_spread", "coupling", NULL, NULL, 0.0f, 0.0f},
    {"motion", "Motion", "mod", "drift drift_time", "", "network", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t coupled_engine = {
    .name = "coupled",
    .description = "coupled-oscillator networks: each note is a network of Stuart-Landau partials whose modulus bends "
                   "their pitch and whose phase relations (harmonic n:m coupling) feed or starve their modulus; "
                   "random detune and drift, cooperation or competition, locking, freezing (8 voices)",
    .params = coupled_params,
    .param_count = CPL_P_COUNT,
    .voices = CPL_VOICES,
    .init = coupled_init,
    .poly = coupled_poly,
    .set_param = coupled_set_param,
    .pitch_bend = coupled_pitch_bend,
    .render = coupled_render,
    .stages = coupled_stages,
    .stage_count = (int)(sizeof coupled_stages / sizeof coupled_stages[0]),
};
