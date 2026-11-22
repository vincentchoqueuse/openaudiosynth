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
    e->click_mode_set = -1;
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

static DSP_FAST_CODE void micro_render(void *st, float *L, float *R)
{
    micro_t *e = st;
    const int n = SYNTH_BLOCK;
    const float inv_sr = 1.0f / e->sr;
    const float *p = e->p;

    const float vol0 = e->vol_prev;
    const float vol1 = smooth_next(&e->vol, fm_db2lin(p[MICRO_P_VOLUME]));
    e->vol_prev = vol1;

    /* block-rate settings */
    const float l1 = p[MICRO_P_SINE_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_SINE_LEVEL]);
    const float l2 = p[MICRO_P_OSC2_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_OSC2_LEVEL]);
    const float ls = p[MICRO_P_SUB_LEVEL] <= -59.9f ? 0.0f : fm_db2lin(p[MICRO_P_SUB_LEVEL]);
    const int split = (int)p[MICRO_P_OSC2_SPLIT], o2hz = (int)p[MICRO_P_OSC2_MODE] == 1;
    const int o2penv = (int)p[MICRO_P_OSC2_PITCH_ENV];
    const float o2ratio = p[MICRO_P_OSC2_RATIO], o2off = p[MICRO_P_OSC2_OFFSET] * inv_sr;
    const float sub_mul = (int)p[MICRO_P_SUB_OCTAVE] >= 2 ? 0.25f : 0.5f;
    const float pamt = p[MICRO_P_PITCH_AMOUNT];
    const float glide_c = p[MICRO_P_GLIDE] > 0.00075f ? voice_glide_arrive_coef(p[MICRO_P_GLIDE], 1, e->sr) : 1.0f;
    const int cm = (int)p[MICRO_P_CLICK_MODE];
    const float click_g = fm_db2lin(p[MICRO_P_CLICK_LEVEL]);
    const float click_mul = fm_exp2(-9.966f / (p[MICRO_P_CLICK_DECAY] * e->sr)); /* -60 dB after click_decay */
    const int drive_on = p[MICRO_P_DRIVE] > 0.05f;
    const int gmode = (int)p[MICRO_P_GATE_MODE];
    const int gsteps = (int)p[MICRO_P_GATE_STEPS];
    const uint32_t gpat = (uint32_t)p[MICRO_P_GATE_PATTERN];
    const float gfloor = 1.0f - p[MICRO_P_GATE_DEPTH];
    const float gslew = 1.0f / ((float)samples_of(p[MICRO_P_GATE_EDGE], e->sr));
    const float gdiv = tempo_div_beats[(int)p[MICRO_P_GATE_DIV]];
    const float ginv = gdiv > 0.0f ? 1.0f / gdiv : 4.0f;
    const float ghz = p[MICRO_P_GATE_HZ] * inv_sr;
    const float db = (e->beat1 - e->beat0) / (float)n;
    float pan_l, pan_r;
    voice_pan(p[MICRO_P_PAN], &pan_l, &pan_r);

    if (cm != e->click_mode_set) {
        for (int i = 0; i < MICRO_VOICES; i++)
            svf_set_mode(&e->v[i].click_f, cm == 2 ? SVF_BP : SVF_LP);
        e->click_mode_set = cm;
    }

    for (int vi = 0; vi < MICRO_VOICES; vi++) {
        if (!vpoly_active(&e->poly, vi))
            continue;
        micro_voice_t *v = &e->v[vi];
        const float *vm = vpoly_vmod(&e->poly, vi);
        const float shift = e->bend + vm[VM_PITCH];
        float xa[SYNTH_BLOCK], xb[SYNTH_BLOCK], ck[SYNTH_BLOCK];
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
            if (gmode > 0) {
                int step;
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

        /* level: velocity, ratchet hit, volume; the steal fade; per-voice level and pan */
        const float g0 = MICRO_GAIN * v->vel * v->hit_gain * vol0, g1 = MICRO_GAIN * v->vel * v->hit_gain * vol1;
        dsp_ramp_mul(xa, g0, g1, n);
        dsp_ramp_mul(xb, g0, g1, n);
        float fade[SYNTH_BLOCK];
        if (vpoly_fade(&e->poly, vi, fade, n))
            for (int i = 0; i < n; i++)
                xa[i] *= fade[i], xb[i] *= fade[i];
        float d = 0.0f;
        if (voicemod_cfg_active(e->poly.vm_cfg)) {
            const float vg1 = vm[VM_LEVEL] != 0.0f ? fm_db2lin(dsp_clampf(vm[VM_LEVEL], -96.0f, 24.0f)) : 1.0f;
            const float vg0 = v->vm_fresh ? vg1 : v->vm_gain;
            dsp_ramp_mul(xa, vg0, vg1, n);
            dsp_ramp_mul(xb, vg0, vg1, n);
            v->vm_gain = vg1;
            d = dsp_clampf(vm[VM_PAN], -1.0f, 1.0f);
        }
        v->vm_fresh = 0;
        dsp_guard_block(xa, n, &e->nan_count);
        dsp_guard_block(xb, n, &e->nan_count);
        if (split) { /* main sine on the left, second on the right, around the pan */
            float al, ar, bl, br;
            voice_pan(dsp_clampf(p[MICRO_P_PAN] + d - 1.0f, -1.0f, 1.0f), &al, &ar);
            voice_pan(dsp_clampf(p[MICRO_P_PAN] + d + 1.0f, -1.0f, 1.0f), &bl, &br);
            for (int i = 0; i < n; i++) {
                L[i] += xa[i] * al + xb[i] * bl;
                R[i] += xa[i] * ar + xb[i] * br;
            }
        } else {
            float pl, pr;
            voice_pan_offset(d, pan_l, pan_r, &pl, &pr);
            for (int i = 0; i < n; i++) {
                const float x = xa[i] + xb[i];
                L[i] += x * pl;
                R[i] += x * pr;
            }
        }
        const int sounding = v->stage != MICRO_ENV_IDLE || v->hit_in >= 0 || v->click_live > 0;
        vpoly_end_block(&e->poly, vi, sounding);
    }
}

