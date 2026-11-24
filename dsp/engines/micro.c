/* micro.c — precision sine engine (see micro.h). Unlike the other engines, everything that shapes a hit runs per
 * sample: the amplitude and pitch envelopes (exact lengths in samples, curved segments recomputed exactly at each
 * block start), the glide, the ratchet clock, the gate pattern; a note starts on the sample the host asked for
 * (vpoly_t.onset), and the tempo-synced parts read the beat position handed over by synth.c (engine_ops_t.tempo).
 * Curve c in -1..1: 0 = linear, > 0 = fast start (an RC discharge, the classic punchy decay), < 0 = slow start. */
#include "micro.h"
#include "../core/fastmath.h"
#include "../mod/tempo.h"

#define MICRO_GAIN 0.125f      /* -18 dBFS per voice */
#define MICRO_CLICK_LIVE 0.25f /* s: how long the click filter keeps running after a hit */

static const char *const voice_mode_labels[] = {"poly", "mono", "legato", NULL};
static const char *const pitch_mode_labels[] = {"key", "fixed", NULL};
static const char *const env_mode_labels[] = {"gate", "one_shot", NULL};
static const char *const osc2_mode_labels[] = {"ratio", "hz", NULL};
static const char *const click_mode_labels[] = {"off", "impulse", "noise", NULL};
static const char *const gate_mode_labels[] = {"off", "sync", "free", NULL};
static const char *const noise_type_labels[] = {"white", "stepped", "periodic", NULL};
static const char *const noise_filter_labels[] = {"off", "lp", "bp", "hp", NULL};
static const char *const noise_env_labels[] = {"amp", "hit", "step", NULL};
static const char *const noise_gate_labels[] = {"off", "same", "inverse", "pattern", NULL};
static const char *const low_mode_labels[] = {"fixed", "note", NULL};

