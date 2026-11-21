/* acid.c — acid bass engine (see acid.h). One voice (vpoly in legato mode):
 * - new note (no key held): pitch jumps, filter envelope restarts, accent decided by velocity;
 * - overlapping note (slide): pitch glides over `slide`, envelopes keep running (no retrigger);
 * - accent: +6 dB, envelope decay fixed short (0.2 s), extra envelope depth, and a slow "sweep"
 *   (one-pole charged by each accent) that pushes the cutoff further on consecutive accents.
 * Cutoff in octaves = log2(cutoff) + envmod * env + accent terms; per block, ladder interpolates. */
#include "acid.h"
#include "../core/fastmath.h"

#define ACID_GAIN 0.3f
#define ACCENT_DECAY_S 0.2f

static const char *const wave_labels[] = {"saw", "square", NULL};

const param_desc_t acid_params[ACID_P_COUNT] = {
    PARAM(ACID_P_WAVE, "wave", "osc1.waveform", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, wave_labels,
          "saw = classic squelch, square = hollower, woodier line"),
    PARAM(ACID_P_TUNE, "tune", "osc1.detune", -100.0f, 100.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "fine tuning"),
    PARAM(ACID_P_CUTOFF, "cutoff", "filter.cutoff", 40.0f, 8000.0f, 500.0f, PARAM_EXP, "Hz", 1, NULL,
          "base cutoff, the main knob to ride live (150-400 Hz dark and rubbery, 1-3 kHz screaming)"),
    PARAM(ACID_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.75f, PARAM_LIN, "", 1, NULL,
          "resonance: 0.5 vocal, 0.8 squelchy, 0.95+ whistling self-oscillation"),
    PARAM(ACID_P_ENVMOD, "env_mod", "filter.envelope_amount", 0.0f, 5.0f, 2.5f, PARAM_LIN, "oct", 1, NULL,
          "how far the filter envelope opens the cutoff on each note"),
    PARAM(ACID_P_DECAY, "decay", "filter_env.decay", 0.2f, 2.5f, 0.6f, PARAM_EXP, "s", 1, NULL,
          "filter envelope decay of non-accented notes: short = plucky blips, long = wah"),
    PARAM(ACID_P_ACCENT, "accent", "accent.amount", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "strength of accented notes (velocity >= 96, '!' in phrases): louder, brighter, shorter, and the "
          "cutoff climbs over consecutive accents"),
    PARAM(ACID_P_SLIDE, "slide", "voice.glide", 0.01f, 0.3f, 0.06f, PARAM_EXP, "s", 1, NULL,
          "slide time between overlapping notes ('~' in phrases); 60 ms is the classic"),
    PARAM(ACID_P_DRIVE, "drive", "filter.drive", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "saturation inside the filter: rounder, then grittier, louder resonance"),
    PARAM(ACID_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM_OVERSAMPLE(ACID_P_OVERSAMPLE, 1.0f, "oversampling of the nonlinear filter: 1x (lightest), 2x (default: clean drive and resonance), 4x (cleanest self-oscillation and hard drive high up, about twice the filter cost of 2x)"),
    PARAM(ACID_P_DIST, "distortion", "fx.distortion.amount", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "distortion after the filter, like a pedal: 0 = clean, 0.3 = warm grit, 0.6 = snarling, 1 = fuzz (level kept even)"),
    PARAM(ACID_P_DIST_TONE, "dist_tone", "fx.distortion.tone", 1000.0f, 12000.0f, 5000.0f, PARAM_EXP, "Hz", 1, NULL,
          "tone of the distortion: lower = darker, rounder; higher = fizzier"),
};

/* ---------------- vpoly callbacks (single voice) ---------------- */

static void note_on(acid_t *e, int note, float vel, int slide)
{
    const int accent = vel >= 0.75f;
    if (!slide) {
        e->pitch = (float)note; /* no glide between detached notes */
        e->fenv = 1.0f;
        const float t = accent ? ACCENT_DECAY_S : e->p[ACID_P_DECAY];
        e->fenv_coef = fm_exp2(-FM_LOG2E * (float)SYNTH_BLOCK / (t / 3.0f * e->sr));
        e->accented = (float)accent;
        if (accent)
            e->acc_env = 1.0f;
    }
    e->gate = 1;
    e->amp_target = 1.0f + (accent ? e->p[ACID_P_ACCENT] : 0.0f);
}

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    acid_t *e = eng;
    (void)vi;
    if (hard) {
        osc_reset(&e->osc, 0.0f);
        e->amp = 0.0f;
    }
    note_on(e, note, vel, 0);
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    (void)vi;
    note_on(eng, note, vel, 1); /* slide: the pitch glides in render, envelopes continue */
}

