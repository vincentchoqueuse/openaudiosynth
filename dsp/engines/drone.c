/* drone.c — drone engine (see drone.h). Per block and voice: the random walks advance (Ornstein-Uhlenbeck,
 * time scale set by `motion`), the macros give each layer its targets (levels, pitches, pans, cut-offs), then the
 * audio loops interpolate them over the block. temperature t (0 = glacial, 1 = warm) moves the partials' tilt and
 * stretch, the body's level, the air's band centres and sharpness, and the sparkles' register.
 * Reference for the walk: dx = -x dt / T + sqrt(2 dt / T) dW gives a unit stationary deviation. */
#include "drone.h"
#include "../core/fastmath.h"

#define DRN_GAIN 0.08f
#define DRN_PHI 1.6180340f

static const char *const drone_help_temp = "glacial (0: pure glassy partials, high whistling air, bright bells) .. "
                                           "warm (1: dark partials, a body of detuned saws, low breathy air)";

const param_desc_t drone_params[DRN_P_COUNT] = {
    PARAM(DRN_P_TEMPERATURE, "temperature", "drone.temperature", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          drone_help_temp),
    PARAM(DRN_P_DENSITY, "density", "drone.density", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "how many partials sound (1 .. 12) and how much air: a lone sine .. a wall"),
    PARAM(DRN_P_MOTION, "motion", "drone.motion", 0.0f, 1.0f, 0.35f, PARAM_LIN, "", 1, NULL,
          "speed of every drift: 0 = almost still (walks over ~2 min), 1 = alive (~3 s)"),
    PARAM(DRN_P_DEPTH, "depth", "drone.depth", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 1, NULL,
          "size of the drifts: partial levels (up to +/-9 dB), pitches (up to +/-8 cents), body cut-off, air"),
    PARAM(DRN_P_BRIGHTNESS, "brightness", "drone.brightness", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "spectral tilt of the partials and cut-off of the body"),
    PARAM(DRN_P_SPARKLE, "sparkle", "drone.sparkle", 0.0f, 1.0f, 0.15f, PARAM_LIN, "", 1, NULL,
          "how often a high harmonic rings out of a note (0 = never, 1 = about 10 a minute)"),
    PARAM(DRN_P_WIDTH, "width", "drone.width", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL,
          "stereo spread of the partials, each wandering in its own place"),
    PARAM(DRN_P_PARTIALS_LEVEL, "partials_level", "drone.partials_level", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL,
          "level of the partials layer"),
    PARAM(DRN_P_BODY_LEVEL, "body_level", "drone.body_level", -60.0f, 6.0f, -4.0f, PARAM_DB, "dB", 1, NULL,
          "level of the body (detuned saws in a low-pass), faded in by temperature"),
    PARAM(DRN_P_AIR_LEVEL, "air_level", "drone.air_level", -60.0f, 6.0f, -6.0f, PARAM_DB, "dB", 1, NULL,
          "level of the air (noise in band-passes on the note's harmonics)"),
    PARAM(DRN_P_SPARKLE_LEVEL, "sparkle_level", "drone.sparkle_level", -60.0f, 6.0f, -6.0f, PARAM_DB, "dB", 1, NULL,
          "level of the sparkles"),
    PARAM(DRN_P_ATTACK, "amp_attack", "amp.attack", 0.05f, 180.0f, 6.0f, PARAM_EXP, "s", 1, NULL,
          "fade in of a note (up to 3 minutes)"),
    PARAM(DRN_P_RELEASE, "amp_release", "amp.release", 0.05f, 180.0f, 10.0f, PARAM_EXP, "s", 1, NULL,
          "fade out of a note (up to 3 minutes)"),
    PARAM(DRN_P_VOLUME, "volume", "output.volume", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(DRN_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
};

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* one step of a smoothed walk: a = dt / T, kick = sqrt(3) sqrt(2 a) (uniform noise of unit variance) */
static float walk_step(drone_walk_t *w, uint32_t *rng, float a, float kick)
{
    w->x += kick * dsp_rand_bipolar(rng) - a * w->x;
    w->y += 2.0f * a * (w->x - w->y);
    return 1.7f * w->y; /* the one-pole leaves ~0.6 of the deviation: back to ~1 */
}

/* ---------------- voices ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    drone_t *e = eng;
    drone_voice_t *v = &e->v[vi];
    v->note = note;
    v->vel = vel;
    v->gate = 1;
    if (hard) { /* a new drone: its own phases, detunes, places in the walks */
        for (int k = 0; k < DRN_SINES; k++) {
            v->phase[k] = dsp_rand_unipolar(&v->rng);
            v->detune[k] = 2.0f * dsp_rand_bipolar(&v->rng);
            v->w_amp[k].x = v->w_amp[k].y = 0.6f * dsp_rand_bipolar(&v->rng);
            v->w_pitch[k].x = v->w_pitch[k].y = 0.6f * dsp_rand_bipolar(&v->rng);
            v->w_pan[k].x = v->w_pan[k].y = 0.6f * dsp_rand_bipolar(&v->rng);
            v->al[k] = v->ar[k] = 0.0f;
        }
        v->lfo[0] = dsp_rand_unipolar(&v->rng);
        v->lfo[1] = dsp_rand_unipolar(&v->rng);
        for (int s = 0; s < 2; s++)
            osc_reset(&v->saw[s], dsp_rand_unipolar(&v->rng));
        svf_reset(&v->body_lp);
        svf_reset(&v->air_bp[0]);
        svf_reset(&v->air_bp[1]);
        v->ping_env = v->ping_peak = 0.0f;
        v->ping_rising = 0;
        v->env = v->env_prev = 0.0f;
        v->gain_prev = 0.0f;
        v->fresh = 1;
    }
}