static const engine_stage_t micro_stages[] = {
    {"sine", "Sine", "source", "sine_level pitch_mode freq transpose fine phase phase_reset burst", "", NULL, NULL, 0.0f, 0.0f},
    {"osc2", "Sine 2", "source", "osc2_", "", NULL, "osc2_level", -60.0f, -12.0f},
    {"sub", "Sub", "source", "sub_", "", NULL, "sub_level", -60.0f, -6.0f},
    {"click", "Click", "source", "click_", "", NULL, "click_mode", 0.0f, 1.0f},
    {"drive", "Drive", "filter", "drive", "sine sub click", NULL, "drive", 0.0f, 6.0f},
    {"amp", "Amp", "amp", "env_mode amp_attack amp_hold amp_decay amp_sustain amp_release attack_curve decay_curve "
                          "velocity pan volume", "drive osc2", NULL, NULL, 0.0f, 0.0f},
    {"pitch_env", "Pitch env", "mod", "pitch_amount pitch_time pitch_curve", "", "sine osc2 sub", NULL, 0.0f, 0.0f},
    {"ratchet", "Ratchet", "mod", "ratchet ratchet_div ratchet_hz ratchet_decay", "", "amp", "ratchet", 1.0f, 4.0f},
    {"gate", "Gate", "mod", "gate_", "", "amp", "gate_mode", 0.0f, 1.0f},
    {"voice", "Voice", "voice", "voice_mode glide bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t micro_engine = {
    .name = "micro",
    .description = "precision sines: per-sample envelopes, fixed or keyed pitch, pitch envelope, second sine, sub, "
                   "click, tone bursts, ratchets, gate patterns (8 voices)",
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