static void voice_release(void *eng, int vi)
{
    (void)vi;
    ((acid_t *)eng)->gate = 0;
}

static float voice_level(const void *eng, int vi)
{
    (void)vi;
    return ((const acid_t *)eng)->amp;
}

static void voice_silence(void *eng, int vi)
{
    acid_t *e = eng;
    (void)vi;
    e->amp = 0.0f;
    e->gate = 0;
}

static const vpoly_ops_t acid_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void acid_init(void *st, float sr)
{
    acid_t *e = st;
    const float ctrl = sr / (float)SYNTH_BLOCK;
    e->sr = sr;
    for (int i = 0; i < ACID_P_COUNT; i++)
        e->p[i] = acid_params[i].def;
    smooth_init(&e->sm_cut, 0.005f, ctrl, fm_log2(e->p[ACID_P_CUTOFF]));
    smooth_init(&e->sm_res, 0.005f, ctrl, e->p[ACID_P_RESO]);
    osc_init(&e->osc, sr);
    ladder_init(&e->filter, sr);
    hb2x_reset(&e->dist_os);
    e->dist_lp = 0.0f;
    ladder_set_mode(&e->filter, LADDER_LP18);
    ladder_set_oversampling(&e->filter, 2);
    e->pitch = 36.0f;
    e->fenv = 0.0f;
    e->fenv_coef = 0.99f;
    e->amp = e->amp_target = 0.0f;
    e->amp_coef_up = 1.0f - fm_exp2(-FM_LOG2E / (0.003f * sr));
    e->amp_coef_down = 1.0f - fm_exp2(-FM_LOG2E / (0.008f * sr));
    e->accented = e->acc_env = e->sweep = 0.0f;
    e->gain_prev = 0.0f;
    e->gate = 0;
    e->nan_count = 0;
    vpoly_init(&e->poly, 1, sr, &acid_poly_ops, e);
    vpoly_set_mode(&e->poly, VPOLY_LEGATO);
}

static vpoly_t *acid_poly(void *st) { return &((acid_t *)st)->poly; }

static void acid_set_param(void *st, int id, float norm)
{
    acid_t *e = st;
    if (id < 0 || id >= ACID_P_COUNT)
        return;
    e->p[id] = param_from_norm(&acid_params[id], norm);
    if (id == ACID_P_OVERSAMPLE)
        ladder_set_oversampling(&e->filter, 1 << (int)e->p[id]);
    if (!vpoly_active(&e->poly, 0)) { /* silent: nothing to glide, the next note starts on the new values */
        smooth_snap(&e->sm_cut, fm_log2(e->p[ACID_P_CUTOFF]));
        smooth_snap(&e->sm_res, e->p[ACID_P_RESO]);
    }
}

static void acid_pitch_bend(void *st, float bend) { (void)st, (void)bend; } /* the original had none */