static void voice_release(void *eng, int vi) { ((drone_t *)eng)->v[vi].gate = 0; }
static float voice_level(const void *eng, int vi) { return ((const drone_t *)eng)->v[vi].env; }
static void voice_silence(void *eng, int vi)
{
    drone_voice_t *v = &((drone_t *)eng)->v[vi];
    v->env = v->env_prev = 0.0f;
    v->gate = 0;
}

static const vpoly_ops_t drone_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void drone_init(void *st, float sr)
{
    drone_t *e = st;
    e->sr = sr;
    for (int i = 0; i < DRN_P_COUNT; i++)
        e->p[i] = drone_params[i].def;
    e->bend = 0.0f;
    e->nan_count = 0;
    for (int i = 0; i < DRN_VOICES; i++) {
        drone_voice_t *v = &e->v[i];
        for (int k = 0; k < DRN_SINES; k++) {
            v->phase[k] = v->inc[k] = v->al[k] = v->ar[k] = v->detune[k] = 0.0f;
            v->w_amp[k].x = v->w_amp[k].y = v->w_pitch[k].x = v->w_pitch[k].y = v->w_pan[k].x = v->w_pan[k].y = 0.0f;
        }
        for (int s = 0; s < 2; s++) {
            osc_init(&v->saw[s], sr);
            osc_set_wave(&v->saw[s], OSC_SAW);
        }
        svf_init(&v->body_lp, sr);
        svf_set_mode(&v->body_lp, SVF_LP);
        for (int b = 0; b < 2; b++) {
            svf_init(&v->air_bp[b], sr);
            svf_set_mode(&v->air_bp[b], SVF_BP);
        }
        v->noise = dsp_seed(0xD500u + (uint32_t)i);
        v->rng = dsp_seed(0xD400u + (uint32_t)i);
        v->lfo[0] = v->lfo[1] = 0.0f;
        v->ping_phase = v->ping_inc = v->ping_env = v->ping_peak = v->ping_pan = v->ping_up = v->ping_down = 0.0f;
        v->ping_l = v->ping_r = 0.0f;
        v->ping_rising = 0;
        v->env = v->env_prev = 0.0f;
        v->gate = 0;
        v->note = 60;
        v->vel = 1.0f;
        v->gain_prev = 0.0f;
        v->fresh = 1;
    }
    vpoly_init(&e->poly, DRN_VOICES, sr, &drone_poly_ops, e);
}