const param_desc_t micro_params[MICRO_P_COUNT] = {
    PARAM(MICRO_P_VOICE_MODE, "voice_mode", "voice.mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, voice_mode_labels,
          "poly, mono (every note retriggers) or legato (overlapping notes glide without retriggering)"),
    PARAM(MICRO_P_PITCH_MODE, "pitch_mode", "micro.pitch_mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, pitch_mode_labels,
          "key: the note sets the pitch; fixed: every note plays `freq` (the note only triggers)"),
    PARAM(MICRO_P_FREQ, "freq", "micro.freq", 10.0f, 20000.0f, 440.0f, PARAM_EXP, "Hz", 1, NULL,
          "frequency of the fixed mode"),
    PARAM(MICRO_P_TRANSPOSE, "transpose", "voice.transpose", -48.0f, 48.0f, 0.0f, PARAM_INT, "st", 0, NULL,
          "transposition, in both modes"),
    PARAM(MICRO_P_FINE, "fine", "micro.fine", -100.0f, 100.0f, 0.0f, PARAM_LIN, "cent", 1, NULL, "fine tuning"),
    PARAM(MICRO_P_GLIDE, "glide", "voice.glide", 0.0005f, 5.0f, 0.0005f, PARAM_EXP, "s", 1, NULL,
          "glide time in the mono and legato modes (arrives within 1 % after this time; the bottom = off)"),
    PARAM(MICRO_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
    PARAM(MICRO_P_PHASE, "phase", "micro.phase", 0.0f, 360.0f, 0.0f, PARAM_LIN, "deg", 0, NULL,
          "start phase of every hit: 0 = soft start from silence, 90 = a deliberate click"),
    PARAM(MICRO_P_PHASE_RESET, "phase_reset", "micro.phase_reset", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0,
          param_onoff_labels, "on: every hit restarts the sines at `phase` (exact, repeatable); off: free running"),
    PARAM(MICRO_P_SINE_LEVEL, "sine_level", "osc1.level", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL,
          "level of the main sine (the bottom = off: the click or the second sine alone)"),
    PARAM(MICRO_P_PITCH_AMOUNT, "pitch_amount", "pitch_env.amount", -48.0f, 96.0f, 0.0f, PARAM_LIN, "st", 1, NULL,
          "start of the pitch envelope above the note (a kick: +24 .. +48), falling to the note"),
    PARAM(MICRO_P_PITCH_TIME, "pitch_time", "pitch_env.decay", 0.0002f, 4.0f, 0.04f, PARAM_EXP, "s", 1, NULL,
          "time of the pitch envelope's fall, exact"),
    PARAM(MICRO_P_PITCH_CURVE, "pitch_curve", "micro.pitch_curve", -1.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "shape of the pitch fall: 0 linear (in semitones), > 0 fast then slow (a kick), < 0 slow then fast"),
    PARAM(MICRO_P_ENV_MODE, "env_mode", "micro.env_mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, env_mode_labels,
          "gate: the note-off starts the release; one_shot: attack, hold and decay always run to the end (drums)"),
    PARAM(MICRO_P_ATTACK, "amp_attack", "amp.attack", 0.00002f, 10.0f, 0.0005f, PARAM_EXP, "s", 1, NULL,
          "attack time, exact (the bottom = one sample)"),
    PARAM(MICRO_P_HOLD, "amp_hold", "micro.hold", 0.00002f, 10.0f, 0.00002f, PARAM_EXP, "s", 1, NULL,
          "time held at full level after the attack (the bottom = none)"),
    PARAM(MICRO_P_DECAY, "amp_decay", "amp.decay", 0.00002f, 30.0f, 0.3f, PARAM_EXP, "s", 1, NULL,
          "decay time to the sustain level, exact"),
    PARAM(MICRO_P_SUSTAIN, "amp_sustain", "amp.sustain", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "sustain level (0: a percussive hit)"),
    PARAM(MICRO_P_RELEASE, "amp_release", "amp.release", 0.00002f, 30.0f, 0.05f, PARAM_EXP, "s", 1, NULL,
          "release time, exact"),
    PARAM(MICRO_P_ATTACK_CURVE, "attack_curve", "micro.attack_curve", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "shape of the attack: 0 linear, > 0 fast then slow, < 0 slow then fast"),
    PARAM(MICRO_P_DECAY_CURVE, "decay_curve", "micro.decay_curve", -1.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "shape of the decay and the release: 0 linear, > 0 fast then slow (analog), < 0 slow then fast (a gate)"),
    PARAM(MICRO_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "velocity to level: 0 = every hit equal, 1 = full range"),
    PARAM(MICRO_P_OSC2_LEVEL, "osc2_level", "osc2.level", -60.0f, 6.0f, -60.0f, PARAM_DB, "dB", 1, NULL,
          "level of the second sine (the bottom = off)"),
    PARAM(MICRO_P_OSC2_MODE, "osc2_mode", "micro.osc2_mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, osc2_mode_labels,
          "ratio: a frequency ratio to the main sine; hz: an offset in Hz (exact beating)"),
    PARAM(MICRO_P_OSC2_RATIO, "osc2_ratio", "micro.osc2_ratio", 0.0625f, 16.0f, 2.0f, PARAM_EXP, "", 1, NULL,
          "frequency ratio of the second sine (ratio mode)"),
    PARAM(MICRO_P_OSC2_OFFSET, "osc2_offset", "micro.osc2_offset", -100.0f, 100.0f, 1.0f, PARAM_LIN, "Hz", 1, NULL,
          "frequency offset of the second sine (hz mode): the two sines beat this many times a second"),
    PARAM(MICRO_P_OSC2_SPLIT, "osc2_split", "micro.osc2_split", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "on: the main sine (and sub, click) on the left, the second on the right (beating heard between the ears)"),
    PARAM(MICRO_P_OSC2_PITCH_ENV, "osc2_pitch_env", "micro.osc2_pitch_env", 0.0f, 1.0f, 1.0f, PARAM_INT, "", 0,
          param_onoff_labels, "the pitch envelope moves the second sine too"),
    PARAM(MICRO_P_SUB_LEVEL, "sub_level", "sub.level", -60.0f, 6.0f, -60.0f, PARAM_DB, "dB", 1, NULL,
          "level of the sub sine (the bottom = off)"),
    PARAM(MICRO_P_SUB_OCTAVE, "sub_octave", "micro.sub_octave", 1.0f, 2.0f, 1.0f, PARAM_INT, "oct", 0, NULL,
          "the sub one or two octaves below the main sine"),
    PARAM(MICRO_P_DRIVE, "drive", "micro.drive", 0.0f, 24.0f, 0.0f, PARAM_LIN, "dB", 1, NULL,
          "saturation of the main sine, sub and click (2x oversampled tanh): harmonics a small speaker can play"),
    PARAM(MICRO_P_CLICK_MODE, "click_mode", "micro.click_mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, click_mode_labels,
          "a click on every hit: an impulse through a low-pass, or a noise burst through a band-pass"),
    PARAM(MICRO_P_CLICK_LEVEL, "click_level", "micro.click_level", -60.0f, 6.0f, -12.0f, PARAM_DB, "dB", 1, NULL,
          "level of the click"),
    PARAM(MICRO_P_CLICK_TONE, "click_tone", "micro.click_tone", 100.0f, 20000.0f, 4000.0f, PARAM_EXP, "Hz", 1, NULL,
          "frequency of the click's filter"),
    PARAM(MICRO_P_CLICK_DECAY, "click_decay", "micro.click_decay", 0.0001f, 0.1f, 0.003f, PARAM_EXP, "s", 1, NULL,
          "length of the noise burst (time to -60 dB)"),
    PARAM(MICRO_P_BURST, "burst", "micro.burst", 0.0f, 256.0f, 0.0f, PARAM_INT, "cycles", 0, NULL,
          "tone burst: every hit plays exactly this many cycles of the main sine, then the sines stop (0 = off)"),
    PARAM(MICRO_P_RATCHET, "ratchet", "micro.ratchet", 1.0f, 16.0f, 1.0f, PARAM_INT, "", 0, NULL,
          "hits per note: the note repeats this many times (1 = off)"),
    PARAM(MICRO_P_RATCHET_DIV, "ratchet_div", "micro.ratchet_div", 0.0f, (float)(TEMPO_DIV_COUNT - 1), 1.0f,
          PARAM_INT, "", 0, tempo_div_labels, "interval between the repeats on the tempo (off: `ratchet_hz`)"),
    PARAM(MICRO_P_RATCHET_HZ, "ratchet_hz", "micro.ratchet_hz", 0.5f, 500.0f, 16.0f, PARAM_EXP, "Hz", 1, NULL,
          "repeat rate when not on the tempo"),
    PARAM(MICRO_P_RATCHET_DECAY, "ratchet_decay", "micro.ratchet_decay", -24.0f, 6.0f, -3.0f, PARAM_LIN, "dB", 1,
          NULL, "level change at each repeat (< 0: dying away, > 0: growing toward full level)"),
    PARAM(MICRO_P_GATE_MODE, "gate_mode", "micro.gate_mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, gate_mode_labels,
          "16-step gate pattern: on the song's beat (sync) or from the note at `gate_hz` (free, up to audio rates)"),
    PARAM(MICRO_P_GATE_DIV, "gate_div", "micro.gate_div", 1.0f, (float)(TEMPO_DIV_COUNT - 1), 3.0f, PARAM_INT, "", 0,
          tempo_div_labels, "length of a step in sync mode"),
    PARAM(MICRO_P_GATE_HZ, "gate_hz", "micro.gate_hz", 0.5f, 2000.0f, 32.0f, PARAM_EXP, "Hz", 1, NULL,
          "steps per second in free mode"),
    PARAM(MICRO_P_GATE_STEPS, "gate_steps", "micro.gate_steps", 1.0f, 16.0f, 16.0f, PARAM_INT, "", 0, NULL,
          "length of the pattern"),
    PARAM(MICRO_P_GATE_PATTERN, "gate_pattern", "micro.gate_pattern", 0.0f, 65535.0f, 43690.0f, PARAM_INT, "", 0,
          NULL, "the pattern as 16 bits, step 1 = the lowest bit (43690 = every other step, from step 2)"),
    PARAM(MICRO_P_GATE_DEPTH, "gate_depth", "micro.gate_depth", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "how far a closed step cuts the sound (1 = silence)"),
    PARAM(MICRO_P_GATE_EDGE, "gate_edge", "micro.gate_edge", 0.00002f, 0.02f, 0.001f, PARAM_EXP, "s", 1, NULL,
          "time of each gate edge (the bottom = one sample: hard, digital)"),
    PARAM(MICRO_P_PAN, "pan", "micro.pan", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "position in the stereo field"),
    PARAM(MICRO_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume"),
    PARAM(MICRO_P_NOISE_LEVEL, "noise_level", "noise.level", -60.0f, 12.0f, -60.0f, PARAM_DB, "dB", 1, NULL,
          "level of the noise layer (the bottom = off); it bypasses the drive"),
    PARAM(MICRO_P_NOISE_TYPE, "noise_type", "micro.noise_type", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, noise_type_labels,
          "white; stepped: a new random value `noise_rate` times a second (digital grain); periodic: a 7-bit shift "
          "register clocked at `noise_rate`, a metallic buzz at noise_rate / 127"),
    PARAM(MICRO_P_NOISE_RATE, "noise_rate", "micro.noise_rate", 50.0f, 48000.0f, 4000.0f, PARAM_EXP, "Hz", 1, NULL,
          "clock of the stepped and periodic noises (no band limiting: the aliasing is the sound)"),
    PARAM(MICRO_P_NOISE_FILTER, "noise_filter", "micro.noise_filter", 0.0f, 3.0f, 3.0f, PARAM_INT, "", 0,
          noise_filter_labels, "filter of the noise: off, low-pass, band-pass or high-pass"),
    PARAM(MICRO_P_NOISE_CUTOFF, "noise_cutoff", "micro.noise_cutoff", 20.0f, 20000.0f, 6000.0f, PARAM_EXP, "Hz", 1,
          NULL, "frequency of the noise filter"),
    PARAM(MICRO_P_NOISE_Q, "noise_q", "micro.noise_q", 0.5f, 20.0f, 0.7071f, PARAM_EXP, "", 1, NULL,
          "resonance of the noise filter (high = a narrow, whistling band)"),
    PARAM(MICRO_P_NOISE_ENV, "noise_env", "micro.noise_env", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, noise_env_labels,
          "amp: the noise follows the amplitude envelope; hit: its own decay from every hit; step: its own decay, "
          "restarted on every step where its gate opens (a tick per step)"),
    PARAM(MICRO_P_NOISE_DECAY, "noise_decay", "micro.noise_decay", 0.0005f, 4.0f, 0.05f, PARAM_EXP, "s", 1, NULL,
          "length of the noise's own decay (hit and step modes; time to -60 dB)"),
    PARAM(MICRO_P_NOISE_GATE, "noise_gate", "micro.noise_gate", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 0,
          noise_gate_labels, "the noise on the gate's steps: off (not gated), same steps as the tone, inverse (the "
          "noise fills the tone's holes) or its own `noise_pattern`; needs `gate_mode`"),
    PARAM(MICRO_P_NOISE_PATTERN, "noise_pattern", "micro.noise_pattern", 0.0f, 65535.0f, 21845.0f, PARAM_INT, "", 0,
          NULL, "the noise's own pattern, 16 bits, step 1 = the lowest bit (21845 = every other step, from step 1)"),
    PARAM(MICRO_P_NOISE_PAN, "noise_pan", "micro.noise_pan", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "position of the noise, relative to `pan`"),
    PARAM(MICRO_P_LOW_LEVEL, "low_level", "micro.low_level", -60.0f, 12.0f, -60.0f, PARAM_DB, "dB", 1, NULL,
          "level of the low layer, the weight (the bottom = off): a sine in the centre, out of the drive, with its "
          "own envelope counted in samples"),
    PARAM(MICRO_P_LOW_MODE, "low_mode", "micro.low_mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, low_mode_labels,
          "fixed: always at `low_freq`; note: the note played, brought into the octave that starts at `low_freq` "
          "(any key gives a low in the same range)"),
    PARAM(MICRO_P_LOW_FREQ, "low_freq", "micro.low_freq", 20.0f, 160.0f, 45.0f, PARAM_EXP, "Hz", 1, NULL,
          "frequency of the low layer (fixed), or the bottom of its octave (note)"),
    PARAM(MICRO_P_LOW_PHASE, "low_phase", "micro.low_phase", 0.0f, 360.0f, 0.0f, PARAM_LIN, "deg", 0, NULL,
          "phase every hit starts at: 0 = from zero, soft; 90 = from the top, a click of its own"),
    PARAM(MICRO_P_LOW_DROP, "low_drop", "micro.low_drop", 0.0f, 72.0f, 0.0f, PARAM_LIN, "st", 1, NULL,
          "how far above its frequency every hit starts (the punch)"),
    PARAM(MICRO_P_LOW_DROP_TIME, "low_drop_time", "micro.low_drop_time", 0.001f, 1.0f, 0.04f, PARAM_EXP, "s", 1, NULL,
          "time of the fall to its frequency"),
    PARAM(MICRO_P_LOW_ATTACK, "low_attack", "micro.low_attack", 0.00002f, 0.5f, 0.001f, PARAM_EXP, "s", 1, NULL,
          "attack of the low layer (the bottom = one sample)"),
    PARAM(MICRO_P_LOW_HOLD, "low_hold", "micro.low_hold", 0.0f, 4.0f, 0.05f, PARAM_LIN, "s", 1, NULL,
          "time at full level before the decay"),
    PARAM(MICRO_P_LOW_DECAY, "low_decay", "micro.low_decay", 0.002f, 10.0f, 0.4f, PARAM_EXP, "s", 1, NULL,
          "decay to silence, reached exactly at its end"),
    PARAM(MICRO_P_LOW_CURVE, "low_curve", "micro.low_curve", -1.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL,
          "curve of the decay: 0 = a straight line, towards 1 = a fast fall then a long tail, towards -1 = held then cut"),
    PARAM(MICRO_P_LOW_DRIVE, "low_drive", "micro.low_drive", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "saturation of the low sine: harmonics that carry it on small speakers, towards a square at the top"),
    PARAM(MICRO_P_LOW_GATE, "low_gate", "micro.low_gate", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 0, noise_gate_labels,
          "off: one hit per note (and per ratchet); else a hit on every step that opens while the note is held: the "
          "same steps as the tone, the inverse ones, or its own `low_pattern`; needs `gate_mode`"),
    PARAM(MICRO_P_LOW_PATTERN, "low_pattern", "micro.low_pattern", 0.0f, 65535.0f, 4369.0f, PARAM_INT, "", 0, NULL,
          "the low layer's own pattern, 16 bits, step 1 = the lowest bit (4369 = one step in four, from step 1)"),
    PARAM(MICRO_P_LOW_BEAT, "low_beat", "micro.low_beat", -30.0f, 30.0f, 0.0f, PARAM_LIN, "Hz", 1, NULL,
          "a second low sine this many hertz away from the first (0 = none): the two interfere, the level swells "
          "and falls `low_beat` times a second, starting full on every hit"),
    PARAM(MICRO_P_LOW_BEAT_MIX, "low_beat_mix", "micro.low_beat_mix", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "share of the second sine: 0.5 = equal, the sound falls to silence at every beat; less = a shallower swell"),
};

/* ---------------- envelope segments ---------------- */

static int samples_of(float seconds, float sr)
{
    const float n = seconds * sr + 0.5f;
    return n < 1.0f ? 1 : (int)n;
}

/* a segment from the current value to y1 in n samples, with curve c */
static void seg_start(micro_seg_t *s, float y1, int n, float c)
{
    s->y0 = s->y;
    s->y1 = y1;
    s->n = n;
    s->k = 0;
    const float ac = dsp_absf(c);
    if (ac < 0.01f || dsp_absf(y1 - s->y0) < 1e-9f) {
        s->lin = 1;
        s->a = 1.0f;
        s->la = 0.0f;
        s->b = (y1 - s->y0) / (float)n;
        return;
    }
    /* overshoot o (a fraction of the travel): small = strongly curved, large = nearly linear */
    const float o = 0.002f * fm_exp2(12.0f * (1.0f - ac));
    const float d = y1 - s->y0;
    s->lin = 0;
    if (c > 0.0f) { /* decays toward a target past the end: fast start */
        s->t = y1 + o * d;
        s->la = fm_log2(o / (1.0f + o)) / (float)n;
    } else { /* grows away from a point before the start: slow start */
        s->t = s->y0 - o * d;
        s->la = fm_log2((1.0f + o) / o) / (float)n;
    }
    s->a = fm_exp2(s->la);
    s->b = s->t * (1.0f - s->a);
}

/* exact value after k samples (called at block starts) */
static void seg_sync(micro_seg_t *s)
{
    if (s->k >= s->n)
        s->y = s->y1;
    else if (s->lin)
        s->y = s->y0 + s->b * (float)s->k;
    else
        s->y = s->t + (s->y0 - s->t) * fm_exp2(s->la * (float)s->k);
}

/* one sample; returns 1 when the segment has just ended (y is then exactly y1) */
static inline int seg_step(micro_seg_t *s)
{
    s->y = s->a * s->y + s->b;
    if (++s->k >= s->n) {
        s->y = s->y1;
        return 1;
    }
    return 0;
}

/* ---------------- voice ---------------- */

/* enter a stage (a stage of length zero passes straight to the next one; bounded: at most three steps) */
static void env_stage(micro_t *e, micro_voice_t *v, int stage)
{
    const float sr = e->sr;
    for (int guard = 0; guard < 4; guard++) {
        v->stage = stage;
        switch (stage) {
        case MICRO_ENV_ATTACK:
            seg_start(&v->amp, 1.0f, samples_of(e->p[MICRO_P_ATTACK], sr), e->p[MICRO_P_ATTACK_CURVE]);
            return;
        case MICRO_ENV_HOLD:
            if (e->p[MICRO_P_HOLD] < 0.00003f) { /* the knob's bottom: no hold */
                stage = MICRO_ENV_DECAY;
                continue;
            }
            seg_start(&v->amp, 1.0f, samples_of(e->p[MICRO_P_HOLD], sr), 0.0f);
            return;
        case MICRO_ENV_DECAY:
            seg_start(&v->amp, e->p[MICRO_P_SUSTAIN], samples_of(e->p[MICRO_P_DECAY], sr), e->p[MICRO_P_DECAY_CURVE]);
            return;
        case MICRO_ENV_SUSTAIN:
            if (!v->gate) {
                stage = MICRO_ENV_RELEASE;
                continue;
            }
            if (v->amp.y <= 0.0f)
                v->stage = MICRO_ENV_IDLE; /* sustain 0: the hit is over */
            return;
        case MICRO_ENV_RELEASE:
            if (v->amp.y <= 0.0f) {
                v->stage = MICRO_ENV_IDLE;
                return;
            }
            seg_start(&v->amp, 0.0f, samples_of(e->p[MICRO_P_RELEASE], sr), e->p[MICRO_P_DECAY_CURVE]);
            return;
        default:
            v->amp.y = 0.0f;
            return;
        }
    }
}

/* the end of a segment: next stage */
static void env_next(micro_t *e, micro_voice_t *v)
{
    switch (v->stage) {
    case MICRO_ENV_ATTACK: env_stage(e, v, MICRO_ENV_HOLD); break;
    case MICRO_ENV_HOLD: env_stage(e, v, MICRO_ENV_DECAY); break;
    case MICRO_ENV_DECAY: env_stage(e, v, MICRO_ENV_SUSTAIN); break;
    case MICRO_ENV_RELEASE: v->stage = MICRO_ENV_IDLE; v->amp.y = 0.0f; break;
    default: break;
    }
}

/* the pitch of a note before bend and per-voice modulation (MIDI note number, fractional) */
static float base_semi(const micro_t *e, int note)
{
    const float tr = e->p[MICRO_P_TRANSPOSE] + 0.01f * e->p[MICRO_P_FINE];
    if ((int)e->p[MICRO_P_PITCH_MODE] == 1)
        return 69.0f + 12.0f * fm_log2(e->p[MICRO_P_FREQ] * (1.0f / 440.0f)) + tr;
    return (float)note + tr;
}

static float ratchet_interval(const micro_t *e)
{
    const int div = (int)e->p[MICRO_P_RATCHET_DIV];
    const float s = div > 0 ? tempo_div_beats[div] * 60.0f / e->bpm : 1.0f / e->p[MICRO_P_RATCHET_HZ];
    return s * e->sr;
}

/* a hit of the low layer: what it was playing fades in 2 ms, the sine restarts at its phase, the envelope at zero */
static void low_hit(micro_t *e, micro_voice_t *v)
{
    const float *p = e->p;
    const float base = 69.0f + 12.0f * fm_log2(p[MICRO_P_LOW_FREQ] * (1.0f / 440.0f));
    v->low_semi = base;
    if ((int)p[MICRO_P_LOW_MODE] == 1) { /* the note, brought into the octave above the base */
        float up = v->semi_target - base;
        up -= 12.0f * (float)(int)(up * (1.0f / 12.0f));
        if (up < 0.0f)
            up += 12.0f;
        v->low_semi = base + up;
    }
    if (v->low_stage != MICRO_ENV_IDLE) {
        const float dg = 1.0f + 7.0f * p[MICRO_P_LOW_DRIVE];
        const float m = p[MICRO_P_LOW_BEAT] != 0.0f ? p[MICRO_P_LOW_BEAT_MIX] : 0.0f;
        const float s = (1.0f - m) * fm_sin_pos(v->low_ph) + m * fm_sin_pos(v->low_ph2);
        v->low_tail = v->low_amp.y * fm_tanh(dg * s) / fm_tanh(dg);
        v->low_tail_d = v->low_tail / (0.002f * e->sr);
    }
    v->low_ph = p[MICRO_P_LOW_PHASE] * (1.0f / 360.0f);
    v->low_ph -= (float)(int)v->low_ph;
    v->low_ph2 = v->low_ph;
    v->low_amp.y = 0.0f;
    v->low_stage = MICRO_ENV_ATTACK;
    seg_start(&v->low_amp, 1.0f, samples_of(p[MICRO_P_LOW_ATTACK], e->sr), 0.0f);
    v->low_pitch.y = 1.0f;
    v->low_pitch_on = p[MICRO_P_LOW_DROP] > 0.0f;
    if (v->low_pitch_on)
        seg_start(&v->low_pitch, 0.0f, samples_of(p[MICRO_P_LOW_DROP_TIME], e->sr), 0.7f);
}

/* the end of a segment of the low layer's envelope */
static void low_next(micro_t *e, micro_voice_t *v)
{
    if (v->low_stage == MICRO_ENV_ATTACK && e->p[MICRO_P_LOW_HOLD] >= 0.00003f) {
        v->low_stage = MICRO_ENV_HOLD;
        seg_start(&v->low_amp, 1.0f, samples_of(e->p[MICRO_P_LOW_HOLD], e->sr), 0.0f);
    } else if (v->low_stage == MICRO_ENV_ATTACK || v->low_stage == MICRO_ENV_HOLD) {
        v->low_stage = MICRO_ENV_DECAY;
        seg_start(&v->low_amp, 0.0f, samples_of(e->p[MICRO_P_LOW_DECAY], e->sr), e->p[MICRO_P_LOW_CURVE]);
    } else {
        v->low_stage = MICRO_ENV_IDLE;
        v->low_amp.y = 0.0f;
    }
}

/* a hit: the note's first one or a ratchet repeat */
static void hit(micro_t *e, micro_voice_t *v)
{
    if (v->hit_index == 0) {
        const float target = base_semi(e, v->next_note);
        v->note = v->next_note;
        if (v->glide_next && e->p[MICRO_P_GLIDE] > 0.00075f)
            v->semi_target = target; /* glide from where the voice is */
        else
            v->semi = v->semi_target = target;
        v->gate = 1;
    }
    const float db = e->p[MICRO_P_RATCHET_DECAY] * (float)v->hit_index;
    v->hit_gain = dsp_minf(fm_db2lin(db), 1.0f);
    if ((int)e->p[MICRO_P_PHASE_RESET]) {
        const float ph = e->p[MICRO_P_PHASE] * (1.0f / 360.0f);
        v->ph1 = v->ph2 = ph;
        v->phs = 0.0f;
    }
    env_stage(e, v, MICRO_ENV_ATTACK);
    v->pitch.y = 1.0f;
    v->pitch_on = e->p[MICRO_P_PITCH_AMOUNT] != 0.0f;
    if (v->pitch_on)
        seg_start(&v->pitch, 0.0f, samples_of(e->p[MICRO_P_PITCH_TIME], e->sr), e->p[MICRO_P_PITCH_CURVE]);
    const int burst = (int)e->p[MICRO_P_BURST];
    v->burst_left = burst > 0 ? (float)burst : -1.0f;
    v->tone_on = 1;
    const int cm = (int)e->p[MICRO_P_CLICK_MODE];
    if (cm > 0) {
        if (cm == 1) /* impulse through a 2-pole low-pass: peak ~ 2.87 fc / sr per unit of area, scaled back to 1 */
            v->click_imp = dsp_minf(e->sr / (2.87f * e->p[MICRO_P_CLICK_TONE]), 200.0f);
        else
            v->click_amp = 1.0f;
        v->click_live = (int)(MICRO_CLICK_LIVE * e->sr);
    }
    if ((int)e->p[MICRO_P_NOISE_ENV] > 0)
        v->noise_amp = 1.0f;
    if (e->p[MICRO_P_LOW_LEVEL] > -59.9f && ((int)e->p[MICRO_P_GATE_MODE] == 0 || (int)e->p[MICRO_P_LOW_GATE] == 0))
        low_hit(e, v);
    if ((int)e->p[MICRO_P_GATE_MODE] == 2 && v->hit_index == 0)
        v->gate_phase = 0.0f;
    v->hit_index++;
    if (v->hits_left > 0) {
        v->hits_left--;
        v->hit_in = (int)(ratchet_interval(e) + 0.5f) - 1; /* counted down from the next sample */
        if (v->hit_in < 0)
            v->hit_in = 0;
    } else {
        v->hit_in = -1;
    }
}

static void voice_release_now(micro_t *e, micro_voice_t *v)
{
    v->gate = 0;
    if ((int)e->p[MICRO_P_ENV_MODE] == 1 && v->stage >= MICRO_ENV_ATTACK && v->stage <= MICRO_ENV_DECAY)
        return; /* one shot: the decay's end starts the release */
    if (v->stage != MICRO_ENV_IDLE && v->stage != MICRO_ENV_RELEASE)
        env_stage(e, v, MICRO_ENV_RELEASE);
}

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    micro_t *e = eng;
    micro_voice_t *v = &e->v[vi];
    if (hard) {
        v->stage = MICRO_ENV_IDLE;
        v->amp.y = 0.0f;
        v->gate = 0;
        v->ph1 = v->ph2 = e->p[MICRO_P_PHASE] * (1.0f / 360.0f);
        v->phs = 0.0f;
        v->click_amp = v->click_imp = 0.0f;
        v->click_live = 0;
        svf_reset(&v->click_f);
        dist_reset(&v->dist);
        svf_reset(&v->noise_f);
        v->noise_amp = 0.0f;
        v->ngate_g = -1.0f;
        v->nstep = -1;
        v->lstep = -1;
        v->low_stage = MICRO_ENV_IDLE;
        v->low_amp.y = v->low_tail = 0.0f;
        v->low_pitch_on = 0;
        v->vm_fresh = 1;
        v->gate_g = -1.0f; /* set on the first sample */
    }
    const float vs = e->p[MICRO_P_VELOCITY];
    v->vel = 1.0f - vs + vs * vel;
    v->next_note = note;
    v->glide_next = !hard && (int)e->p[MICRO_P_VOICE_MODE] > 0;
    v->hit_index = 0;
    v->hits_left = (int)e->p[MICRO_P_RATCHET] - 1;
    v->hit_in = e->poly.onset; /* on its sample */
    v->off_in = -1;
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    micro_t *e = eng;
    micro_voice_t *v = &e->v[vi];
    (void)vel;
    v->note = note;
    v->semi_target = base_semi(e, note);
    if (e->p[MICRO_P_GLIDE] <= 0.00075f)
        v->semi = v->semi_target;
}

static void voice_release(void *eng, int vi)
{
    micro_t *e = eng;
    micro_voice_t *v = &e->v[vi];
    if (v->hit_in >= 0 && v->hit_index == 0)
        v->off_in = e->poly.onset > v->hit_in ? e->poly.onset : v->hit_in; /* never before the note */
    else
        v->off_in = e->poly.onset;
}

static float voice_level(const void *eng, int vi)
{
    const micro_voice_t *v = &((const micro_t *)eng)->v[vi];
    return v->amp.y * v->vel * v->hit_gain;
}

static void voice_silence(void *eng, int vi)
{
    micro_voice_t *v = &((micro_t *)eng)->v[vi];
    v->stage = MICRO_ENV_IDLE;
    v->amp.y = 0.0f;
    v->hit_in = v->off_in = -1;
    v->hits_left = 0;
    v->click_live = 0;
    v->click_amp = v->click_imp = 0.0f;
    v->noise_amp = 0.0f;
    v->low_stage = MICRO_ENV_IDLE;
    v->low_amp.y = v->low_tail = 0.0f;
}

static const vpoly_ops_t micro_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void micro_init(void *st, float sample_rate)
{
    micro_t *e = st;
    e->sr = sample_rate;
    for (int i = 0; i < MICRO_P_COUNT; i++)
        e->p[i] = micro_params[i].def;
    e->bend = 0.0f;
    e->bpm = 120.0f;
    e->beat0 = e->beat1 = 0.0f;
    e->have_beat = 0;
    e->click_mode_set = e->noise_filter_set = -1;
    e->nan_count = 0;
    smooth_init(&e->vol, 0.005f, sample_rate / (float)SYNTH_BLOCK, fm_db2lin(e->p[MICRO_P_VOLUME]));
    e->vol_prev = e->vol.y;
    vpoly_init(&e->poly, MICRO_VOICES, sample_rate, &micro_poly_ops, e);
    for (int i = 0; i < MICRO_VOICES; i++) {
        micro_voice_t *v = &e->v[i];
        *v = (micro_voice_t){0};
        v->hit_in = v->off_in = -1;
        v->hit_gain = 1.0f;
        v->noise = 0x9E3779B9u * (uint32_t)(i + 1);
        svf_init(&v->click_f, sample_rate);
        dist_init(&v->dist, sample_rate);
        svf_init(&v->noise_f, sample_rate);
        v->noise_rng = 0x2545F491u * (uint32_t)(i + 1);
        v->low_stage = MICRO_ENV_IDLE;
        v->low_amp.y = v->low_tail = v->low_tail_d = v->low_ph = v->low_ph2 = 0.0f;
        v->low_semi = 28.0f;
        v->low_pitch_on = 0;
        v->lstep = -1;
        v->lfsr = 0x7Fu;
        v->ngate_g = -1.0f;
        v->nstep = -1;
    }
}

static vpoly_t *micro_poly(void *st) { return &((micro_t *)st)->poly; }

static void micro_set_param(void *st, int id, float norm)
{
    micro_t *e = st;
    if (id < 0 || id >= MICRO_P_COUNT)
        return;
    e->p[id] = param_from_norm(&micro_params[id], norm);
    if (id == MICRO_P_VOICE_MODE)
        vpoly_set_mode(&e->poly, (vpoly_mode_t)(int)e->p[id]);
    if (id == MICRO_P_PITCH_MODE || id == MICRO_P_FREQ || id == MICRO_P_TRANSPOSE || id == MICRO_P_FINE)
        for (int i = 0; i < MICRO_VOICES; i++) { /* a held note follows the new tuning (through the glide) */
            micro_voice_t *v = &e->v[i];
            if (vpoly_active(&e->poly, i))
                v->semi_target = base_semi(e, v->note);
        }
}

static void micro_pitch_bend(void *st, float bend)
{
    micro_t *e = st;
    e->bend = bend * e->p[MICRO_P_BEND_RANGE];
}

static void micro_tempo(void *st, float bpm, float beat)
{
    micro_t *e = st;
    const float nominal = bpm * (float)SYNTH_BLOCK / (60.0f * e->sr);
    e->bpm = bpm > 1.0f ? bpm : 120.0f;
    float b0 = e->beat1;
    const float d = beat - b0;
    if (!e->have_beat || d < 0.0f || d > 4.0f * nominal + 1e-3f) /* first block, a wrap or a jump */
        b0 = beat - nominal;
    e->beat0 = b0;
    e->beat1 = beat;
    e->have_beat = 1;
}

/* phase increment (cycles per sample) of a pitch, at most 0.45 */
static inline float inc_of(float semi, float inv_sr)
{
    return dsp_minf(fm_mtof(semi) * inv_sr, 0.45f);
}

static inline float wrap1(float ph) { return ph >= 1.0f ? ph - (float)(int32_t)ph : ph; }

/* what micro_render computes once per block, for every voice */
typedef struct {
    float inv_sr;
    float vol0, vol1;                         /* engine volume at the start and the end of the block */
    float l1, l2, ls;                         /* levels of the main sine, the second sine and the sub (0 = off) */
    int split, o2hz, o2penv;
    float o2ratio, o2off, sub_mul, pamt, glide_c;
    int cm, drive_on;
    float click_g, click_mul;
    int gmode, gsteps;                        /* the gate pattern */
    uint32_t gpat;
    float gfloor, gslew, ginv, ghz, db;
    float pan_l, pan_r;
    float ln;                                 /* the noise layer */
    int ntype, nfilt, nenv, ngate;
    float nrate, nmul;
    uint32_t npat;
    float ll;                                 /* the low layer */
    int lgate;
    uint32_t lpat;
    float ldg, lnorm, ldrop, lbeat, lmix;
} micro_block_t;

/* one voice into the bus: level, steal fade, per-voice level and pan. xa = main sine, sub and click; xb = second
 * sine; xn = noise layer (valid when it is on); xw = low layer (valid when low_live). */
static DSP_FAST_CODE void voice_mix(micro_t *e, int vi, const micro_block_t *b, int low_live, float *xa, float *xb,
                                    float *xn, float *xw, float *L, float *R)
{
    micro_voice_t *v = &e->v[vi];
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    const float *vm = vpoly_vmod(&e->poly, vi);
    const float ln = b->ln, ll = b->ll;

    /* level: velocity, ratchet hit, volume; the steal fade; per-voice level and pan */
    const float g0 = MICRO_GAIN * v->vel * v->hit_gain * b->vol0, g1 = MICRO_GAIN * v->vel * v->hit_gain * b->vol1;
    dsp_ramp_mul(xa, g0, g1, n);
    dsp_ramp_mul(xb, g0, g1, n);
    if (ln > 0.0f)
        dsp_ramp_mul(xn, g0 * ln, g1 * ln, n);
    if (low_live)
        dsp_ramp_mul(xw, g0 * ll, g1 * ll, n);
    float fade[SYNTH_BLOCK];
    if (vpoly_fade(&e->poly, vi, fade, n))
        for (int i = 0; i < n; i++)
            xa[i] *= fade[i], xb[i] *= fade[i], xn[i] *= ln > 0.0f ? fade[i] : 0.0f, xw[i] *= low_live ? fade[i] : 0.0f;
    float d = 0.0f;
    if (voicemod_cfg_active(e->poly.vm_cfg)) {
        const float vg1 = vm[VM_LEVEL] != 0.0f ? fm_db2lin(dsp_clampf(vm[VM_LEVEL], -96.0f, 24.0f)) : 1.0f;
        const float vg0 = v->vm_fresh ? vg1 : v->vm_gain;
        dsp_ramp_mul(xa, vg0, vg1, n);
        dsp_ramp_mul(xb, vg0, vg1, n);
        if (ln > 0.0f)
            dsp_ramp_mul(xn, vg0, vg1, n);
        v->vm_gain = vg1;
        d = dsp_clampf(vm[VM_PAN], -1.0f, 1.0f);
    }
    v->vm_fresh = 0;
    dsp_guard_block(xa, n, &e->nan_count);
    dsp_guard_block(xb, n, &e->nan_count);
    if (low_live) { /* the low layer in the centre, whatever the pan */
        dsp_guard_block(xw, n, &e->nan_count);
        for (int i = 0; i < n; i++) {
            L[i] += 0.70710678f * xw[i];
            R[i] += 0.70710678f * xw[i];
        }
    }
    if (ln > 0.0f) { /* the noise at its own place */
        dsp_guard_block(xn, n, &e->nan_count);
        float nl, nr;
        voice_pan(dsp_clampf(p[MICRO_P_PAN] + d + p[MICRO_P_NOISE_PAN], -1.0f, 1.0f), &nl, &nr);
        for (int i = 0; i < n; i++) {
            L[i] += xn[i] * nl;
            R[i] += xn[i] * nr;
        }
    }
    if (b->split) { /* main sine on the left, second on the right, around the pan */
        float al, ar, bl, br;
        voice_pan(dsp_clampf(p[MICRO_P_PAN] + d - 1.0f, -1.0f, 1.0f), &al, &ar);
        voice_pan(dsp_clampf(p[MICRO_P_PAN] + d + 1.0f, -1.0f, 1.0f), &bl, &br);
        for (int i = 0; i < n; i++) {
            L[i] += xa[i] * al + xb[i] * bl;
            R[i] += xa[i] * ar + xb[i] * br;
        }
    } else {
        float pl, pr;
        voice_pan_offset(d, b->pan_l, b->pan_r, &pl, &pr);
        for (int i = 0; i < n; i++) {
            const float x = xa[i] + xb[i];
            L[i] += x * pl;
            R[i] += x * pr;
        }
    }
}

static DSP_FAST_CODE void voice_render(micro_t *e, int vi, const micro_block_t *blk, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    const float inv_sr = blk->inv_sr;
    const float l1 = blk->l1, l2 = blk->l2, ls = blk->ls;
    const int o2hz = blk->o2hz, o2penv = blk->o2penv;
    const float o2ratio = blk->o2ratio, o2off = blk->o2off, sub_mul = blk->sub_mul, pamt = blk->pamt;
    const float glide_c = blk->glide_c;
    const int cm = blk->cm, drive_on = blk->drive_on;
    const float click_g = blk->click_g, click_mul = blk->click_mul;
    const int gmode = blk->gmode, gsteps = blk->gsteps;
    const uint32_t gpat = blk->gpat;
    const float gfloor = blk->gfloor, gslew = blk->gslew, ginv = blk->ginv, ghz = blk->ghz, db = blk->db;
    const float ln = blk->ln;
    const int ntype = blk->ntype, nfilt = blk->nfilt, nenv = blk->nenv, ngate = blk->ngate;
    const float nrate = blk->nrate, nmul = blk->nmul;
    const uint32_t npat = blk->npat;
    const float ll = blk->ll;
    const int lgate = blk->lgate;
    const uint32_t lpat = blk->lpat;
    const float ldg = blk->ldg, lnorm = blk->lnorm, ldrop = blk->ldrop, lbeat = blk->lbeat, lmix = blk->lmix;

    micro_voice_t *v = &e->v[vi];
    const float *vm = vpoly_vmod(&e->poly, vi);
    const float shift = e->bend + vm[VM_PITCH];
    float xa[SYNTH_BLOCK], xb[SYNTH_BLOCK], ck[SYNTH_BLOCK], xn[SYNTH_BLOCK], xw[SYNTH_BLOCK];
    const int low_live = ll > 0.0f || v->low_stage != MICRO_ENV_IDLE || v->low_tail != 0.0f;
    if (v->low_stage != MICRO_ENV_IDLE) {
        seg_sync(&v->low_amp);
        if (v->low_pitch_on)
            seg_sync(&v->low_pitch);
    }
    float linc = inc_of(v->low_semi + (v->low_pitch_on ? ldrop * v->low_pitch.y : 0.0f), inv_sr);
    float lseen = v->low_semi;
    int any_click = 0, refresh = 0;

    seg_sync(&v->amp);
    if (v->pitch_on)
        seg_sync(&v->pitch);
    float inc1 = inc_of(v->semi + shift, inv_sr);

    for (int i = 0; i < n; i++) {
        if (v->hit_in == 0) {
            hit(e, v);
            refresh = 1;
        } else if (v->hit_in > 0) {
            v->hit_in--;
        }
        if (v->off_in == 0) {
            voice_release_now(e, v);
            v->off_in = -1;
        } else if (v->off_in > 0) {
            v->off_in--;
        }

        /* amplitude envelope */
        if (v->stage == MICRO_ENV_ATTACK || v->stage == MICRO_ENV_HOLD || v->stage == MICRO_ENV_DECAY ||
            v->stage == MICRO_ENV_RELEASE) {
            if (seg_step(&v->amp))
                env_next(e, v);
        }
        const float env = v->amp.y;

        /* pitch: glide and pitch envelope, per sample while they move */
        const int gliding = v->semi != v->semi_target;
        if (gliding) {
            v->semi += (v->semi_target - v->semi) * glide_c;
            if (dsp_absf(v->semi_target - v->semi) < 1e-4f)
                v->semi = v->semi_target;
        }
        if (gliding || v->pitch_on || refresh) {
            const float penv = v->pitch_on ? pamt * v->pitch.y : 0.0f;
            inc1 = inc_of(v->semi + shift + penv, inv_sr);
            refresh = 0;
            if (v->pitch_on && seg_step(&v->pitch)) {
                v->pitch_on = 0;
                refresh = 1; /* land on the note on the next sample */
            }
        }

        /* sines */
        float a = 0.0f, b = 0.0f;
        if (v->tone_on) {
            a = l1 * fm_sin_pos(v->ph1);
            if (ls > 0.0f)
                a += ls * fm_sin_pos(v->phs);
            if (l2 > 0.0f) {
                const float base = o2penv ? inc1 : inc_of(v->semi + shift, inv_sr);
                const float inc2 = o2hz ? dsp_clampf(base + o2off, 0.0f, 0.45f) : dsp_minf(base * o2ratio, 0.45f);
                b = l2 * fm_sin_pos(v->ph2);
                v->ph2 = wrap1(v->ph2 + inc2);
            }
            v->ph1 = wrap1(v->ph1 + inc1);
            v->phs = wrap1(v->phs + inc1 * sub_mul);
            if (v->burst_left >= 0.0f) {
                v->burst_left -= inc1;
                if (v->burst_left <= 0.0f)
                    v->tone_on = 0;
            }
        }

        /* gate pattern */
        float gg = 1.0f;
        int step = -1;
        if (gmode > 0) {
            if (gmode == 1) {
                const float beat = e->beat0 + db * (float)i;
                step = (int)(beat * ginv) % gsteps;
            } else {
                step = (int)v->gate_phase;
                v->gate_phase += ghz;
                if (v->gate_phase >= (float)gsteps)
                    v->gate_phase -= (float)gsteps;
            }
            const float target = (gpat >> step) & 1u ? 1.0f : gfloor;
            if (v->gate_g < 0.0f)
                v->gate_g = target;
            else if (v->gate_g < target)
                v->gate_g = dsp_minf(v->gate_g + gslew, target);
            else if (v->gate_g > target)
                v->gate_g = dsp_maxf(v->gate_g - gslew, target);
            gg = v->gate_g;
        }

        const float g = env * gg;
        xa[i] = a * g;
        xb[i] = b * g;

        /* noise layer: generator, its gate (its own slew), its envelope */
        if (ln > 0.0f) {
            float x;
            if (ntype == 0) {
                x = dsp_rand_bipolar(&v->noise_rng);
            } else {
                v->noise_ph += nrate;
                if (v->noise_ph >= 1.0f) {
                    v->noise_ph -= 1.0f;
                    if (ntype == 1) {
                        v->noise_hold = dsp_rand_bipolar(&v->noise_rng);
                    } else { /* x^7 + x^6 + 1: a maximal sequence of 127 */
                        const uint32_t bit = ((v->lfsr >> 6) ^ (v->lfsr >> 5)) & 1u;
                        v->lfsr = ((v->lfsr << 1) | bit) & 0x7Fu;
                        v->noise_hold = (v->lfsr & 1u) ? 1.0f : -1.0f;
                    }
                }
                x = v->noise_hold;
            }
            float ng = 1.0f;
            int open = 1;
            if (step >= 0) {
                if (ngate > 0) {
                    open = (int)((npat >> step) & 1u);
                    const float target = open ? 1.0f : gfloor;
                    if (v->ngate_g < 0.0f)
                        v->ngate_g = target;
                    else if (v->ngate_g < target)
                        v->ngate_g = dsp_minf(v->ngate_g + gslew, target);
                    else if (v->ngate_g > target)
                        v->ngate_g = dsp_maxf(v->ngate_g - gslew, target);
                    ng = v->ngate_g;
                }
                if (step != v->nstep) { /* a new step: a tick if the noise opens on it and the note is held */
                    if (nenv == 2 && open && v->gate)
                        v->noise_amp = 1.0f;
                    v->nstep = step;
                }
            }
            float ne;
            if (nenv == 0) {
                ne = env;
            } else {
                ne = v->noise_amp;
                v->noise_amp *= nmul;
            }
            xn[i] = x * ne * ng;
        }

        /* low layer: a hit on every step its gate opens, then its sine under its own envelope */
        if (low_live) {
            if (lgate > 0 && step >= 0 && step != v->lstep) {
                if (ll > 0.0f && v->gate && ((lpat >> step) & 1u)) {
                    low_hit(e, v);
                    linc = inc_of(v->low_semi + (v->low_pitch_on ? ldrop : 0.0f), inv_sr);
                }
                v->lstep = step;
            }
            if (v->low_semi != lseen) { /* a hit moved it (also the note's own, a few lines up) */
                lseen = v->low_semi;
                linc = inc_of(lseen + (v->low_pitch_on ? ldrop * v->low_pitch.y : 0.0f), inv_sr);
            }
            float w = 0.0f;
            if (v->low_stage != MICRO_ENV_IDLE) {
                if (v->low_pitch_on) {
                    linc = inc_of(v->low_semi + ldrop * v->low_pitch.y, inv_sr);
                    if (seg_step(&v->low_pitch)) {
                        v->low_pitch_on = 0;
                        linc = inc_of(v->low_semi, inv_sr);
                    }
                }
                const int ended = seg_step(&v->low_amp); /* (first, as the main envelope: a hit sounds on its sample) */
                float s = fm_sin_pos(v->low_ph);
                if (lmix > 0.0f) { /* the second sine, an exact number of hertz away: the two interfere */
                    s += lmix * (fm_sin_pos(v->low_ph2) - s);
                    v->low_ph2 = wrap1(v->low_ph2 + dsp_clampf(linc + lbeat, 0.0f, 0.45f));
                }
                w = v->low_amp.y * lnorm * fm_tanh(ldg * s);
                v->low_ph = wrap1(v->low_ph + linc);
                if (ended)
                    low_next(e, v);
            }
            if (v->low_tail != 0.0f) { /* the hit this one cut */
                w += v->low_tail;
                const float t = v->low_tail - v->low_tail_d;
                v->low_tail = t * v->low_tail <= 0.0f ? 0.0f : t;
            }
            xw[i] = w;
        }

        /* click input: an impulse or a decaying noise */
        float c = 0.0f;
        if (v->click_imp != 0.0f) {
            c = v->click_imp;
            v->click_imp = 0.0f;
        } else if (v->click_amp > 1e-4f) {
            c = v->click_amp * dsp_rand_bipolar(&v->noise);
            v->click_amp *= click_mul;
        }
        ck[i] = c * gg;
        any_click |= c != 0.0f;
    }

    /* the click through its filter, while it rings */
    if (v->click_live > 0 || any_click) {
        svf_set(&v->click_f, p[MICRO_P_CLICK_TONE], cm == 2 ? 1.2f : 0.7071f);
        svf_process(&v->click_f, ck, ck, n);
        for (int i = 0; i < n; i++)
            xa[i] += ck[i] * click_g;
        v->click_live -= n;
        if (v->click_live <= 0) {
            v->click_live = 0;
            svf_reset(&v->click_f);
        }
    }

    if (drive_on) {
        dist_set(&v->dist, DIST_SOFT, p[MICRO_P_DRIVE], 0.45f * e->sr, 1.0f);
        dist_process(&v->dist, xa, xa, n);
    }
    if (ln > 0.0f && nfilt > 0) {
        svf_set(&v->noise_f, p[MICRO_P_NOISE_CUTOFF], p[MICRO_P_NOISE_Q]);
        svf_process(&v->noise_f, xn, xn, n);
    }

    voice_mix(e, vi, blk, low_live, xa, xb, xn, xw, L, R);

    const int sounding = v->stage != MICRO_ENV_IDLE || v->hit_in >= 0 || v->click_live > 0 ||
                         (ln > 0.0f && nenv > 0 && v->noise_amp > 1e-4f) || v->low_stage != MICRO_ENV_IDLE ||
                         v->low_tail != 0.0f;
    vpoly_end_block(&e->poly, vi, sounding);
}

static DSP_FAST_CODE void micro_render(void *st, float *L, float *R)
{
    micro_t *e = st;
    const int n = SYNTH_BLOCK;
    const float inv_sr = 1.0f / e->sr;
    const float *p = e->p;
    micro_block_t b;

    b.inv_sr = inv_sr;
    b.vol0 = e->vol_prev;
    b.vol1 = smooth_next(&e->vol, fm_db2lin(p[MICRO_P_VOLUME]));
    e->vol_prev = b.vol1;

    /* block-rate settings */
    b.l1 = p[MICRO_P_SINE_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_SINE_LEVEL]);
    b.l2 = p[MICRO_P_OSC2_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_OSC2_LEVEL]);
    b.ls = p[MICRO_P_SUB_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_SUB_LEVEL]);
    b.split = (int)p[MICRO_P_OSC2_SPLIT], b.o2hz = (int)p[MICRO_P_OSC2_MODE] == 1;
    b.o2penv = (int)p[MICRO_P_OSC2_PITCH_ENV];
    b.o2ratio = p[MICRO_P_OSC2_RATIO], b.o2off = p[MICRO_P_OSC2_OFFSET] * inv_sr;
    b.sub_mul = (int)p[MICRO_P_SUB_OCTAVE] >= 2 ? 0.25f : 0.5f;
    b.pamt = p[MICRO_P_PITCH_AMOUNT];
    b.glide_c = p[MICRO_P_GLIDE] > 0.00075f ? voice_glide_arrive_coef(p[MICRO_P_GLIDE], 1, e->sr) : 1.0f;
    b.cm = (int)p[MICRO_P_CLICK_MODE];
    b.click_g = fm_db2lin(p[MICRO_P_CLICK_LEVEL]);
    b.click_mul = fm_exp2(-9.966f / (p[MICRO_P_CLICK_DECAY] * e->sr)); /* -60 dB after click_decay */
    b.drive_on = p[MICRO_P_DRIVE] > 0.05f;
    b.gmode = (int)p[MICRO_P_GATE_MODE];
    b.gsteps = (int)p[MICRO_P_GATE_STEPS];
    b.gpat = (uint32_t)p[MICRO_P_GATE_PATTERN];
    b.gfloor = 1.0f - p[MICRO_P_GATE_DEPTH];
    b.gslew = 1.0f / ((float)samples_of(p[MICRO_P_GATE_EDGE], e->sr));
    const float gdiv = tempo_div_beats[(int)p[MICRO_P_GATE_DIV]];
    b.ginv = gdiv > 0.0f ? 1.0f / gdiv : 4.0f;
    b.ghz = p[MICRO_P_GATE_HZ] * inv_sr;
    b.db = (e->beat1 - e->beat0) / (float)n;
    voice_pan(p[MICRO_P_PAN], &b.pan_l, &b.pan_r);
    /* the noise layer */
    b.ln = p[MICRO_P_NOISE_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_NOISE_LEVEL]);
    b.ntype = (int)p[MICRO_P_NOISE_TYPE], b.nfilt = (int)p[MICRO_P_NOISE_FILTER];
    b.nenv = (int)p[MICRO_P_NOISE_ENV], b.ngate = b.gmode > 0 ? (int)p[MICRO_P_NOISE_GATE] : 0;
    b.nrate = dsp_minf(p[MICRO_P_NOISE_RATE] * inv_sr, 1.0f);
    b.nmul = fm_exp2(-9.966f / (p[MICRO_P_NOISE_DECAY] * e->sr)); /* -60 dB after noise_decay */
    b.npat = b.ngate == 1 ? b.gpat : b.ngate == 2 ? ~b.gpat : (uint32_t)p[MICRO_P_NOISE_PATTERN];
    /* the low layer */
    b.ll = p[MICRO_P_LOW_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_LOW_LEVEL]);
    b.lgate = b.gmode > 0 ? (int)p[MICRO_P_LOW_GATE] : 0;
    b.lpat = b.lgate == 1 ? b.gpat : b.lgate == 2 ? ~b.gpat : (uint32_t)p[MICRO_P_LOW_PATTERN];
    b.ldg = 1.0f + 7.0f * p[MICRO_P_LOW_DRIVE], b.lnorm = 1.0f / fm_tanh(b.ldg), b.ldrop = p[MICRO_P_LOW_DROP];
    b.lbeat = p[MICRO_P_LOW_BEAT] * inv_sr, b.lmix = b.lbeat != 0.0f ? p[MICRO_P_LOW_BEAT_MIX] : 0.0f;
    if (b.nfilt != e->noise_filter_set) {
        for (int i = 0; i < MICRO_VOICES; i++) {
            svf_set_mode(&e->v[i].noise_f, (svf_mode_t)(b.nfilt > 0 ? b.nfilt - 1 : 0));
            svf_reset(&e->v[i].noise_f);
        }
        e->noise_filter_set = b.nfilt;
    }

    if (b.cm != e->click_mode_set) {
        for (int i = 0; i < MICRO_VOICES; i++)
            svf_set_mode(&e->v[i].click_f, b.cm == 2 ? SVF_BP : SVF_LP);
        e->click_mode_set = b.cm;
    }

    for (int vi = 0; vi < MICRO_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, &b, L, R);
}

