/* mono.c — classic monosynth engine (see mono.h). One voice:
 * pitch -> 3 free-running oscillators (osc 3 can ignore the keyboard and act as a modulator) -> mixer (with
 * noise and the output fed back; levels above ~0.7 start driving the filter input) -> 24 dB ladder (2x
 * oversampled, no passband compensation: emphasis thins the bass) -> loudness contour.
 * Waveforms: tri, shark (tri/saw blend; reverse saw on osc 3), saw, square, wide and narrow pulse.
 * Modulation: wheel x mix(osc 3 | filter contour, noise | LFO) -> oscillator pitch and filter cutoff.
 * Pink noise: P. Kellet's three-pole approximation (musicdsp.org archive, 1999). */
#include "mono.h"
#include "../core/fastmath.h"

#define MONO_GAIN 0.22f
#define MONO_FEEDBACK_GAIN 9.0f /* loop gain at feedback 1, volume 0 dB, sustain 1: past ~0.7 the loop takes over */

enum { W_TRI = 0, W_SHARK, W_SAW, W_SQUARE, W_WIDE, W_NARROW };

/* footage; "lo" (sub-audio, for osc 3 as a modulator) comes last so the indices of existing patches stay */
static const char *const range_labels[] = {"32", "16", "8", "4", "2", "lo", NULL};
static const char *const wave_labels[] = {"tri", "shark", "saw", "square", "wide", "narrow", NULL};
static const char *const wave3_labels[] = {"tri", "rsaw", "saw", "square", "wide", "narrow", NULL};
static const char *const mode_labels[] = {"mono", "legato", NULL};
static const char *const glide_mode_labels[] = {"always", "legato", NULL};
static const char *const color_labels[] = {"white", "pink", NULL};
static const char *const mod_a_labels[] = {"osc3", "filter_eg", NULL};
static const char *const mod_b_labels[] = {"noise", "lfo", NULL};
static const char *const lfo_labels[] = {"tri", "square", NULL};
static const char *const prio_labels[] = {"last", "low", "high", NULL};