static vpoly_t *drone_poly(void *st) { return &((drone_t *)st)->poly; }

static void drone_set_param(void *st, int id, float norm)
{
    drone_t *e = st;
    if (id >= 0 && id < DRN_P_COUNT)
        e->p[id] = param_from_norm(&drone_params[id], norm);
}

static void drone_pitch_bend(void *st, float bend) { ((drone_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

/* Steal fade, per-voice level and pan modulation, NaN guard, then the voice's stereo pair into the bus. */
static void voice_out_stereo(drone_t *e, int vi, float *xl, float *xr, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    float g[SYNTH_BLOCK];
    if (vpoly_fade(&e->poly, vi, g, n))
        for (int i = 0; i < n; i++)
            xl[i] *= g[i], xr[i] *= g[i];
    for (int i = 0; i < n; i++)
        g[i] = 1.0f;
    float d0, d1, l0, r0, l1, r1;
    voice_vmod_apply(&e->poly, vi, g, n, &d0, &d1); /* g: the level modulation ramp (left at 1 without one) */
    voice_pan_offset(d0, 1.0f, 1.0f, &l0, &r0);
    voice_pan_offset(d1, 1.0f, 1.0f, &l1, &r1);
    dsp_guard_block(xl, n, &e->nan_count);
    dsp_guard_block(xr, n, &e->nan_count);
    const float dl = (l1 - l0) / (float)n, dr = (r1 - r0) / (float)n;
    for (int i = 0; i < n; i++) {
        l0 += dl, r0 += dr;
        L[i] += xl[i] * g[i] * l0;
        R[i] += xr[i] * g[i] * r0;
    }
}

static DSP_FAST_CODE void voice_render(drone_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    drone_voice_t *v = &e->v[vi];
    const float sr = e->sr, dt = (float)n / sr, inv_sr = 1.0f / sr, inv_n = 1.0f / (float)n;
    const float fmax = 0.45f * sr;
    const float t = p[DRN_P_TEMPERATURE], dens = p[DRN_P_DENSITY], depth = p[DRN_P_DEPTH];
    const float bright = p[DRN_P_BRIGHTNESS], width = p[DRN_P_WIDTH];
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, concert pitch, MPE */
    const float f0 = fm_mtof((float)v->note + e->bend * p[DRN_P_BEND_RANGE] + vm[VM_PITCH]);

    /* envelope (block rate): exponential towards an overshooting target, as an analog one */
    v->env_prev = v->env;
    if (v->gate) {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 1.609f * dt / p[DRN_P_ATTACK]); /* 0 -> 1 in `attack` */
        v->env = dsp_minf(v->env + (1.25f - v->env) * c, 1.0f);
    } else {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 3.932f * dt / p[DRN_P_RELEASE]); /* 1 -> 0 in `release` */
        v->env += (-0.02f - v->env) * c;
        if (v->env < 0.0f)
            v->env = 0.0f;
    }

    /* walks and LFOs: time scale from ~2 minutes (motion 0) to ~3 s (motion 1) */
    const float T = 120.0f * fm_exp2(-5.3f * p[DRN_P_MOTION]);
    const float a = dt / T, kick = 1.7320508f * fm_exp2(0.5f * fm_log2(2.0f * a));
    v->lfo[0] += dt / (2.5f * T + 6.0f);
    v->lfo[1] += dt / ((2.5f * T + 6.0f) * DRN_PHI); /* the golden ratio: the two never fall back in step */
    v->lfo[0] -= (float)(int)v->lfo[0];
    v->lfo[1] -= (float)(int)v->lfo[1];
    const float lfo0 = fm_sin_pos(v->lfo[0]), lfo1 = fm_sin_pos(v->lfo[1]);

    /* partials: harmonic h (stretched when cold), tilt, density fade-in, wandering level, pitch, place */
    const float tilt = dsp_clampf(lerpf(0.3f, 1.2f, t) + 1.2f * (0.5f - bright) - 0.25f * vm[VM_BRIGHT], 0.0f, 3.0f);
    const float stretch = 0.0004f * (1.0f - t);
    const float n_on = 1.0f + dens * (float)(DRN_PARTIALS - 1);
    const float beat_hz = 0.08f + 0.5f * p[DRN_P_MOTION];
    const float g_part = fm_db2lin(p[DRN_P_PARTIALS_LEVEL]);
    float inc1[DRN_SINES], al1[DRN_SINES], ar1[DRN_SINES];
    for (int k = 0; k < DRN_SINES; k++) {
        const int kp = k < DRN_PARTIALS ? k : k - DRN_PARTIALS; /* the twins follow partials 1 and 2 */
        const float h = (float)(kp + 1);
        const float wa = walk_step(&v->w_amp[k], &v->rng, a, kick);
        const float wp = walk_step(&v->w_pitch[k], &v->rng, a, kick);
        const float wn = walk_step(&v->w_pan[k], &v->rng, a, kick);
        const float cents = v->detune[k] + depth * 8.0f * wp;
        float f = f0 * h * fm_exp2(0.5f * fm_log2(1.0f + stretch * h * h) + cents * (1.0f / 1200.0f));
        if (k >= DRN_PARTIALS)
            f += beat_hz * (k == DRN_PARTIALS ? 1.0f : -1.3f);
        const float on = dsp_clampf(n_on - (float)kp, 0.0f, 1.0f);
        float amp = on * g_part * fm_exp2(-tilt * fm_log2(h) + depth * 1.5f * wa);
        if (k >= DRN_PARTIALS)
            amp *= 0.6f;
        if (f >= fmax)
            amp = 0.0f, f = fmax;
        float pl, pr;
        voice_pan(dsp_clampf(width * 0.8f * wn, -1.0f, 1.0f), &pl, &pr);
        inc1[k] = f * inv_sr;
        al1[k] = amp * pl;
        ar1[k] = amp * pr;
    }

    float xl[SYNTH_BLOCK], xr[SYNTH_BLOCK], tmp[SYNTH_BLOCK], tmp2[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        xl[i] = xr[i] = 0.0f;
    for (int k = 0; k < DRN_SINES; k++) {
        if (v->fresh)
            v->inc[k] = inc1[k], v->al[k] = al1[k], v->ar[k] = ar1[k];
        if (al1[k] == 0.0f && ar1[k] == 0.0f && v->al[k] == 0.0f && v->ar[k] == 0.0f) {
            v->phase[k] += (float)n * inc1[k]; /* silent: keep its phase moving, skip the work */
            v->phase[k] -= (float)(int)v->phase[k];
            v->inc[k] = inc1[k];
            continue;
        }
        const float di = (inc1[k] - v->inc[k]) * inv_n;
        const float dl = (al1[k] - v->al[k]) * inv_n, dr = (ar1[k] - v->ar[k]) * inv_n;
        float ph = v->phase[k], inc = v->inc[k], gl = v->al[k], gr = v->ar[k];
        for (int i = 0; i < n; i++) {
            inc += di, gl += dl, gr += dr;
            ph += inc;
            ph -= (float)(int)ph;
            const float s = fm_sin_pos(ph);
            xl[i] += gl * s;
            xr[i] += gr * s;
        }
        v->phase[k] = ph, v->inc[k] = inc1[k], v->al[k] = al1[k], v->ar[k] = ar1[k];
    }

    /* body: two saws a few cents apart in a low-pass whose cut-off breathes with LFO 1; warm side only */
    const float g_body = fm_db2lin(p[DRN_P_BODY_LEVEL]) * t * t * 1.2f;
    if (g_body > 1e-5f) {
        const float spread = 4.0f + 8.0f * t, drift = depth * 6.0f * v->w_pitch[0].y;
        osc_set(&v->saw[0], f0 * fm_exp2((drift + spread) * (1.0f / 1200.0f)), 0.5f);
        osc_set(&v->saw[1], f0 * fm_exp2((drift - spread) * (1.0f / 1200.0f)), 0.5f);
        osc_process(&v->saw[0], tmp, n);
        osc_process(&v->saw[1], tmp2, n);
        for (int i = 0; i < n; i++)
            tmp[i] = (tmp[i] + tmp2[i]) * g_body;
        const float oct = 0.8f + 3.2f * bright + depth * 0.8f * lfo0 + 0.5f * vm[VM_BRIGHT];
        svf_set(&v->body_lp, dsp_clampf(f0 * fm_exp2(oct), 30.0f, 12000.0f), 0.8f);
        svf_process(&v->body_lp, tmp, tmp, n);
        for (int i = 0; i < n; i++) /* a little apart: the saws are already a chorus */
            xl[i] += tmp[i] * 0.75f, xr[i] += tmp[i] * 0.66f;
    }

    /* air: noise in two band-passes on harmonics of the note (high and sharp when cold, low and soft warm) */
    const float g_air = fm_db2lin(p[DRN_P_AIR_LEVEL]) * (0.15f + 0.85f * dens) * fm_exp2(depth * 1.0f * lfo1);
    if (g_air > 1e-5f) {
        const float q = lerpf(22.0f, 4.0f, t);
        const float h_air[2] = {lerpf(6.0f, 2.0f, t), lerpf(11.0f, 3.0f, t)};
        const float gq = g_air * 5.0f * fm_exp2(0.5f * fm_log2(q)); /* sharper = narrower: same loudness */
        for (int i = 0; i < n; i++)
            tmp[i] = dsp_rand_bipolar(&v->noise);
        for (int b = 0; b < 2; b++) {
            svf_set(&v->air_bp[b], dsp_clampf(f0 * h_air[b], 40.0f, fmax), q);
            svf_process(&v->air_bp[b], tmp, tmp2, n);
            const float gl = gq * (b ? 0.45f : 0.75f), gr = gq * (b ? 0.75f : 0.45f);
            for (int i = 0; i < n; i++)
                xl[i] += tmp2[i] * gl, xr[i] += tmp2[i] * gr;
        }
    }

    /* sparkles: now and then a high harmonic swells in (50 ms) and rings away over seconds */
    const float rate = p[DRN_P_SPARKLE] * (10.0f / 60.0f); /* per second */
    if (rate > 0.0f && v->ping_env < 0.002f && v->gate && dsp_rand_unipolar(&v->rng) < rate * dt) {
        static const float warm_h[5] = {3.0f, 4.0f, 5.0f, 6.0f, 8.0f};
        static const float cold_h[5] = {6.0f, 8.0f, 10.0f, 12.0f, 16.0f};
        const int pick = dsp_clampi((int)(5.0f * dsp_rand_unipolar(&v->rng)), 0, 4);
        const float h = dsp_rand_unipolar(&v->rng) < t ? warm_h[pick] : cold_h[pick];
        const float f = f0 * h * fm_exp2(0.5f * fm_log2(1.0f + stretch * h * h));
        if (f < fmax) {
            v->ping_inc = f * inv_sr;
            v->ping_peak = fm_db2lin(p[DRN_P_SPARKLE_LEVEL]) * (0.5f + 0.5f * dsp_rand_unipolar(&v->rng)) * 1.5f;
            v->ping_rising = 1;
            v->ping_up = 1.0f - fm_exp2(-FM_LOG2E * dt / 0.05f);
            v->ping_down = fm_exp2(-FM_LOG2E * dt / lerpf(3.0f, 1.2f, t)); /* cold rings longer */
            voice_pan(dsp_rand_bipolar(&v->rng) * width, &v->ping_l, &v->ping_r);
        }
    }
    if (v->ping_peak > 0.0f) {
        const float e0 = v->ping_env;
        if (v->ping_rising) {
            v->ping_env += (1.1f - v->ping_env) * v->ping_up;
            if (v->ping_env >= 1.0f)
                v->ping_env = 1.0f, v->ping_rising = 0;
        } else {
            v->ping_env *= v->ping_down;
            if (v->ping_env < 1e-4f)
                v->ping_env = 0.0f, v->ping_peak = 0.0f;
        }
        const float g0 = e0 * v->ping_peak, de = (v->ping_env * v->ping_peak - g0) * inv_n;
        float g = g0, ph = v->ping_phase;
        for (int i = 0; i < n; i++) {
            g += de;
            ph += v->ping_inc;
            ph -= (float)(int)ph;
            const float s = g * fm_sin_pos(ph);
            xl[i] += s * v->ping_l, xr[i] += s * v->ping_r;
        }
        v->ping_phase = ph;
    }
    v->fresh = 0;

    /* envelope and gain, interpolated over the block */
    const float g1 = DRN_GAIN * fm_db2lin(p[DRN_P_VOLUME]) * (0.6f + 0.4f * v->vel);
    const float e0 = v->env_prev * v->gain_prev, de = (v->env * g1 - e0) * inv_n;
    float g = e0;
    for (int i = 0; i < n; i++) {
        g += de;
        xl[i] *= g, xr[i] *= g;
    }
    v->gain_prev = g1;
    voice_out_stereo(e, vi, xl, xr, L, R);
    vpoly_end_block(&e->poly, vi, v->gate || v->env > 0.0f);
}

static DSP_FAST_CODE void drone_render(void *st, float *L, float *R)
{
    drone_t *e = st;
    for (int vi = 0; vi < DRN_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* the signal chain for interfaces (engine_stage_t): sources, then filter and amp, modulators below */
static const engine_stage_t drone_stages[] = {
    {"partials", "Partials", "source", "partials_level", "", NULL, "partials_level", -60.0f, 0.0f},
    {"body", "Body", "source", "body_level", "", NULL, "body_level", -60.0f, -4.0f},
    {"air", "Air", "source", "air_level", "", NULL, "air_level", -60.0f, -6.0f},
    {"sparkles", "Sparkles", "source", "sparkle sparkle_level", "", NULL, "sparkle", 0.0f, 0.15f},
    {"amp", "Amp", "amp", "amp_attack amp_release volume width", "partials body air sparkles", NULL, NULL, 0.0f, 0.0f},
    {"character", "Character", "mod", "temperature density brightness", "", "partials body air sparkles", NULL, 0.0f, 0.0f},
    {"motion", "Motion", "mod", "motion depth", "", "partials body air", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t drone_engine = {
    .name = "drone",
    .description = "drone machine: each note an endless drone of wandering harmonic partials, a warm body of saws, "
                   "air on the harmonics and rare sparkles; temperature (glacial .. warm), density, motion, depth, "
                   "brightness, sparkle, width; envelopes up to 3 minutes (8 voices)",
    .params = drone_params,
    .param_count = DRN_P_COUNT,
    .voices = DRN_VOICES,
    .init = drone_init,
    .poly = drone_poly,
    .set_param = drone_set_param,
    .pitch_bend = drone_pitch_bend,
    .render = drone_render,
    .stages = drone_stages,
    .stage_count = (int)(sizeof drone_stages / sizeof drone_stages[0]),
};