static const engine_stage_t micro_stages[] = {
    {"sine", "Sine", "source", "sine_level pitch_mode freq transpose fine phase phase_reset burst", "", NULL, NULL, 0.0f, 0.0f},
    {"osc2", "Sine 2", "source", "osc2_", "", NULL, "osc2_level", -60.0f, -12.0f},
    {"sub", "Sub", "source", "sub_", "", NULL, "sub_level", -60.0f, -6.0f},
    {"click", "Click", "source", "click_", "", NULL, "click_mode", 0.0f, 1.0f},
    {"noise", "Noise", "source", "noise_", "", NULL, "noise_level", -60.0f, -18.0f},
    {"low", "Low", "source", "low_", "", NULL, "low_level", -60.0f, -6.0f},
    {"drive", "Drive", "filter", "drive", "sine sub click", NULL, "drive", 0.0f, 6.0f},
    {"amp", "Amp", "amp", "env_mode amp_attack amp_hold amp_decay amp_sustain amp_release attack_curve decay_curve "
                          "velocity pan volume", "drive osc2 noise low", NULL, NULL, 0.0f, 0.0f},
    {"pitch_env", "Pitch env", "mod", "pitch_amount pitch_time pitch_curve", "", "sine osc2 sub", NULL, 0.0f, 0.0f},
    {"ratchet", "Ratchet", "mod", "ratchet ratchet_div ratchet_hz ratchet_decay", "", "amp", "ratchet", 1.0f, 4.0f},
    {"gate", "Gate", "mod", "gate_", "", "amp", "gate_mode", 0.0f, 1.0f},
    {"voice", "Voice", "voice", "voice_mode glide bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t micro_engine = {
    .name = "micro",
    .description = "precision sines: per-sample envelopes, fixed or keyed pitch, pitch envelope, second sine, sub, "
                   "click, noise layer with its own gate, low layer (a sub with its own sample-exact envelope, pitch drop "
                   "and gate), tone bursts, ratchets, gate patterns (8 voices)",
    .params = micro_params,
    .param_count = MICRO_P_COUNT,
    .voices = MICRO_VOICES,
    .init = micro_init,
    .poly = micro_poly,
    .set_param = micro_set_param,
    .pitch_bend = micro_pitch_bend,
    .mod_wheel = NULL,
    .render = micro_render,
    .tempo = micro_tempo,
    .stages = micro_stages,
    .stage_count = (int)(sizeof micro_stages / sizeof micro_stages[0]),
};
