/* gull.c — seabird call engine (see gull.h). Control rate (one SYNTH_BLOCK): the syllable sequencer, the pitch
 * contour, the vowel and the jitter; audio rate: the pulse (polyBLEP), the subharmonic, the breath noise and the
 * formant filter. */
#include "gull.h"
#include "../core/fastmath.h"

#define GULL_GAIN 0.3f
#define GULL_JITTER_ST 0.6f   /* semitones of pitch jitter at jitter 1 */
#define GULL_JITTER_TIME 0.012f /* the jitter picks a new target that often (s), and glides to it */

static const param_desc_t *const P = gull_params;

const param_desc_t gull_params[GULL_P_COUNT] = {
    PARAM(GULL_P_CALLS, "calls", "gull.calls", 1.0f, 16.0f, 3.0f, PARAM_INT, "", 0, NULL,
          "syllables in one call (1 = a single 'kee-ow', 6 = a laugh); random varies it from call to call"),
    PARAM(GULL_P_LOOP, "loop", "gull.loop", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "on: the bird keeps calling while the key is held, with pauses between its calls"),
    PARAM(GULL_P_SYLLABLE, "syllable", "gull.syllable", 0.04f, 1.5f, 0.24f, PARAM_EXP, "s", 1, NULL,
          "length of the first syllable"),
    PARAM(GULL_P_GAP, "gap", "gull.gap", 0.0f, 1.0f, 0.07f, PARAM_LIN, "s", 1, NULL, "silence between syllables"),
    PARAM(GULL_P_ACCEL, "accel", "gull.accel", -0.3f, 0.3f, 0.08f, PARAM_LIN, "", 1, NULL,
          "each syllable shorter than the last by this fraction (> 0: the laugh speeds up)"),
    PARAM(GULL_P_RISE, "rise", "gull.rise", 0.0f, 12.0f, 5.0f, PARAM_LIN, "st", 1, NULL,
          "the jump up at the start of each syllable"),
    PARAM(GULL_P_FALL, "fall", "gull.fall", 0.0f, 24.0f, 9.0f, PARAM_LIN, "st", 1, NULL,
          "the slide down after the peak, below the note"),
    PARAM(GULL_P_PEAK, "peak", "gull.peak", 0.05f, 0.8f, 0.25f, PARAM_LIN, "", 1, NULL,
          "where the pitch peaks in the syllable (0.25 = early: 'KEE-ow')"),
    PARAM(GULL_P_ROUGH, "roughness", "gull.roughness", 0.0f, 1.0f, 0.35f, PARAM_LIN, "", 1, NULL,
          "subharmonic (period doubling): a hoarse, breaking voice"),
    PARAM(GULL_P_JITTER, "jitter", "gull.jitter", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL, "unsteady pitch"),
    PARAM(GULL_P_NOISE, "noise", "gull.noise", 0.0f, 1.0f, 0.15f, PARAM_LIN, "", 1, NULL, "breath noise"),
    PARAM(GULL_P_PW, "pulse_width", "osc1.pulse_width", 0.05f, 0.5f, 0.18f, PARAM_LIN, "", 1, NULL,
          "narrow = nasal and thin, 0.5 = hollow"),
    PARAM(GULL_P_FORMANT, "formant", "filter.cutoff", 300.0f, 4000.0f, 1500.0f, PARAM_EXP, "Hz", 1, NULL,
          "size of the throat: the formants scale with it (1 kHz = a human voice, higher = a smaller bird)"),
    PARAM(GULL_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "formant sharpness"),
    PARAM(GULL_P_VOWEL, "vowel", "filter.character", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "vowel at the start of a syllable (0 a, 0.25 e, 0.5 i, 0.75 o, 1 u)"),
    PARAM(GULL_P_VOWEL_END, "vowel_end", "gull.vowel_end", 0.0f, 1.0f, 0.1f, PARAM_LIN, "", 1, NULL,
          "vowel at its end ('ee' -> 'ow')"),
    PARAM(GULL_P_RANDOM, "random", "gull.random", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 1, NULL,
          "how much the birds improvise: each syllable's length, gap, contour and pitch, the number of syllables "
          "of each call, short 'kek's among the long ones, the pauses between calls (loop)"),
    PARAM(GULL_P_RELEASE, "amp_release", "amp.release", 0.005f, 5.0f, 0.08f, PARAM_EXP, "s", 1, NULL,
          "fade after the key is released"),
    PARAM(GULL_P_VOLUME, "volume", "output.volume", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(GULL_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 0, NULL,
          "random stereo position per bird"),
    PARAM(GULL_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
};

static void syllable_start(gull_engine_t *e, gull_voice_t *v, int first)
{
    const float *p = e->p, r = p[GULL_P_RANDOM];
    /* each syllable shorter (accel > 0) than the last one's nominal length, then varied */
    v->len_base = first ? p[GULL_P_SYLLABLE] : dsp_clampf(v->len_base * (1.0f - p[GULL_P_ACCEL]), 0.04f, 2.0f);
    v->len = dsp_clampf(v->len_base * (1.0f + 0.3f * r * dsp_rand_bipolar(&v->rng)), 0.04f, 2.0f);
    v->gap = dsp_maxf(p[GULL_P_GAP] * (1.0f + 0.6f * r * dsp_rand_bipolar(&v->rng)), 0.0f);
    const float vel = 0.7f + 0.6f * v->vel;
    v->rise = p[GULL_P_RISE] * vel * (1.0f + 0.4f * r * dsp_rand_bipolar(&v->rng));
    v->fall = p[GULL_P_FALL] * (1.0f + 0.3f * r * dsp_rand_bipolar(&v->rng));
    v->offset = 1.5f * r * dsp_rand_bipolar(&v->rng);
    v->t = 0.0f;
    if (first) { /* a new call: how many syllables this time */
        const float calls = p[GULL_P_CALLS];
        const int k = (int)(calls + 0.6f * r * calls * dsp_rand_bipolar(&v->rng) + 0.5f);
        v->calls_n = k < 1 ? 1 : k;
    } else if (dsp_rand_unipolar(&v->rng) < 0.35f * r) { /* now and then a short, dry 'kek' */
        v->len = dsp_maxf(0.4f * v->len, 0.04f);
        v->rise *= 0.4f, v->fall *= 0.3f, v->gap *= 0.5f;
    }
}

/* pitch contour of a syllable at u (0..1), semitones from the note: a quick jump from a little below up to rise
 * (eased), then a slide down that speeds up, to -fall */
static float contour(const gull_voice_t *v, float peak, float u)
{
    if (u < peak) {
        const float w = u / peak;
        return v->rise * (-0.3f + 1.3f * w * (2.0f - w));
    }
    const float w = (u - peak) / (1.0f - peak);
    return v->rise - (v->rise + v->fall) * w * (0.4f + 0.6f * w);
}

/* loudness of a syllable at u: a fast onset (the 'k'), held, then a fade over its last third */
static float syllable_amp(float u)
{
    if (u <= 0.0f || u >= 1.0f)
        return 0.0f;
    const float a = dsp_minf(u * (1.0f / 0.06f), 1.0f), d = dsp_minf((1.0f - u) * (1.0f / 0.35f), 1.0f);
    return a * d * d;
}

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    gull_engine_t *e = eng;
    gull_voice_t *v = &e->v[vi];
    v->note = note;
    v->vel = vel;
    v->rng = dsp_seed(dsp_rand_u32(&e->rng));
    if (hard) {
        osc_reset(&v->osc, 0.0f);
        vfilter_reset(&v->filter);
        adsr_reset(&v->env);
        voice_pan(e->p[GULL_P_SPREAD] * dsp_rand_bipolar(&e->rng), &v->pan_l, &v->pan_r);
        v->gain_prev = v->amp_prev = 0.0f;
        v->sub_phase = 0.0f;
        v->jitter = v->jitter_target = 0.0f;
    }
    v->syllable = 0;
    v->calling = 1;
    syllable_start(e, v, 1);
    adsr_gate(&v->env, 1);
}

static void voice_release(void *eng, int vi) { adsr_gate(&((gull_engine_t *)eng)->v[vi].env, 0); }
static float voice_level(const void *eng, int vi)
{
    const gull_voice_t *v = &((const gull_engine_t *)eng)->v[vi];
    return v->env.level * (v->calling ? 1.0f : 0.0f);
}
static void voice_silence(void *eng, int vi)
{
    gull_voice_t *v = &((gull_engine_t *)eng)->v[vi];
    adsr_reset(&v->env);
    v->calling = 0;
}

static const vpoly_ops_t gull_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

static void gull_init(void *st, float sr)
{
    gull_engine_t *e = st;
    e->sr = sr;
    for (int i = 0; i < GULL_P_COUNT; i++)
        e->p[i] = P[i].def;
    e->bend = 0.0f;
    e->env_dirty = 1;
    e->rng = dsp_seed(0x6C11u);
    e->nan_count = 0;
    for (int i = 0; i < GULL_VOICES; i++) {
        gull_voice_t *v = &e->v[i];
        osc_init(&v->osc, sr);
        osc_set_wave(&v->osc, OSC_PULSE);
        vfilter_init(&v->filter, sr);
        adsr_init(&v->env, sr);
        v->sub_phase = v->t = v->len = v->len_base = v->gap = v->rise = v->fall = v->offset = 0.0f;
        v->syllable = v->calling = 0;
        v->calls_n = 1;
        v->amp_prev = v->jitter = v->jitter_target = 0.0f;
        v->vel = 1.0f;
        v->gain_prev = 0.0f;
        v->pan_l = v->pan_r = VOICE_CENTER;
        v->rng = dsp_seed((uint32_t)i + 1u);
        v->note = 72;
    }
    vpoly_init(&e->poly, GULL_VOICES, sr, &gull_poly_ops, e);
}

static vpoly_t *gull_poly(void *st) { return &((gull_engine_t *)st)->poly; }

static void gull_set_param(void *st, int id, float norm)
{
    gull_engine_t *e = st;
    if (id < 0 || id >= GULL_P_COUNT)
        return;
    e->p[id] = param_from_norm(&P[id], norm);
    if (id == GULL_P_RELEASE)
        e->env_dirty = 1;
}

static void gull_pitch_bend(void *st, float bend) { ((gull_engine_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

static DSP_FAST_CODE void voice_render(gull_engine_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    gull_voice_t *v = &e->v[vi];
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK];
    const float dtb = (float)n / e->sr;

    /* the syllable sequencer: syllable, then its gap, then the next one (or the end of the call) */
    float amp = 0.0f, u = 1.0f;
    if (v->calling) {
        v->t += dtb;
        if (v->t > v->len + v->gap) {
            v->syllable++;
            if (v->syllable < v->calls_n) {
                syllable_start(e, v, 0);
            } else if (p[GULL_P_LOOP] < 0.5f) {
                v->calling = 0; /* the call is over */
            } else { /* looping: a new call after a pause of irregular length (t < 0: silent) */
                const float r = p[GULL_P_RANDOM];
                v->syllable = 0;
                syllable_start(e, v, 1);
                v->t = -(2.0f * p[GULL_P_GAP] + p[GULL_P_SYLLABLE] * (1.0f + 4.0f * r * dsp_rand_unipolar(&v->rng)));
            }
        }
        u = dsp_clampf(v->t / v->len, 0.0f, 1.0f);
        amp = v->calling && v->t > 0.0f ? syllable_amp(u) : 0.0f;
    }

    /* jitter: a new random target every few milliseconds, glided to */
    if (dsp_rand_unipolar(&v->rng) < dtb / GULL_JITTER_TIME)
        v->jitter_target = GULL_JITTER_ST * p[GULL_P_JITTER] * dsp_rand_bipolar(&v->rng);
    v->jitter += 0.3f * (v->jitter_target - v->jitter);

    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float c = contour(v, p[GULL_P_PEAK], u);
    const float pitch = (float)v->note + e->bend * p[GULL_P_BEND_RANGE] + vm[VM_PITCH] + v->offset + c + v->jitter;
    const float f = dsp_clampf(fm_mtof(pitch), 20.0f, 0.45f * e->sr);
    osc_set(&v->osc, f, p[GULL_P_PW]);
    osc_process(&v->osc, x, n);

    /* roughness: amplitude modulation at half the pitch (period doubling), then breath noise; the syllable's
     * loudness ramps over the block */
    const float rough = p[GULL_P_ROUGH], dsub = 0.5f * f / e->sr, noise = 0.8f * p[GULL_P_NOISE];
    const float a0 = v->amp_prev, da = (amp - a0) * (1.0f / (float)SYNTH_BLOCK);
    float ph = v->sub_phase, a = a0;
    for (int i = 0; i < n; i++) {
        ph += dsub;
        ph -= (float)(int)ph;
        a += da;
        x[i] = a * (x[i] * (1.0f + rough * fm_sin_pos(ph)) + noise * dsp_rand_bipolar(&v->rng));
    }
    v->sub_phase = ph;
    v->amp_prev = amp;

    /* the throat: a vowel formant filter moving from 'ee' to 'ow', a little brighter at the top of the jump */
    const float vowel = p[GULL_P_VOWEL] + (p[GULL_P_VOWEL_END] - p[GULL_P_VOWEL]) * u;
    const float size = p[GULL_P_FORMANT] * fm_exp2((c * (1.0f / 24.0f)) + vm[VM_BRIGHT]);
    vfilter_set(&v->filter, VF_FORMANT, dsp_clampf(size, 100.0f, 0.4f * e->sr), 0.7f, p[GULL_P_RESO],
                dsp_clampf(vowel + 0.25f * vm[VM_TIMBRE], 0.0f, 1.0f));
    vfilter_process(&v->filter, x, x, n);

    adsr_process(&v->env, env, n);
    const float g1 = GULL_GAIN * fm_db2lin(p[GULL_P_VOLUME]) * (0.4f + 0.6f * v->vel);
    dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
    v->gain_prev = g1;
    voice_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, vi, adsr_active(&v->env) && (v->calling || amp > 0.0f));
}

static DSP_FAST_CODE void gull_render(void *st, float *L, float *R)
{
    gull_engine_t *e = st;
    if (e->env_dirty) {
        for (int i = 0; i < GULL_VOICES; i++)
            adsr_set(&e->v[i].env, 0.002f, 0.01f, 1.0f, e->p[GULL_P_RELEASE]);
        e->env_dirty = 0;
    }
    for (int vi = 0; vi < GULL_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* signal chain, for UIs (engine.h) */
static const engine_stage_t gull_stages[] = {
    {"call", "Call", "mod", "calls loop syllable gap accel rise fall peak random", "", "voice", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "source", "pulse_width roughness jitter noise", "", NULL, NULL, 0.0f, 0.0f},
    {"throat", "Throat", "filter", "formant resonance vowel vowel_end", "voice", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "amp_release volume pan_spread", "throat", NULL, NULL, 0.0f, 0.0f},
    {"keys", "Keys", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t gull_engine = {
    .name = "gull",
    .description = "seabird calls: each note a series of syllables jumping up then sliding down, a nasal pulse "
                   "through a moving vowel formant, hoarse subharmonic, jitter and breath; several notes make a "
                   "colony (8 voices)",
    .params = gull_params,
    .param_count = GULL_P_COUNT,
    .voices = GULL_VOICES,
    .init = gull_init,
    .poly = gull_poly,
    .set_param = gull_set_param,
    .pitch_bend = gull_pitch_bend,
    .render = gull_render,
    .stages = gull_stages,
    .stage_count = (int)(sizeof gull_stages / sizeof gull_stages[0]),
};