#define OSC_PARAMS(N, R, W, defrange, defwave, labels, wavehelp)                                                 \
    PARAM(R, "osc" #N "_range", "osc" #N ".octave", 0.0f, 5.0f, defrange, PARAM_INT, "ft", 0, range_labels,       \
          "osc " #N " footage: 32' (two octaves down) .. 2' (two octaves up), 8' = the played note; lo = five "   \
          "octaves under 32', sub-audio (a modulator)"),                                                          \
    PARAM(W, "osc" #N "_wave", "osc" #N ".waveform", 0.0f, 5.0f, defwave, PARAM_INT, "", 0, labels, wavehelp)
#define WAVE_HELP "waveform: tri soft, shark (triangle-sawtooth) warm, saw bright, square hollow, wide/narrow pulse nasal"
#define WAVE3_HELP "waveform: as osc 1 and 2, but a reverse sawtooth (rsaw) replaces the shark, for modulation"

const param_desc_t mono_params[MONO_P_COUNT] = {
    OSC_PARAMS(1, MONO_P_OSC1_RANGE, MONO_P_OSC1_WAVE, 2.0f, 2.0f, wave_labels, "osc 1 " WAVE_HELP),
    PARAM(MONO_P_OSC1_LEVEL, "osc1_level", "osc1.level", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL, "osc 1 level in the mixer"),
    OSC_PARAMS(2, MONO_P_OSC2_RANGE, MONO_P_OSC2_WAVE, 2.0f, 2.0f, wave_labels, "osc 2 " WAVE_HELP),
    PARAM(MONO_P_OSC2_DETUNE, "osc2_detune", "osc2.detune", -7.0f, 7.0f, 0.06f, PARAM_LIN, "st", 1, NULL,
          "osc 2 frequency: a few hundredths = fat beating, 7 = a fifth, -5 = a fourth"),
    PARAM(MONO_P_OSC2_LEVEL, "osc2_level", "osc2.level", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL, "osc 2 level in the mixer"),
    OSC_PARAMS(3, MONO_P_OSC3_RANGE, MONO_P_OSC3_WAVE, 1.0f, 2.0f, wave3_labels, "osc 3 " WAVE3_HELP),
    PARAM(MONO_P_OSC3_DETUNE, "osc3_detune", "osc3.detune", -7.0f, 7.0f, -0.08f, PARAM_LIN, "st", 1, NULL,
          "osc 3 frequency (as a modulator with keytrack off: low ranges give LFO rates)"),
    PARAM(MONO_P_OSC3_LEVEL, "osc3_level", "osc3.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "osc 3 level in the mixer"),
    PARAM(MONO_P_OSC3_KEYTRACK, "osc3_keytrack", "osc3.keytrack", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, param_onoff_labels,
          "off = osc 3 ignores the keyboard (drone, or LFO for the mod wheel when its range is 32')"),
    PARAM(MONO_P_NOISE_LEVEL, "noise_level", "noise.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "noise level in the mixer"),
    PARAM(MONO_P_MIX_DRIVE, "mixer_drive", "filter.drive", 0.0f, 1.0f, 0.35f, PARAM_LIN, "", 1, NULL,
          "how hard the mixer hits the filter: thicker, compressed, warmer (the famous overdrive)"),
    PARAM(MONO_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 18000.0f, 900.0f, PARAM_EXP, "Hz", 1, NULL,
          "filter cutoff: the main tone knob"),
    PARAM(MONO_P_EMPHASIS, "emphasis", "filter.resonance", 0.0f, 1.0f, 0.25f, PARAM_LIN, "", 1, NULL,
          "filter emphasis (resonance); ~0.95 self-oscillates into a pure sine that tracks the keyboard"),
    PARAM(MONO_P_CONTOUR, "contour", "filter.envelope_amount", 0.0f, 5.0f, 2.5f, PARAM_LIN, "oct", 1, NULL,
          "amount of filter contour (octaves)"),
    PARAM(MONO_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.66f, PARAM_LIN, "", 1, NULL,
          "filter follows the keyboard: 0, 1/3, 2/3 (default) or 1"),
    PARAM(MONO_P_FENV_A, "fenv_attack", "filter_env.attack", 0.001f, 10.0f, 0.005f, PARAM_EXP, "s", 1, NULL, "filter contour attack"),
    PARAM(MONO_P_FENV_D, "fenv_decay", "filter_env.decay", 0.005f, 10.0f, 0.35f, PARAM_EXP, "s", 1, NULL, "filter contour decay"),
    PARAM(MONO_P_FENV_S, "fenv_sustain", "filter_env.sustain", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL, "filter contour sustain"),
    PARAM(MONO_P_AMP_A, "amp_attack", "amp.attack", 0.001f, 10.0f, 0.003f, PARAM_EXP, "s", 1, NULL, "loudness contour attack"),
    PARAM(MONO_P_AMP_D, "amp_decay", "amp.decay", 0.005f, 10.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "loudness contour decay"),
    PARAM(MONO_P_AMP_S, "amp_sustain", "amp.sustain", 0.0f, 1.0f, 0.85f, PARAM_LIN, "", 1, NULL, "loudness contour sustain"),
    PARAM(MONO_P_RELEASE, "release", "env.release_switch", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 1, param_onoff_labels,
          "on = both contours release with their decay time after key-up; off = short release"),
    PARAM(MONO_P_GLIDE, "glide", "voice.glide", MONO_GLIDE_OFF, 10.0f, MONO_GLIDE_OFF, PARAM_EXP, "s", 1, NULL,
          "portamento: the time to reach the new note (1 ms = off); exponential, as the original's RC lag"),
    PARAM(MONO_P_VOICE_MODE, "voice_mode", "voice.mode", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, mode_labels,
          "legato = contours retrigger only after all keys are released (the original behaviour); mono = every note"),
    PARAM(MONO_P_MOD_MIX, "mod_mix", "mod.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "modulation mix: 0 = only source A (mod_a: osc 3 or the filter contour), 1 = only source B (mod_b: "
          "noise or the LFO), in between both; the mod wheel sets how much of it is applied"),
    PARAM(MONO_P_MOD_PITCH, "mod_pitch", "mod.to_pitch", 0.0f, 12.0f, 0.3f, PARAM_LIN, "st", 1, NULL,
          "mod wheel -> oscillator pitch depth (semitones at full wheel)"),
    PARAM(MONO_P_MOD_CUTOFF, "mod_cutoff", "mod.to_cutoff", 0.0f, 4.0f, 0.0f, PARAM_LIN, "oct", 1, NULL,
          "mod wheel -> filter cutoff depth (octaves at full wheel)"),
    PARAM(MONO_P_DRIFT, "drift", "voice.drift", 0.0f, 15.0f, 3.0f, PARAM_LIN, "cent", 0, NULL, "analog drift: slow random wander of each oscillator (standard deviation, cents)"),
    PARAM(MONO_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 0, NULL,
          "velocity -> loudness and brightness (the original had none)"),
    PARAM(MONO_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(MONO_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range (the original wheel: 7, a fifth)"),
    PARAM(MONO_P_NOISE_COLOR, "noise_color", "noise.color", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, color_labels,
          "white = equal energy per frequency (hiss), pink = equal energy per octave (waterfall); as a modulation "
          "source the noise is one step darker (pink, or red)"),
    PARAM(MONO_P_MOD_A, "mod_a", "mod.source_a", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, mod_a_labels,
          "modulation source A: osc 3 (vibrato, trills, FM) or the filter contour (brass and percussion sweeps)"),
    PARAM(MONO_P_MOD_B, "mod_b", "mod.source_b", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, mod_b_labels,
          "modulation source B: noise (random growl) or the LFO (vibrato while the three oscillators play)"),
    PARAM(MONO_P_LFO_RATE, "lfo_rate", "lfo.rate", 0.05f, 200.0f, 5.0f, PARAM_EXP, "Hz", 1, NULL,
          "rate of the modulation LFO (source B)"),
    PARAM(MONO_P_LFO_WAVE, "lfo_wave", "lfo.waveform", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, lfo_labels,
          "LFO shape: triangle (vibrato) or square (trill)"),
    PARAM(MONO_P_FEEDBACK, "feedback", "mixer.feedback", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "the output fed back into the mixer (the external input left unplugged): fattens, then overloads; "
          "it follows the volume, like the original"),
    PARAM(MONO_P_PRIORITY, "note_priority", "voice.priority", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, prio_labels,
          "which held key sounds: last played, lowest (the original) or highest"),
    PARAM_OVERSAMPLE(MONO_P_OVERSAMPLE, 1.0f, "oversampling of the nonlinear filter: 1x (lightest), 2x (default: clean drive and resonance), 4x (cleanest self-oscillation and hard drive high up, about twice the filter cost of 2x)"),
    ANALOG_PARAMS(MONO_P_TOLERANCE, MONO_P_DRIFT_TIME, MONO_P_JITTER),
    PARAM(MONO_P_GLIDE_MODE, "glide_mode", "voice.glide_mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, glide_mode_labels,
          "always = every note glides from the one before (the original); legato = only a note tied to the one before glides, the others start on pitch (the slides of a bass line)"),
};

static void set_wave(osc_t *o, osc_t *o2, int w)
{
    switch (w) {
    case W_TRI: osc_set_wave(o, OSC_TRI); osc_set_wave(o2, OSC_TRI); break;
    case W_SHARK: osc_set_wave(o, OSC_TRI); osc_set_wave(o2, OSC_SAW); break;
    case W_SAW: osc_set_wave(o, OSC_SAW); osc_set_wave(o2, OSC_SAW); break;
    default: osc_set_wave(o, OSC_PULSE); osc_set_wave(o2, OSC_PULSE); break;
    }
}

/* glide at or under this is off (the bottom of the glide knob) */
static int glide_on(const float *p) { return p[MONO_P_GLIDE] > 1.5f * MONO_GLIDE_OFF; }

static float wave_pw(int w) { return w == W_WIDE ? 0.3f : w == W_NARROW ? 0.12f : 0.5f; }

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    mono_t *e = eng;
    (void)vi;
    e->slide = 0;
    if (!glide_on(e->p) || !e->has_note || (int)e->p[MONO_P_GLIDE_MODE])
        e->pitch = (float)note;
    e->has_note = 1;
    e->vel = vel;
    if (hard) { /* the oscillators run free (no phase reset): every attack is a little different */
        ladder_reset(&e->filter);
        e->fb_state[0] = e->fb_state[1] = 0.0f;
        adsr_reset(&e->fenv);
        adsr_reset(&e->amp);
    }
    adsr_gate(&e->fenv, 1);
    adsr_gate(&e->amp, 1);
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    mono_t *e = eng;
    (void)vi, (void)vel;
    e->slide = 1;
    if (!glide_on(e->p))
        e->pitch = (float)note;
}

static void voice_release(void *eng, int vi)
{
    mono_t *e = eng;
    (void)vi;
    adsr_gate(&e->fenv, 0);
    adsr_gate(&e->amp, 0);
}

static float voice_level(const void *eng, int vi)
{
    (void)vi;
    return ((const mono_t *)eng)->amp.level;
}

static void voice_silence(void *eng, int vi)
{
    mono_t *e = eng;
    (void)vi;
    adsr_reset(&e->fenv);
    adsr_reset(&e->amp);
}

static const vpoly_ops_t mono_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void mono_init(void *st, float sr)
{
    mono_t *e = st;
    const float ctrl = sr / (float)SYNTH_BLOCK;
    e->sr = sr;
    for (int i = 0; i < MONO_P_COUNT; i++)
        e->p[i] = mono_params[i].def;
    smooth_init(&e->sm_cut, 0.005f, ctrl, fm_log2(e->p[MONO_P_CUTOFF]));
    smooth_init(&e->sm_emph, 0.005f, ctrl, e->p[MONO_P_EMPHASIS]);
    static const int lv[4] = {MONO_P_OSC1_LEVEL, MONO_P_OSC2_LEVEL, MONO_P_OSC3_LEVEL, MONO_P_NOISE_LEVEL};
    for (int k = 0; k < 4; k++) {
        smooth_init(&e->sm_lvl[k], 0.005f, ctrl, e->p[lv[k]]);
        e->lvl_prev[k] = e->p[lv[k]];
    }
    for (int k = 0; k < 3; k++) {
        osc_init(&e->osc[k], sr);
        osc_init(&e->osc_shark[k], sr);
        osc_reset(&e->osc[k], 0.33f * (float)k);
        osc_reset(&e->osc_shark[k], 0.33f * (float)k);
    }
    noise_init(&e->noise, 0x5A5Au);
    e->pink[0] = e->pink[1] = e->pink[2] = 0.0f;
    e->mod_noise = e->lfo_phase = 0.0f;
    e->fb_state[0] = e->fb_state[1] = 0.0f;
    ladder_init(&e->filter, sr);
    ladder_set_compensation(&e->filter, 0.0f); /* a transistor ladder loses its passband as emphasis rises */
    ladder_set_mode(&e->filter, LADDER_LP24);
    ladder_set_oversampling(&e->filter, 2);
    adsr_init(&e->fenv, sr);
    adsr_init(&e->amp, sr);
    for (int k = 0; k < 3; k++)
        analog_init(&e->analog[k], 0x3A3Au + (uint32_t)k);
    e->pitch = 48.0f;
    e->vel = 1.0f;
    e->gain_prev = 0.0f;
    e->bend = e->modwheel = 0.0f;
    e->has_note = 0;
    e->env_dirty = 1;
    e->nan_count = 0;
    vpoly_init(&e->poly, 1, sr, &mono_poly_ops, e);
    vpoly_set_mode(&e->poly, VPOLY_LEGATO);
}

static vpoly_t *mono_poly(void *st) { return &((mono_t *)st)->poly; }

static void mono_set_param(void *st, int id, float norm)
{
    mono_t *e = st;
    if (id < 0 || id >= MONO_P_COUNT)
        return;
    e->p[id] = param_from_norm(&mono_params[id], norm);
    if (!vpoly_active(&e->poly, 0)) { /* silent: nothing to glide, the next note starts on the new values */
        static const int lv[4] = {MONO_P_OSC1_LEVEL, MONO_P_OSC2_LEVEL, MONO_P_OSC3_LEVEL, MONO_P_NOISE_LEVEL};
        smooth_snap(&e->sm_cut, fm_log2(e->p[MONO_P_CUTOFF]));
        smooth_snap(&e->sm_emph, e->p[MONO_P_EMPHASIS]);
        for (int k = 0; k < 4; k++)
            smooth_snap(&e->sm_lvl[k], e->p[lv[k]]);
    }
    if (id == MONO_P_VOICE_MODE)
        vpoly_set_mode(&e->poly, (int)e->p[id] ? VPOLY_LEGATO : VPOLY_MONO);
    if (id == MONO_P_OVERSAMPLE)
        ladder_set_oversampling(&e->filter, 1 << (int)e->p[id]);
    if (id == MONO_P_PRIORITY)
        vpoly_set_priority(&e->poly, (vpoly_priority_t)(int)e->p[id]);
    if ((id >= MONO_P_FENV_A && id <= MONO_P_RELEASE))
        e->env_dirty = 1;
}

static void mono_pitch_bend(void *st, float bend) { ((mono_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }
static void mono_mod_wheel(void *st, float v) { ((mono_t *)st)->modwheel = dsp_clampf(v, 0.0f, 1.0f); }

static DSP_FAST_CODE void mono_render(void *st, float *L, float *R)
{
    mono_t *e = st;
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    float x[SYNTH_BLOCK], t[SYNTH_BLOCK], t2[SYNTH_BLOCK], env[SYNTH_BLOCK];
    if (!vpoly_active(&e->poly, 0))
        return;

    if (e->env_dirty) {
        const int rel = (int)p[MONO_P_RELEASE];
        adsr_set(&e->fenv, p[MONO_P_FENV_A], p[MONO_P_FENV_D], p[MONO_P_FENV_S], rel ? p[MONO_P_FENV_D] : 0.01f);
        adsr_set(&e->amp, p[MONO_P_AMP_A], p[MONO_P_AMP_D], p[MONO_P_AMP_S], rel ? p[MONO_P_AMP_D] : 0.01f);
        e->env_dirty = 0;
    }

    /* pitch: glide, bend; osc 3 / noise as mod-wheel modulator (block rate) */
    const float target = (float)vpoly_note(&e->poly, 0);
    if (glide_on(p) && (e->slide || !(int)p[MONO_P_GLIDE_MODE]) && dsp_absf(target - e->pitch) > 0.002f)
        e->pitch += (target - e->pitch) * voice_glide_arrive_coef(p[MONO_P_GLIDE], n, e->sr);
    else
        e->pitch = target; /* arrived (a tail under a fifth of a cent is not heard) */
    analog_cfg_t acfg;
    analog_cfg(&acfg, p[MONO_P_TOLERANCE], p[MONO_P_DRIFT], p[MONO_P_DRIFT_TIME], p[MONO_P_JITTER], (float)n / e->sr);
    const float *vm = vpoly_vmod(&e->poly, 0); /* per-voice modulation, microtuning, MPE */
    const float base = e->pitch + e->bend * p[MONO_P_BEND_RANGE] + vm[VM_PITCH];
    /* modulation (block rate): source A and source B, balanced by the modulation mix, sent by the wheel */
    float mod_a, mod_b;
    if ((int)p[MONO_P_MOD_A] == 0) {
        /* osc 3 where the previous block left it (at LFO rates its wave sweeps slowly) */
        const int w = (int)p[MONO_P_OSC3_WAVE];
        const float ph = e->osc[2].phase;
        mod_a = w == W_TRI ? 1.0f - 4.0f * dsp_absf(ph - 0.5f)
              : w == W_SHARK ? 1.0f - 2.0f * ph /* reverse sawtooth on osc 3 */
              : w == W_SAW ? 2.0f * ph - 1.0f
              : ph < wave_pw(w) ? 1.0f : -1.0f;
    } else {
        mod_a = e->fenv.level; /* the filter contour, 0..1 */
    }
    {
        /* the noise is always drawn (one value per block, as before) and low-passed: pink-ish when the mixer
         * noise is white, red when it is pink */
        const float w = dsp_rand_bipolar(&e->noise.state);
        const float coef = (int)p[MONO_P_NOISE_COLOR] ? 0.08f : 1.0f;
        e->mod_noise += (w - e->mod_noise) * coef;
        e->lfo_phase += p[MONO_P_LFO_RATE] * (float)n / e->sr;
        e->lfo_phase -= (float)(int)e->lfo_phase;
        const float lfo = (int)p[MONO_P_LFO_WAVE] ? (e->lfo_phase < 0.5f ? 1.0f : -1.0f)
                                                  : 1.0f - 4.0f * dsp_absf(e->lfo_phase - 0.5f);
        mod_b = (int)p[MONO_P_MOD_B] ? lfo : e->mod_noise * ((int)p[MONO_P_NOISE_COLOR] ? 3.0f : 1.0f);
    }
    const float mix = p[MONO_P_MOD_MIX];
    float mod = ((1.0f - mix) * mod_a + mix * mod_b) * e->modwheel;
    const float pitch_mod = mod * p[MONO_P_MOD_PITCH];

    static const int rng[3] = {MONO_P_OSC1_RANGE, MONO_P_OSC2_RANGE, MONO_P_OSC3_RANGE};
    static const int wav[3] = {MONO_P_OSC1_WAVE, MONO_P_OSC2_WAVE, MONO_P_OSC3_WAVE};
    const float detune[3] = {0.0f, p[MONO_P_OSC2_DETUNE], p[MONO_P_OSC3_DETUNE]};
    float drift1 = 0.0f; /* osc 1's wander (semitones): the filter follows half of it, as before */
    for (int k = 0; k < 3; k++) {
        const float imperfect = 0.01f * analog_next(&e->analog[k], &acfg); /* each oscillator drifts on its own */
        if (k == 0)
            drift1 = imperfect;
        /* 32' .. 2' = -2 .. +2 octaves; lo = five octaves under 32' */
        const float oct = (int)p[rng[k]] == 5 ? -84.0f : 12.0f * (p[rng[k]] - 2.0f);
        const int keyed = k < 2 || (int)p[MONO_P_OSC3_KEYTRACK];
        /* osc 3 without keytrack: fixed pitch around C1 shifted by its range (32' -> LFO-like ~8 Hz) */
        const float semis = (keyed ? base + oct + detune[k] + (k < 2 ? pitch_mod : 0.0f) : 24.0f + oct + detune[k]) + imperfect;
        float f = fm_mtof(semis);
        if (!keyed && (int)p[rng[k]] == 0)
            f *= 0.25f; /* 32' with keytrack off: sub-audio modulator */
        set_wave(&e->osc[k], &e->osc_shark[k], (int)p[wav[k]]);
        osc_set(&e->osc[k], f, wave_pw((int)p[wav[k]]));
        osc_set(&e->osc_shark[k], f, wave_pw((int)p[wav[k]]));
    }

    /* mixer */
    const float lvl[4] = {smooth_next(&e->sm_lvl[0], p[MONO_P_OSC1_LEVEL]), smooth_next(&e->sm_lvl[1], p[MONO_P_OSC2_LEVEL]),
                          smooth_next(&e->sm_lvl[2], p[MONO_P_OSC3_LEVEL]), smooth_next(&e->sm_lvl[3], p[MONO_P_NOISE_LEVEL])};
    for (int i = 0; i < n; i++)
        x[i] = 0.0f;
    for (int k = 0; k < 3; k++) {
        osc_process(&e->osc[k], t, n); /* always run: osc 3 may be the modulator */
        if ((int)p[wav[k]] == W_SHARK && k < 2) {
            osc_process(&e->osc_shark[k], t2, n);
            for (int i = 0; i < n; i++)
                t[i] = 0.6f * t[i] + 0.4f * t2[i];
        } else if ((int)p[wav[k]] == W_SHARK) { /* osc 3: reverse sawtooth */
            for (int i = 0; i < n; i++)
                t[i] = -t[i];
        }
        if (lvl[k] > 0.0f || e->lvl_prev[k] > 0.0f)
            dsp_ramp_mac(x, t, e->lvl_prev[k], lvl[k], n);
    }
    if (lvl[3] > 0.0f || e->lvl_prev[3] > 0.0f) {
        noise_process(&e->noise, t, n);
        if ((int)p[MONO_P_NOISE_COLOR]) { /* pink: -3 dB per octave, level matched to the white */
            float b0 = e->pink[0], b1 = e->pink[1], b2 = e->pink[2];
            for (int i = 0; i < n; i++) {
                const float w = t[i];
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                t[i] = 0.25f * (b0 + b1 + b2 + w * 0.1848f);
            }
            e->pink[0] = b0, e->pink[1] = b1, e->pink[2] = b2;
        }
        dsp_ramp_mac(x, t, e->lvl_prev[3], lvl[3], n);
    }
    for (int k = 0; k < 4; k++)
        e->lvl_prev[k] = lvl[k];

    /* filter: cutoff (oct) = base + contour + keytrack (from C4) + mod wheel + velocity */
    adsr_process(&e->fenv, env, n);
    const float vel_term = p[MONO_P_VELOCITY] * (e->vel - 1.0f);
    const float cut = smooth_next(&e->sm_cut, fm_log2(p[MONO_P_CUTOFF])) + p[MONO_P_CONTOUR] * env[n - 1] +
                      p[MONO_P_KEYTRACK] * (e->pitch - 60.0f) / 12.0f + mod * p[MONO_P_MOD_CUTOFF] + 1.5f * vel_term + drift1 * 0.5f +
                      vm[VM_BRIGHT];
    ladder_set(&e->filter, fm_exp2(cut), smooth_next(&e->sm_emph, p[MONO_P_EMPHASIS]), p[MONO_P_MIX_DRIVE]);

    /* loudness contour, and the output fed back into the mixer when asked: the loop goes around the filter
     * and the amplifier, so it follows the contour and the volume (as on the instrument) */
    adsr_process(&e->amp, env, n);
    const float g1 = MONO_GAIN * fm_db2lin(p[MONO_P_VOLUME]) * (1.0f + vel_term);
    if (p[MONO_P_FEEDBACK] > 0.0f) {
        const float dg = (g1 - e->gain_prev) / (float)n;
        float g = e->gain_prev;
        for (int i = 0; i < n; i++) {
            g += dg;
            env[i] *= g; /* env becomes the whole post gain */
        }
        const float hp = 1.0f - fm_exp2(-FM_LOG2E * FM_TWO_PI * 20.0f / e->sr); /* AC coupling at 20 Hz */
        ladder_process_fb(&e->filter, x, env, MONO_FEEDBACK_GAIN * p[MONO_P_FEEDBACK] / MONO_GAIN, hp, e->fb_state, x, n);
        for (int i = 0; i < n; i++)
            x[i] *= env[i];
    } else {
        ladder_process(&e->filter, x, x, n);
        dsp_ramp_env_mul(x, env, e->gain_prev, g1, n);
    }
    e->gain_prev = g1;
    voice_out(&e->poly, 0, x, n, VOICE_CENTER, VOICE_CENTER, L, R, &e->nan_count);
    vpoly_end_block(&e->poly, 0, adsr_active(&e->amp));
}

/* signal chain, for UIs (engine.h) */
static const engine_stage_t mono_stages[] = {
    {"osc1", "Osc 1", "source", "osc1_", "", NULL, "osc1_level", 0.0f, 0.8f},
    {"osc2", "Osc 2", "source", "osc2_", "", NULL, "osc2_level", 0.0f, 0.7f},
    {"osc3", "Osc 3", "source", "osc3_", "", NULL, "osc3_level", 0.0f, 0.7f},
    {"noise", "Noise", "source", "noise_", "", NULL, "noise_level", 0.0f, 0.3f},
    {"mixer", "Mixer", "filter", "mixer_drive feedback", "osc1 osc2 osc3 noise", NULL, NULL, 0.0f, 0.0f},
    {"filter", "Filter", "filter", "cutoff emphasis contour keytrack oversample", "mixer", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Loudness", "amp", "amp_ volume velocity", "filter", NULL, NULL, 0.0f, 0.0f},
    {"fenv", "Filter contour", "mod", "fenv_ release", "", "filter", NULL, 0.0f, 0.0f},
    {"mod", "Modulation", "mod", "mod_ lfo_", "", "osc1 osc2 filter", NULL, 0.0f, 0.0f},
    {"voice", "Keyboard", "voice", "glide glide_mode voice_mode note_priority bend_range drift drift_time tolerance jitter", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t mono_engine = {
    .name = "mono",
    .description = "classic monosynth (the early-70s portable one, panel by panel): 3 free-running oscillators with "
                   "footage and 6 waveforms, osc 3 as modulator, white / pink noise, mixer that overloads and takes "
                   "the output back in, 24 dB self-oscillating ladder (2x oversampled), ADS contours, modulation "
                   "mix (osc 3 or filter contour, noise or LFO), glide, legato, note priority",
    .params = mono_params,
    .param_count = MONO_P_COUNT,
    .voices = 1,
    .init = mono_init,
    .poly = mono_poly,
    .set_param = mono_set_param,
    .pitch_bend = mono_pitch_bend,
    .mod_wheel = mono_mod_wheel,
    .render = mono_render,
    .stages = mono_stages,
    .stage_count = (int)(sizeof mono_stages / sizeof mono_stages[0]),
};