static DSP_FAST_CODE void acid_render(void *st, float *L, float *R)
{
    acid_t *e = st;
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    float x[SYNTH_BLOCK];
    /* the envelopes and the accent charge keep discharging during rests (like the capacitors they model) */
    e->fenv *= e->fenv_coef;
    e->acc_env *= fm_exp2(-FM_LOG2E * (float)n / (ACCENT_DECAY_S / 3.0f * e->sr));
    e->sweep += (e->acc_env * p[ACID_P_ACCENT] - e->sweep) * 0.08f; /* charges over consecutive accents */
    if (!vpoly_active(&e->poly, 0))
        return;

    /* pitch: slide toward the held note (constant-time exponential, tau = slide / 3) */
    const float target = (float)vpoly_note(&e->poly, 0);
    e->pitch += (target - e->pitch) * voice_glide_coef(p[ACID_P_SLIDE], n, e->sr);
    osc_set_wave(&e->osc, (int)p[ACID_P_WAVE] ? OSC_PULSE : OSC_SAW);
    const float *vm = vpoly_vmod(&e->poly, 0); /* per-voice modulation, microtuning, MPE */
    osc_set(&e->osc, fm_mtof(e->pitch + 0.01f * p[ACID_P_TUNE] + vm[VM_PITCH]), 0.5f);

    /* filter: base + envelope + accent envelope + accent sweep (in octaves) */
    const float cut_oct = smooth_next(&e->sm_cut, fm_log2(p[ACID_P_CUTOFF])) + p[ACID_P_ENVMOD] * e->fenv +
                          1.5f * e->accented * p[ACID_P_ACCENT] * e->fenv + 2.0f * e->sweep + vm[VM_BRIGHT];
    ladder_set(&e->filter, fm_exp2(cut_oct), smooth_next(&e->sm_res, p[ACID_P_RESO]), p[ACID_P_DRIVE]);

    osc_process(&e->osc, x, n);
    ladder_process(&e->filter, x, x, n);

    /* amp: fast attack, holds while gated, 8 ms release; ramps for accents */
    const float tgt = e->gate ? e->amp_target : 0.0f;
    for (int i = 0; i < n; i++) {
        e->amp += (tgt - e->amp) * (tgt > e->amp ? e->amp_coef_up : e->amp_coef_down);
        x[i] *= e->amp;
    }
    /* distortion: drive up to +36 dB into an asymmetric tanh (even harmonics too), 2x oversampled, then its tone
     * (one-pole low-pass); the output is scaled back so turning it up changes the color more than the level */
    const float dist = p[ACID_P_DIST];
    if (dist > 0.0f) {
        const float g = fm_db2lin(36.0f * dist), bias = 0.25f * dist, b0 = fm_tanh(bias);
        const float mk = 0.7f / fm_tanh(0.7f * g);
        const float a = 1.0f - fm_exp2(-FM_LOG2E * 6.2831853f * p[ACID_P_DIST_TONE] / e->sr);
        for (int i = 0; i < n; i++) {
            float u0, u1;
            hb2x_up(&e->dist_os, x[i], &u0, &u1);
            u0 = fm_tanh(g * u0 + bias) - b0;
            u1 = fm_tanh(g * u1 + bias) - b0;
            const float y = hb2x_down(&e->dist_os, u0, u1) * mk;
            e->dist_lp += a * (y - e->dist_lp);
            x[i] = e->dist_lp;
        }
    }
    const float g1 = ACID_GAIN * fm_db2lin(p[ACID_P_VOLUME]);
    dsp_ramp_mul(x, e->gain_prev, g1, n);
    e->gain_prev = g1;
    voice_out(&e->poly, 0, x, n, VOICE_CENTER, VOICE_CENTER, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, 0, e->gate || e->amp > 1e-4f);
}

/* the signal chain for interfaces (engine_stage_t): sources, then filter and amp, modulators below */
static const engine_stage_t acid_stages[] = {
    {"osc", "Osc", "source", "wave tune", "", NULL, NULL, 0.0f, 0.0f},
    {"filter", "Filter", "filter", "cutoff resonance env_mod decay drive oversample", "osc", NULL, NULL, 0.0f, 0.0f},
    {"dist", "Distortion", "filter", "distortion dist_tone", "filter", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "volume", "dist", NULL, NULL, 0.0f, 0.0f},
    {"voice", "Accent and slide", "voice", "accent slide", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t acid_engine = {
    .name = "acid",
    .description = "monophonic acid bass (early-80s transistor bass line spirit): saw/square, resonant 18 dB "
                   "ladder (2x oversampled), decaying filter envelope, accent with cumulative sweep, slide",
    .params = acid_params,
    .param_count = ACID_P_COUNT,
    .voices = 1,
    .init = acid_init,
    .poly = acid_poly,
    .set_param = acid_set_param,
    .pitch_bend = acid_pitch_bend,
    .render = acid_render,
    .stages = acid_stages,
    .stage_count = (int)(sizeof acid_stages / sizeof acid_stages[0]),
};
