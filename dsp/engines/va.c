/* va.c — subtractive engine (see va.h).
 * Per block: parameter smoothing, LFO, then for each voice pitch -> oscillators,
 * filter envelope -> cutoff (in octaves), mix, the two filter slots, amp, pan.
 * Everything affecting gain, cutoff or pitch is interpolated over the block. */
#include "va.h"
#include "../core/fastmath.h"

#define VA_VOICE_GAIN 0.125f /* -18 dBFS per voice */
#define VA_SMOOTH_S 0.005f

static const char *const osc_wave_labels[] = {"sine", "saw", "pulse", "tri", NULL};
static const char *const filter_mode_labels[] = {"lp", "bp", "hp", "notch", "formant", "scream", NULL};
static const char *const filter2_labels[] = {"off", "lp", "bp", "hp", "notch", "formant", "scream", NULL};
static const char *const routing_labels[] = {"series", "parallel", NULL};

const param_desc_t va_params[VA_P_COUNT] = {
    PARAM(VA_P_OSC1_WAVE, "osc1_wave", "osc1.waveform", 0.0f, 3.0f, 1.0f, PARAM_INT, "", 0, osc_wave_labels, "osc 1 waveform: saw = bright/brassy, pulse = hollow (square at pw 0.5), tri = soft flute, sine = pure; osc1_unison stacks detuned copies of it"),
    PARAM(VA_P_OSC1_OCT, "osc1_oct", "osc1.octave", -3.0f, 3.0f, 0.0f, PARAM_INT, "oct", 0, NULL, "osc 1 octave shift"),
    PARAM(VA_P_OSC1_SEMI, "osc1_semi", "osc1.semitone", -12.0f, 12.0f, 0.0f, PARAM_INT, "st", 0, NULL, "osc 1 transposition in semitones (7 = fifth, 12 = octave)"),
    PARAM(VA_P_OSC1_DETUNE, "osc1_detune", "osc1.detune", -50.0f, 50.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "osc 1 fine tune; detuning against osc 2 gives beating/chorus (5-12 cents = lush)"),
    PARAM(VA_P_OSC1_PW, "osc1_pw", "osc1.pulse_width", 0.05f, 0.95f, 0.5f, PARAM_LIN, "", 1, NULL, "osc 1 pulse width (pulse wave only): 0.5 square, small/large values thin and nasal"),
    PARAM(VA_P_OSC1_LEVEL, "osc1_level", "osc1.level", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL, "osc 1 level into the filter"),
    PARAM(VA_P_OSC2_WAVE, "osc2_wave", "osc2.waveform", 0.0f, 3.0f, 1.0f, PARAM_INT, "", 0, osc_wave_labels, "osc 2 waveform"),
    PARAM(VA_P_OSC2_OCT, "osc2_oct", "osc2.octave", -3.0f, 3.0f, 0.0f, PARAM_INT, "oct", 0, NULL, "osc 2 octave shift (-1 = sub oscillator for bass weight)"),
    PARAM(VA_P_OSC2_SEMI, "osc2_semi", "osc2.semitone", -12.0f, 12.0f, 0.0f, PARAM_INT, "st", 0, NULL, "osc 2 transposition in semitones"),
    PARAM(VA_P_OSC2_DETUNE, "osc2_detune", "osc2.detune", -50.0f, 50.0f, 7.0f, PARAM_LIN, "cent", 1, NULL, "osc 2 fine tune (default +7 cents: gentle chorus)"),
    PARAM(VA_P_OSC2_PW, "osc2_pw", "osc2.pulse_width", 0.05f, 0.95f, 0.5f, PARAM_LIN, "", 1, NULL, "osc 2 pulse width"),
    PARAM(VA_P_OSC2_LEVEL, "osc2_level", "osc2.level", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL, "osc 2 level (0 = single oscillator, cleaner)"),
    PARAM(VA_P_NOISE_LEVEL, "noise_level", "noise.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "white noise level: breath, air, percussive transients (with short amp envelope)"),
    PARAM(VA_P_FILTER_MODE, "filter_mode", "filter.type", 0.0f, 5.0f, 0.0f, PARAM_INT, "", 0, filter_mode_labels, "filter type: lp = classic subtractive, bp = nasal/vocal, hp = thin, notch = phasey, formant = vowels (cutoff 1 kHz = adult voice, filter_character = vowel a..u), scream = screaming 2-pole low-pass (filter_character = drive)"),
    PARAM(VA_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 20000.0f, 2000.0f, PARAM_EXP, "Hz", 1, NULL, "filter cutoff (base, before envelope/keytrack/velocity/LFO): main brightness control"),
    PARAM(VA_P_RESO, "resonance", "filter.resonance", 0.5f, 25.0f, 1.0f, PARAM_EXP, "Q", 1, NULL, "filter Q: 0.7 neutral, 2-5 vocal peak, 8-25 squelchy/whistling (acid)"),
    PARAM(VA_P_FENV_AMT, "fenv_amount", "filter.envelope_amount", -8.0f, 8.0f, 2.5f, PARAM_LIN, "oct", 1, NULL, "filter envelope depth in octaves (+4 = big 'pew', negative = inverted sweep)"),
    PARAM(VA_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "cutoff follows the keyboard (1 = one octave per octave): keeps high notes bright"),
    PARAM(VA_P_VEL_CUTOFF, "vel_cutoff", "filter.velocity", 0.0f, 4.0f, 1.0f, PARAM_LIN, "oct", 1, NULL, "octaves of cutoff lost at low velocity: expressive brightness"),
    PARAM(VA_P_AMP_A, "amp_attack", "amp.attack", 0.0005f, 30.0f, 0.005f, PARAM_EXP, "s", 1, NULL, "amp attack: <5 ms percussive, 20-80 ms soft, >0.3 s swelling pad"),
    PARAM(VA_P_AMP_D, "amp_decay", "amp.decay", 0.0005f, 30.0f, 0.3f, PARAM_EXP, "s", 1, NULL, "amp decay time to the sustain level"),
    PARAM(VA_P_AMP_S, "amp_sustain", "amp.sustain", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL, "amp sustain level: 0 = plucked/percussive, 1 = organ-like"),
    PARAM(VA_P_AMP_R, "amp_release", "amp.release", 0.0005f, 30.0f, 0.3f, PARAM_EXP, "s", 1, NULL, "amp release after note-off: short = tight, long = ambient tail"),
    PARAM(VA_P_FENV_A, "fenv_attack", "filter_env.attack", 0.0005f, 30.0f, 0.002f, PARAM_EXP, "s", 1, NULL, "filter envelope attack (slow = filter opens progressively)"),
    PARAM(VA_P_FENV_D, "fenv_decay", "filter_env.decay", 0.0005f, 30.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "filter envelope decay: 0.1-0.25 s = plucky/acid, 1-3 s = evolving"),
    PARAM(VA_P_FENV_S, "fenv_sustain", "filter_env.sustain", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL, "filter envelope sustain level"),
    PARAM(VA_P_FENV_R, "fenv_release", "filter_env.release", 0.0005f, 30.0f, 0.4f, PARAM_EXP, "s", 1, NULL, "filter envelope release"),
    PARAM(VA_P_LFO_RATE, "lfo_rate", "lfo.rate", 0.01f, 30.0f, 5.0f, PARAM_EXP, "Hz", 1, NULL, "global LFO rate (vibrato 4.5-6.5 Hz, slow movement 0.1-1 Hz)"),
    PARAM(VA_P_LFO_WAVE, "lfo_wave", "lfo.waveform", 0.0f, 4.0f, 0.0f, PARAM_INT, "", 0, param_lfo_wave_labels, "LFO shape (sh = random steps)"),
    PARAM(VA_P_LFO_PITCH, "lfo_pitch", "lfo.to_pitch", 0.0f, 12.0f, 0.0f, PARAM_LIN, "st", 1, NULL, "LFO -> pitch depth in semitones (0.1-0.3 = vibrato, more = siren)"),
    PARAM(VA_P_LFO_CUTOFF, "lfo_cutoff", "lfo.to_cutoff", 0.0f, 4.0f, 0.0f, PARAM_LIN, "oct", 1, NULL, "LFO -> cutoff depth in octaves (wah / slow breathing)"),
    PARAM(VA_P_LFO_PWM, "lfo_pwm", "lfo.to_pulse_width", 0.0f, 0.45f, 0.0f, PARAM_LIN, "", 1, NULL, "LFO -> pulse width (PWM: animated, string-ensemble-like pulse sound)"),
    PARAM(VA_P_WHEEL_VIB, "wheel_vibrato", "lfo.wheel_vibrato", 0.0f, 1.0f, 0.3f, PARAM_LIN, "st", 1, NULL, "extra vibrato depth (semitones) added by the mod wheel (CC 1)"),
    PARAM(VA_P_AMP_VEL, "amp_velocity", "amp.velocity", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL, "velocity -> loudness sensitivity (0 = none, 1 = full)"),
    PARAM(VA_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume in dB (voices already sit at -18 dBFS each)"),
    PARAM(VA_P_DRIFT, "drift", "voice.drift", 0.0f, 20.0f, 3.0f, PARAM_LIN, "cent", 0, NULL, "analog drift: slow random wander of each oscillator (standard deviation, cents; the filter follows a little)"),
    PARAM(VA_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 0, NULL, "random stereo position per voice (0 = mono center, 1 = wide)"),
    PARAM(VA_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range in semitones"),
    PARAM(VA_P_LFO2_RATE, "lfo2_rate", "motion.detune1.rate", 0.02f, 10.0f, 0.3f, PARAM_EXP, "Hz", 1, NULL, "per-voice detune LFO rate (0.05-0.5 Hz = slow shimmering beating)"),
    PARAM(VA_P_LFO2_DETUNE, "lfo2_detune", "motion.detune1.depth", 0.0f, 50.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "LFO 2 depth on osc 2 detune in cents: moving chorus/beating, the classic lush detuned pad"),
    PARAM(VA_P_LFO2_SPREAD, "lfo2_spread", "motion.spread", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL, "per-voice randomness of phase and rate of LFOs 2, 3, 4 (+/-40 % at 1): chord notes beat independently"),
    PARAM(VA_P_LFO2_RATE_MOD, "lfo2_rate_mod", "motion.meta_rate", 0.0f, 3.0f, 0.0f, PARAM_LIN, "oct", 1, NULL, "LFO 1 -> rate of LFOs 2/3/4 in octaves, in opposite directions (LFO of LFO: beating speeds up/slows down, LFOs drift apart)"),
    PARAM(VA_P_LFO2_DEPTH_MOD, "lfo2_depth_mod", "motion.meta_depth", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "LFO 1 -> depth of LFOs 2/3/4 (LFO of LFO: the detune swells and settles)"),
    /* default rates in irrational ratios to LFO 2 (x0.764, x0.236): no common period */
    PARAM(VA_P_LFO3_RATE, "lfo3_rate", "motion.detune2.rate", 0.02f, 10.0f, 0.229f, PARAM_EXP, "Hz", 1, NULL, "per-voice LFO 3 rate (default ~0.76 x LFO 2: never in sync)"),
    PARAM(VA_P_LFO3_DETUNE, "lfo3_detune", "motion.detune2.depth", 0.0f, 50.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "LFO 3 depth on osc 1 detune in cents: second, desynchronized beating layer"),
    PARAM(VA_P_LFO4_RATE, "lfo4_rate", "motion.detune3.rate", 0.02f, 10.0f, 0.0708f, PARAM_EXP, "Hz", 1, NULL, "per-voice LFO 4 rate (default ~0.24 x LFO 2: very slow)"),
    PARAM(VA_P_LFO4_DETUNE, "lfo4_detune", "motion.detune3.depth", 0.0f, 50.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "LFO 4 depth on the whole voice pitch in cents: chord notes slowly drift against each other (tape-like)"),
    PARAM(VA_P_OSC1_UNISON, "osc1_unison", "osc1.unison_voices", 1.0f, 16.0f, 1.0f, PARAM_INT, "", 0, NULL, "osc 1 unison: copies of the oscillator sounding together, detuned and spread (1 = a single oscillator; 3-5 = thick, 7-9 = a classic stack, 12-16 = a wall; each voice costs an oscillator)"),
    PARAM(VA_P_OSC1_UNI_DETUNE, "osc1_unison_detune", "osc1.unison_detune", 0.0f, 100.0f, 25.0f, PARAM_LIN, "cent", 1, NULL, "how far the outermost unison voice is detuned, in cents; the inner voices stay closer (10-20 = subtle chorus, 25-40 = lush, 60+ = wide, trance)"),
    PARAM(VA_P_OSC1_UNI_BLEND, "osc1_unison_blend", "osc1.unison_blend", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL, "level of the side voices against the centre one, the loudness kept: 0 = the centre alone, 1 = every voice as loud"),
    PARAM(VA_P_OSC1_UNI_WIDTH, "osc1_unison_width", "osc1.unison_width", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "stereo width of the unison voices: 0 = mono (cheaper: one filter per voice), 1 = from hard left to hard right (mono on a board built with VA_STEREO 0)"),
    PARAM(VA_P_OSC2_UNISON, "osc2_unison", "osc2.unison_voices", 1.0f, 16.0f, 1.0f, PARAM_INT, "", 0, NULL, "osc 2 unison: copies of the oscillator sounding together, detuned and spread (1 = a single oscillator; 3-5 = thick, 7-9 = a classic stack, 12-16 = a wall; each voice costs an oscillator)"),
    PARAM(VA_P_OSC2_UNI_DETUNE, "osc2_unison_detune", "osc2.unison_detune", 0.0f, 100.0f, 25.0f, PARAM_LIN, "cent", 1, NULL, "how far the outermost unison voice is detuned, in cents; the inner voices stay closer (10-20 = subtle chorus, 25-40 = lush, 60+ = wide, trance)"),
    PARAM(VA_P_OSC2_UNI_BLEND, "osc2_unison_blend", "osc2.unison_blend", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL, "level of the side voices against the centre one, the loudness kept: 0 = the centre alone, 1 = every voice as loud"),
    PARAM(VA_P_OSC2_UNI_WIDTH, "osc2_unison_width", "osc2.unison_width", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "stereo width of the unison voices: 0 = mono (cheaper: one filter per voice), 1 = from hard left to hard right (mono on a board built with VA_STEREO 0)"),
    PARAM(VA_P_UNI_MOTION, "unison_motion", "osc.unison.motion", 0.0f, 30.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "a slow, desynchronized LFO on each unison voice's detune, in cents: the stack breathes and shimmers"),
    PARAM(VA_P_UNI_MOTION_RATE, "unison_motion_rate", "osc.unison.motion_rate", 0.02f, 5.0f, 0.2f, PARAM_EXP, "Hz", 1, NULL, "average rate of the unison motion LFOs (each voice gets 0.6-1.4x this rate)"),
    PARAM(VA_P_PENV_AMT, "pitch_env", "pitch_env.amount", -48.0f, 48.0f, 0.0f, PARAM_LIN, "st", 1, NULL,
          "pitch envelope: the note starts this many semitones away and glides back (+24..+36 with 30-60 ms = "
          "kick drum, +7..+12 = snare/tom snap, -12..-24 = laser zap)"),
    PARAM(VA_P_PENV_DECAY, "pitch_env_decay", "pitch_env.decay", 0.002f, 2.0f, 0.05f, PARAM_EXP, "s", 1, NULL,
          "time constant of the pitch envelope"),
    PARAM(VA_P_VOICE_MODE, "voice_mode", "voice.mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, param_voice_mode_labels,
          "poly = chords; mono = one voice, last note wins, every note retriggers the envelopes; legato = one "
          "voice, overlapping notes slide without retriggering (acid lines, leads, basses)"),
    PARAM(VA_P_GLIDE, "glide", "voice.glide", 0.0f, 2.0f, 0.0f, PARAM_LIN, "s", 1, NULL,
          "portamento time (constant time, like an RC glide): 0.03-0.08 s = acid slide, 0.2-0.5 s = lazy lead"),
    PARAM(VA_P_GLIDE_MODE, "glide_mode", "voice.glide_mode", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0, param_glide_mode_labels,
          "always = every note glides from the previous one; legato = only overlapping (tied) notes glide"),
    PARAM(VA_P_F1_CHAR, "filter_character", "filter.character", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "filter_mode formant: the vowel (0 a, 0.25 e, 0.5 i, 0.75 o, 1 u); scream: drive into the screaming feedback"),
    PARAM(VA_P_F2_MODE, "filter2_mode", "filter2.type", 0.0f, 6.0f, 0.0f, PARAM_INT, "", 0, filter2_labels,
          "second filter: off, or a type like filter 1 (follows the same envelope, keytracking and modulation)"),
    PARAM(VA_P_F2_CUTOFF, "filter2_cutoff", "filter2.cutoff", 20.0f, 20000.0f, 1000.0f, PARAM_EXP, "Hz", 1, NULL,
          "second filter cutoff (formant: 1 kHz = adult voice)"),
    PARAM(VA_P_F2_RESO, "filter2_resonance", "filter2.resonance", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "second filter resonance"),
    PARAM(VA_P_F2_CHAR, "filter2_character", "filter2.character", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "second filter: vowel (formant) or drive (scream)"),
    PARAM(VA_P_FILTER_ROUTING, "filter_routing", "filter.routing", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, routing_labels,
          "series = filter 1 then filter 2 (e.g. hp then lp = band); parallel = both on the oscillators, mixed "
          "(e.g. two formants = a vowel pair, lp + hp = notch-like)"),
    PARAM_OVERSAMPLE(VA_P_OVERSAMPLE, 1.0f, "oversampling of the scream filter character (the others are linear): "
                     "1x lightest, 2x default, 4x cleanest screaming resonance"),
    ANALOG_PARAMS(VA_P_TOLERANCE, VA_P_DRIFT_TIME, VA_P_JITTER),
};

static const vpoly_ops_t va_poly_ops;

static void va_init(void *st, float sample_rate)
{
    va_t *e = st;
    const float ctrl_rate = sample_rate / (float)SYNTH_BLOCK;
    e->sr = sample_rate;
    for (int i = 0; i < VA_P_COUNT; i++)
        e->p[i] = va_params[i].def;
    smooth_init(&e->sm_cut, VA_SMOOTH_S, ctrl_rate, fm_log2(e->p[VA_P_CUTOFF]));
    smooth_init(&e->sm_res, VA_SMOOTH_S, ctrl_rate, e->p[VA_P_RESO]);
    smooth_init(&e->sm_lvl[0], VA_SMOOTH_S, ctrl_rate, e->p[VA_P_OSC1_LEVEL]);
    smooth_init(&e->sm_lvl[1], VA_SMOOTH_S, ctrl_rate, e->p[VA_P_OSC2_LEVEL]);
    smooth_init(&e->sm_lvl[2], VA_SMOOTH_S, ctrl_rate, e->p[VA_P_NOISE_LEVEL]);
    smooth_init(&e->sm_pw[0], VA_SMOOTH_S, ctrl_rate, e->p[VA_P_OSC1_PW]);
    smooth_init(&e->sm_pw[1], VA_SMOOTH_S, ctrl_rate, e->p[VA_P_OSC2_PW]);
    smooth_init(&e->sm_vol, VA_SMOOTH_S, ctrl_rate, fm_db2lin(e->p[VA_P_VOLUME]));
    for (int k = 0; k < 3; k++)
        e->lvl_prev[k] = e->sm_lvl[k].y;
    e->vol_prev = e->sm_vol.y;
    e->bend = 0.0f;
    e->modwheel = 0.0f;
    e->last_note = 60.0f;
    e->has_last = 0;
    e->env_dirty = 1;
    e->rng = dsp_seed(0x5EEDu);
    lfo_init(&e->lfo, sample_rate, 0x1F0u);
    lfo_set(&e->lfo, e->p[VA_P_LFO_RATE], (lfo_wave_t)(int)e->p[VA_P_LFO_WAVE]);
    vpoly_init(&e->poly, VA_VOICES, sample_rate, &va_poly_ops, e);
    for (int i = 0; i < VA_VOICES; i++) {
        va_voice_t *v = &e->v[i];
        v->velgain = 0.0f;
        v->penv = 0.0f;
        v->pitch = 60.0f;
        osc_init(&v->osc[0], sample_rate);
        osc_init(&v->osc[1], sample_rate);
        unison_init(&v->uni[0], sample_rate, 0x4000u + 2u * (uint32_t)i);
        unison_init(&v->uni[1], sample_rate, 0x4001u + 2u * (uint32_t)i);
        v->stereo = 0;
        noise_init(&v->noise, 0x1000u + (uint32_t)i);
        vfilter_init(&v->f1, sample_rate);
        vfilter_init(&v->f2, sample_rate);
#if VA_STEREO
        vfilter_init(&v->f1r, sample_rate);
        vfilter_init(&v->f2r, sample_rate);
#endif
        adsr_init(&v->amp, sample_rate);
        adsr_init(&v->fenv, sample_rate);
        analog_init(&v->analog[0], 0x2000u + 2u * (uint32_t)i);
        analog_init(&v->analog[1], 0x2001u + 2u * (uint32_t)i);
        for (int k = 0; k < 3; k++) {
            lfo_init(&v->dlfo[k], sample_rate, 0x3000u + 16u * (uint32_t)i + (uint32_t)k);
            v->dlfo_factor[k] = 1.0f;
        }
    }
}

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    va_t *e = eng;
    va_voice_t *v = &e->v[vi];
    (void)note;
    vel = dsp_clampf(vel, 0.0f, 1.0f);
    const float sens = e->p[VA_P_AMP_VEL];
    v->velgain = 1.0f - sens * (1.0f - vel * vel); /* quadratic curve */
    v->vel_cut = (vel - 1.0f) * e->p[VA_P_VEL_CUTOFF];
    if (hard) {
        /* random free-running phases: each note sounds slightly different, as on an analog synth */
        osc_reset(&v->osc[0], dsp_rand_unipolar(&e->rng));
        osc_reset(&v->osc[1], dsp_rand_unipolar(&e->rng));
        unison_reset(&v->uni[0], 1);
        unison_reset(&v->uni[1], 1);
        vfilter_reset(&v->f1);
        vfilter_reset(&v->f2);
        v->stereo = 0; /* a new note starts mono: the right filters are copied from the left when it widens */
        adsr_reset(&v->amp);
        adsr_reset(&v->fenv);
        const float pos = e->p[VA_P_SPREAD] * dsp_rand_bipolar(&e->rng);
        voice_pan(pos, &v->pan_l, &v->pan_r);
        v->gain_prev = VA_VOICE_GAIN * v->velgain * e->vol_prev;
        /* detune LFOs: independent random start phase and rate (+/-40 % at full spread) for each
         * LFO of each voice: every note of a chord beats at its own pace */
        const float spread = e->p[VA_P_LFO2_SPREAD];
        for (int k = 0; k < 3; k++) {
            lfo_reset(&v->dlfo[k], spread * dsp_rand_unipolar(&e->rng));
            v->dlfo_factor[k] = 1.0f + 0.4f * spread * dsp_rand_bipolar(&e->rng);
        }
    }
    v->vel = vel;
    v->penv = 1.0f; /* the pitch envelope restarts on every strike */
    /* glide: from the previous note if allowed, otherwise jump straight to the new one */
    const int glide = e->p[VA_P_GLIDE] > 0.0f &&
                      ((int)e->p[VA_P_GLIDE_MODE] == 0 ? e->has_last : vpoly_overlap(&e->poly));
    if (!glide)
        v->pitch = (float)note;
    else if (hard)
        v->pitch = e->last_note;
    e->last_note = (float)note;
    e->has_last = 1;
    adsr_gate(&v->amp, 1);
    adsr_gate(&v->fenv, 1);
}

/* Legato: new note without retriggering the envelopes; glides if a glide time is set. */
static void voice_legato(void *eng, int vi, int note, float vel)
{
    va_t *e = eng;
    (void)vel;
    if (e->p[VA_P_GLIDE] <= 0.0f)
        e->v[vi].pitch = (float)note;
    e->last_note = (float)note;
    e->has_last = 1;
}

static void voice_release(void *eng, int vi)
{
    va_voice_t *v = &((va_t *)eng)->v[vi];
    adsr_gate(&v->amp, 0);
    adsr_gate(&v->fenv, 0);
}

static float voice_level(const void *eng, int vi)
{
    const va_voice_t *v = &((const va_t *)eng)->v[vi];
    return v->amp.level * v->velgain;
}

static void voice_silence(void *eng, int vi)
{
    va_voice_t *v = &((va_t *)eng)->v[vi];
    adsr_reset(&v->amp);
    adsr_reset(&v->fenv);
}

static const vpoly_ops_t va_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static vpoly_t *va_poly(void *st) { return &((va_t *)st)->poly; }

static void va_set_param(void *st, int id, float norm)
{
    va_t *e = st;
    if (id < 0 || id >= VA_P_COUNT)
        return;
    e->p[id] = param_from_norm(&va_params[id], norm);
    if (vpoly_active_count(&e->poly) == 0) { /* silent: nothing to glide, the next note starts on the new values */
        smooth_snap(&e->sm_cut, fm_log2(e->p[VA_P_CUTOFF]));
        smooth_snap(&e->sm_res, e->p[VA_P_RESO]);
        smooth_snap(&e->sm_lvl[0], e->p[VA_P_OSC1_LEVEL]);
        smooth_snap(&e->sm_lvl[1], e->p[VA_P_OSC2_LEVEL]);
        smooth_snap(&e->sm_lvl[2], e->p[VA_P_NOISE_LEVEL]);
        smooth_snap(&e->sm_pw[0], e->p[VA_P_OSC1_PW]);
        smooth_snap(&e->sm_pw[1], e->p[VA_P_OSC2_PW]);
        smooth_snap(&e->sm_vol, fm_db2lin(e->p[VA_P_VOLUME]));
        for (int k = 0; k < 3; k++)
            e->lvl_prev[k] = e->sm_lvl[k].y;
        e->vol_prev = e->sm_vol.y;
    }
    switch (id) {
    case VA_P_AMP_A: case VA_P_AMP_D: case VA_P_AMP_S: case VA_P_AMP_R:
    case VA_P_FENV_A: case VA_P_FENV_D: case VA_P_FENV_S: case VA_P_FENV_R:
        e->env_dirty = 1;
        break;
    case VA_P_VOICE_MODE:
        vpoly_set_mode(&e->poly, (vpoly_mode_t)(int)e->p[VA_P_VOICE_MODE]);
        break;
    case VA_P_OVERSAMPLE:
        for (int i = 0; i < VA_VOICES; i++) {
            vfilter_set_oversampling(&e->v[i].f1, 1 << (int)e->p[id]);
            vfilter_set_oversampling(&e->v[i].f2, 1 << (int)e->p[id]);
        }
        break;
    case VA_P_LFO_RATE: case VA_P_LFO_WAVE:
        lfo_set(&e->lfo, e->p[VA_P_LFO_RATE], (lfo_wave_t)(int)e->p[VA_P_LFO_WAVE]);
        break;
    default:
        break;
    }
}

/* Default CC map (Arturia MiniLab 3 factory layout, to be checked with synth_play --verbose):
 * knobs 1..8 = CC 74 71 76 77 93 18 19 16, faders 1..4 = CC 82 83 85 17. CC 93 is taken by the
 * effect bus (chorus), as are 91 (reverb), 94 (delay) and 69 (freeze). */
static const engine_cc_t va_cc_map[] = {
    {74, VA_P_CUTOFF}, {71, VA_P_RESO}, {76, VA_P_FENV_AMT}, {77, VA_P_LFO_RATE},
    {18, VA_P_OSC2_DETUNE}, {19, VA_P_DRIFT}, {16, VA_P_NOISE_LEVEL},
    {82, VA_P_AMP_A}, {83, VA_P_AMP_D}, {85, VA_P_AMP_S}, {17, VA_P_AMP_R},
};

static void va_pitch_bend(void *st, float bend) { ((va_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }
static void va_mod_wheel(void *st, float v) { ((va_t *)st)->modwheel = dsp_clampf(v, 0.0f, 1.0f); }

/* Control values shared by all voices for the current block. */
typedef struct {
    float cut_oct, res, pitch, lfo_cut, vol;
    float lfo1;                       /* LFO 1 value, modulates LFO 2 */
    float lvl0[3], lvl1[3];
    float pw[2];
    analog_cfg_t analog;
} va_block_t;

/* The two filters of one channel: in series, or in parallel (both hear the oscillators, mixed half and half). */
static inline void filters(vfilter_t *f1, vfilter_t *f2, int mode2, int parallel, float *x, int n)
{
    if (parallel) {
        float x2[SYNTH_BLOCK];
        vfilter_process(f2, x, x2, n);
        vfilter_process(f1, x, x, n);
        for (int i = 0; i < n; i++)
            x[i] = 0.5f * (x[i] + x2[i]);
    } else {
        vfilter_process(f1, x, x, n);
        if (mode2 > 0)
            vfilter_process(f2, x, x, n);
    }
}

static DSP_FAST_CODE void voice_render(va_t *e, int vi, const va_block_t *b, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    va_voice_t *v = &e->v[vi];
    const float *p = e->p;
    float x[SYNTH_BLOCK], t[SYNTH_BLOCK], env[SYNTH_BLOCK];
    const int note = vpoly_note(&e->poly, vi);
    /* portamento: constant-time exponential glide toward the note (tau = glide / 3) */
    if (p[VA_P_GLIDE] > 0.0f)
        v->pitch += ((float)note - v->pitch) * voice_glide_coef(p[VA_P_GLIDE], n, e->sr);
    else
        v->pitch = (float)note;

    /* detune LFOs (cents), per voice: rate x 2^(dir * lfo1 * rate_mod), depth x (1 + lfo1 * depth_mod).
     * dir = +1, -1, +0.5: LFO 1 speeds LFO 2 up while it slows LFO 3 down, so they drift apart. */
    static const float dir[3] = {1.0f, -1.0f, 0.5f};
    static const int rate_id[3] = {VA_P_LFO2_RATE, VA_P_LFO3_RATE, VA_P_LFO4_RATE};
    static const int depth_id[3] = {VA_P_LFO2_DETUNE, VA_P_LFO3_DETUNE, VA_P_LFO4_DETUNE};
    const float depth_scale = dsp_maxf(0.0f, 1.0f + b->lfo1 * p[VA_P_LFO2_DEPTH_MOD]);
    float det[3];
    for (int k = 0; k < 3; k++) {
        if (p[depth_id[k]] <= 0.0f) {
            det[k] = 0.0f; /* unused LFO: skip it */
            continue;
        }
        lfo_set(&v->dlfo[k], p[rate_id[k]] * v->dlfo_factor[k] * fm_exp2(dir[k] * b->lfo1 * p[VA_P_LFO2_RATE_MOD]),
                LFO_SINE);
        det[k] = lfo_advance(&v->dlfo[k], SYNTH_BLOCK) * p[depth_id[k]] * depth_scale;
    }

    /* pitch (semitones): note + bend + LFO 1 + whole-voice detune (LFO 4); each oscillator its own imperfections */
    const float an[2] = {analog_next(&v->analog[0], &b->analog), analog_next(&v->analog[1], &b->analog)};
    float penv_st = 0.0f;
    if (p[VA_P_PENV_AMT] != 0.0f && v->penv > 1e-4f) {
        v->penv *= fm_exp2(-FM_LOG2E * (float)n / (p[VA_P_PENV_DECAY] * e->sr)); /* value at block end */
        penv_st = p[VA_P_PENV_AMT] * v->penv;
    }
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE */
    const float base = v->pitch + b->pitch + 0.01f * det[2] + penv_st + vm[VM_PITCH];
    const int wave[2] = {(int)p[VA_P_OSC1_WAVE], (int)p[VA_P_OSC2_WAVE]};
    const float freq[2] = {
        fm_mtof(base + 12.0f * p[VA_P_OSC1_OCT] + p[VA_P_OSC1_SEMI] + 0.01f * (p[VA_P_OSC1_DETUNE] + det[1] + an[0])),
        fm_mtof(base + 12.0f * p[VA_P_OSC2_OCT] + p[VA_P_OSC2_SEMI] + 0.01f * (p[VA_P_OSC2_DETUNE] + det[0] + an[1])),
    };
    /* each oscillator alone, or as a unison of detuned voices; the voice turns stereo while a unison has a width */
    const int uni[2] = {(int)p[VA_P_OSC1_UNISON], (int)p[VA_P_OSC2_UNISON]};
    const float width[2] = {uni[0] >= 2 ? p[VA_P_OSC1_UNI_WIDTH] : 0.0f, uni[1] >= 2 ? p[VA_P_OSC2_UNI_WIDTH] : 0.0f};
    static const int uni_id[2] = {VA_P_OSC1_UNI_DETUNE, VA_P_OSC2_UNI_DETUNE};
    int stereo = 0;
    for (int k = 0; k < 2; k++) {
        const int heard = b->lvl0[k] > 0.0f || b->lvl1[k] > 0.0f;
        if (uni[k] >= 2) {
            const float *u = &p[uni_id[k]]; /* detune, blend, width */
            unison_set(&v->uni[k], freq[k], wave[k], b->pw[k], uni[k], u[0], u[1], u[2], p[VA_P_UNI_MOTION],
                       p[VA_P_UNI_MOTION_RATE], SYNTH_BLOCK);
            stereo |= VA_STEREO && heard && width[k] > 0.0f;
        } else {
            osc_set_wave(&v->osc[k], (osc_wave_t)wave[k]);
            osc_set(&v->osc[k], freq[k], b->pw[k]);
        }
    }
#if VA_STEREO
    if (stereo && !v->stereo) { /* widening: the right channel's filters start where the left ones are */
        v->f1r = v->f1;
        v->f2r = v->f2;
    }
#endif
    v->stereo = stereo;

    /* cutoff (octaves): base + envelope + keytracking + velocity + LFO + drift */
    adsr_process(&v->fenv, env, n);
    const float cut = b->cut_oct + env[n - 1] * p[VA_P_FENV_AMT] +
                      p[VA_P_KEYTRACK] * (v->pitch - 60.0f) * (1.0f / 12.0f) + v->vel_cut + b->lfo_cut +
                      an[0] * 0.005f + vm[VM_BRIGHT];
    const float reso01 = param_to_norm(&va_params[VA_P_RESO], b->res); /* formant and scream take 0..1 */
    vfilter_set(&v->f1, (int)p[VA_P_FILTER_MODE], fm_exp2(cut), b->res, reso01, p[VA_P_F1_CHAR]);
#if VA_STEREO
    if (stereo)
        vfilter_set(&v->f1r, (int)p[VA_P_FILTER_MODE], fm_exp2(cut), b->res, reso01, p[VA_P_F1_CHAR]);
#endif
    const int mode2 = (int)p[VA_P_F2_MODE];
    if (mode2 > 0) { /* filter 2 follows the same envelope, keytracking and modulation as filter 1 */
        const float cut2 = dsp_clampf(fm_log2(p[VA_P_F2_CUTOFF]) + (cut - b->cut_oct), 4.3f, 14.3f);
        vfilter_set(&v->f2, mode2 - 1, fm_exp2(cut2), 0.5f * fm_exp2(5.3f * p[VA_P_F2_RESO]), p[VA_P_F2_RESO], p[VA_P_F2_CHAR]);
#if VA_STEREO
        if (stereo)
            vfilter_set(&v->f2r, mode2 - 1, fm_exp2(cut2), 0.5f * fm_exp2(5.3f * p[VA_P_F2_RESO]), p[VA_P_F2_RESO], p[VA_P_F2_CHAR]);
#endif
    }

    /* sources: x (and xr, the right channel, while the voice is stereo) */
    float xr[SYNTH_BLOCK], tr[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        x[i] = 0.0f;
    if (stereo)
        for (int i = 0; i < n; i++)
            xr[i] = 0.0f;
    for (int k = 0; k < 2; k++) {
        if (!(b->lvl0[k] > 0.0f || b->lvl1[k] > 0.0f))
            continue;
        const int spread = stereo && width[k] > 0.0f; /* this unison's voices go left and right */
        if (uni[k] >= 2)
            unison_process(&v->uni[k], t, spread ? tr : NULL, n);
        else
            osc_process(&v->osc[k], t, n);
        dsp_ramp_mac(x, t, b->lvl0[k], b->lvl1[k], n);
        if (stereo)
            dsp_ramp_mac(xr, spread ? tr : t, b->lvl0[k], b->lvl1[k], n);
    }
    if (b->lvl0[2] > 0.0f || b->lvl1[2] > 0.0f) {
        noise_process(&v->noise, t, n);
        dsp_ramp_mac(x, t, b->lvl0[2], b->lvl1[2], n);
        if (stereo)
            dsp_ramp_mac(xr, t, b->lvl0[2], b->lvl1[2], n);
    }

    const int parallel = mode2 > 0 && (int)p[VA_P_FILTER_ROUTING] == 1;
    filters(&v->f1, &v->f2, mode2, parallel, x, n);
#if VA_STEREO
    if (stereo)
        filters(&v->f1r, &v->f2r, mode2, parallel, xr, n);
#endif

    /* amp: envelope x gain (velocity, volume) x steal fade */
    adsr_process(&v->amp, env, n);
    const float g1 = VA_VOICE_GAIN * v->velgain * b->vol;
    if (stereo) { /* the same gain on both sides, then the voice's own pan as a balance */
        dsp_ramp_env_mul(xr, env, v->gain_prev, g1, n);
        dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
        for (int i = 0; i < n; i++)
            x[i] *= v->pan_l, xr[i] *= v->pan_r;
        voice_out_stereo(&e->poly, vi, x, xr, n, L, R, &e->poly.nan_count);
    } else {
        dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
        voice_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->poly.nan_count);
    }
    v->gain_prev = g1;
    vpoly_end_block(&e->poly, vi, adsr_active(&v->amp));
}

static DSP_FAST_CODE void va_render(void *st, float *L, float *R)
{
    va_t *e = st;
    const float *p = e->p;
    va_block_t b;

    if (e->env_dirty) {
        for (int i = 0; i < VA_VOICES; i++) {
            adsr_set(&e->v[i].amp, p[VA_P_AMP_A], p[VA_P_AMP_D], p[VA_P_AMP_S], p[VA_P_AMP_R]);
            adsr_set(&e->v[i].fenv, p[VA_P_FENV_A], p[VA_P_FENV_D], p[VA_P_FENV_S], p[VA_P_FENV_R]);
        }
        e->env_dirty = 0;
    }

    b.cut_oct = smooth_next(&e->sm_cut, fm_log2(p[VA_P_CUTOFF]));
    b.res = smooth_next(&e->sm_res, p[VA_P_RESO]);
    b.lvl1[0] = smooth_next(&e->sm_lvl[0], p[VA_P_OSC1_LEVEL]);
    b.lvl1[1] = smooth_next(&e->sm_lvl[1], p[VA_P_OSC2_LEVEL]);
    b.lvl1[2] = smooth_next(&e->sm_lvl[2], p[VA_P_NOISE_LEVEL]);
    for (int k = 0; k < 3; k++)
        b.lvl0[k] = e->lvl_prev[k];
    b.vol = smooth_next(&e->sm_vol, fm_db2lin(p[VA_P_VOLUME]));

    const float lfo = lfo_advance(&e->lfo, SYNTH_BLOCK);
    b.pitch = e->bend * p[VA_P_BEND_RANGE] + lfo * (p[VA_P_LFO_PITCH] + e->modwheel * p[VA_P_WHEEL_VIB]);
    b.lfo_cut = lfo * p[VA_P_LFO_CUTOFF];
    b.lfo1 = lfo;
    analog_cfg(&b.analog, p[VA_P_TOLERANCE], p[VA_P_DRIFT], p[VA_P_DRIFT_TIME], p[VA_P_JITTER], (float)SYNTH_BLOCK / e->sr);
    b.pw[0] = dsp_clampf(smooth_next(&e->sm_pw[0], p[VA_P_OSC1_PW]) + lfo * p[VA_P_LFO_PWM], 0.05f, 0.95f);
    b.pw[1] = dsp_clampf(smooth_next(&e->sm_pw[1], p[VA_P_OSC2_PW]) + lfo * p[VA_P_LFO_PWM], 0.05f, 0.95f);

    for (int vi = 0; vi < VA_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, &b, L, R);

    for (int k = 0; k < 3; k++)
        e->lvl_prev[k] = b.lvl1[k];
    e->vol_prev = b.vol;
}

/* signal chain, for UIs (engine.h) */
static const engine_stage_t va_stages[] = {
    {"osc1", "Osc 1", "source", "osc1_ unison_", "", NULL, "osc1_level", 0.0f, 1.0f},
    {"osc2", "Osc 2", "source", "osc2_", "", NULL, "osc2_level", 0.0f, 0.6f},
    {"noise", "Noise", "source", "noise_level", "", NULL, "noise_level", 0.0f, 0.2f},
    {"filter1", "Filter", "filter", "filter_mode cutoff resonance keytrack vel_cutoff filter_character oversample", "osc1 osc2 noise",
     NULL, NULL, 0.0f, 0.0f},
    {"filter2", "Filter 2", "filter", "filter2_ filter_routing", "filter1", NULL, "filter2_mode", 0.0f, 1.0f},
    {"amp", "Amp", "amp", "amp_ volume pan_spread", "filter2", NULL, NULL, 0.0f, 0.0f},
    {"fenv", "Filter env", "mod", "fenv_", "", "filter1 filter2", NULL, 0.0f, 0.0f},
    {"pitchenv", "Pitch env", "mod", "pitch_env pitch_env_decay", "", "osc1 osc2", "pitch_env", 0.0f, 12.0f},
    {"lfo", "LFO", "mod", "lfo_rate lfo_wave lfo_pitch lfo_cutoff lfo_pwm wheel_vibrato", "", "osc1 osc2 filter1", NULL,
     0.0f, 0.0f},
    {"motion", "Motion", "mod", "lfo2_ lfo3_ lfo4_ drift drift_time tolerance jitter", "", "osc1 osc2", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "voice_mode glide glide_mode bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t va_engine = {
    .name = "va",
    .description = "subtractive virtual analog: 2 oscillators (sine/saw/pulse/tri, each a unison of up to 16 detuned voices spread in stereo) + noise -> two filter slots in series or parallel "
                       "(state-variable, formant, screaming 2-pole) -> amp; amp and filter ADSR, global LFO, 3 desynchronized per-voice detune LFOs, analog drift (8 voices)",
    .params = va_params,
    .param_count = VA_P_COUNT,
    .voices = VA_VOICES,
    .init = va_init,
    .poly = va_poly,
    .set_param = va_set_param,
    .pitch_bend = va_pitch_bend,
    .mod_wheel = va_mod_wheel,
    .cc_map = va_cc_map,
    .cc_count = (int)(sizeof va_cc_map / sizeof va_cc_map[0]),
    .render = va_render,
    .stages = va_stages,
    .stage_count = (int)(sizeof va_stages / sizeof va_stages[0]),
};
