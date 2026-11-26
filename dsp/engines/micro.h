/* micro.h — precision sine engine, for exact electronic rhythms: every envelope runs per sample with exact
 * durations and adjustable curves, notes start on their sample inside the block, the pitch follows the key or
 * stays on a fixed frequency. One voice = a main sine (start phase, pitch envelope, glide), a second sine (a ratio,
 * or an offset in Hz for exact beating, optionally split left / right), a sub one or two octaves down, a drive,
 * a click (lowpassed impulse or band-passed noise burst), tone bursts of an exact number of cycles, ratchets
 * (repeats at a tempo division or a rate in Hz, each one quieter), a 16-step gate pattern (on the tempo or free,
 * up to audio rates) and a noise layer (white, stepped or periodic, filtered) with its own gate: the same steps,
 * the inverse ones (noise in the tone's holes) or its own pattern, retriggered on every step it opens; and a low
 * layer, the weight: a sine between 20 and 160 Hz at a fixed frequency or on the note's pitch folded into that octave,
 * restarted at a set phase on every hit (or every step of its own gate), with its own pitch drop and its own
 * attack / hold / decay counted in samples, a second sine a set number of hertz away (both restart together, so the beating is the same on every hit), a
 * drive for harmonics small speakers can play, always in the centre. References: exponential envelope segments as in analog contour generators (an RC charge
 * toward a target past the end level); binaural beating of two pure tones (H. W. Dove, 1839). */
#ifndef ENGINE_MICRO_H
#define ENGINE_MICRO_H

#include "../core/smooth.h"
#include "../filter/svf_tpt.h"
#include "../fx/dist.h"
#include "engine.h"

#define MICRO_VOICES 8

/* settings of a pulse (MICRO_PU(k, f): setting f of pulse k, 0-based) */
enum { MICRO_PU_MODE = 0, MICRO_PU_DIV, MICRO_PU_HZ, MICRO_PU_STEPS, MICRO_PU_PATTERN, MICRO_PU_OFFSET, MICRO_PU_EDGE,
       MICRO_PU_DEPTH, MICRO_PU_TARGET, MICRO_PU_COUNT };
#define MICRO_PULSES 4
#define MICRO_PU(k, f) (MICRO_P_PULSE + (k) * MICRO_PU_COUNT + (f))
enum {
    MICRO_P_VOICE_MODE = 0, MICRO_P_PITCH_MODE, MICRO_P_FREQ, MICRO_P_TRANSPOSE, MICRO_P_FINE, MICRO_P_GLIDE,
    MICRO_P_BEND_RANGE, MICRO_P_PHASE, MICRO_P_PHASE_RESET, MICRO_P_SINE_LEVEL,
    MICRO_P_PITCH_AMOUNT, MICRO_P_PITCH_TIME, MICRO_P_PITCH_CURVE,
    MICRO_P_ENV_MODE, MICRO_P_ATTACK, MICRO_P_HOLD, MICRO_P_DECAY, MICRO_P_SUSTAIN, MICRO_P_RELEASE,
    MICRO_P_ATTACK_CURVE, MICRO_P_DECAY_CURVE, MICRO_P_VELOCITY,
    MICRO_P_OSC2_LEVEL, MICRO_P_OSC2_MODE, MICRO_P_OSC2_RATIO, MICRO_P_OSC2_OFFSET, MICRO_P_OSC2_SPLIT,
    MICRO_P_OSC2_PITCH_ENV,
    MICRO_P_SUB_LEVEL, MICRO_P_SUB_OCTAVE, MICRO_P_DRIVE,
    MICRO_P_CLICK_MODE, MICRO_P_CLICK_LEVEL, MICRO_P_CLICK_TONE, MICRO_P_CLICK_DECAY,
    MICRO_P_BURST,
    MICRO_P_RATCHET, MICRO_P_RATCHET_DIV, MICRO_P_RATCHET_HZ, MICRO_P_RATCHET_DECAY,
    MICRO_P_GATE_MODE, MICRO_P_GATE_DIV, MICRO_P_GATE_HZ, MICRO_P_GATE_STEPS, MICRO_P_GATE_PATTERN,
    MICRO_P_GATE_DEPTH, MICRO_P_GATE_EDGE,
    MICRO_P_PAN, MICRO_P_VOLUME,
    MICRO_P_NOISE_LEVEL, MICRO_P_NOISE_TYPE, MICRO_P_NOISE_RATE, MICRO_P_NOISE_FILTER, MICRO_P_NOISE_CUTOFF,
    MICRO_P_NOISE_Q, MICRO_P_NOISE_ENV, MICRO_P_NOISE_DECAY, MICRO_P_NOISE_GATE, MICRO_P_NOISE_PATTERN,
    MICRO_P_NOISE_PAN,
    MICRO_P_LOW_LEVEL, MICRO_P_LOW_MODE, MICRO_P_LOW_FREQ, MICRO_P_LOW_PHASE, MICRO_P_LOW_DROP, MICRO_P_LOW_DROP_TIME,
    MICRO_P_LOW_ATTACK, MICRO_P_LOW_HOLD, MICRO_P_LOW_DECAY, MICRO_P_LOW_CURVE, MICRO_P_LOW_DRIVE, MICRO_P_LOW_GATE,
    MICRO_P_LOW_PATTERN, MICRO_P_LOW_BEAT, MICRO_P_LOW_BEAT_MIX,
    /* the grid: frequencies a whole number of cycles per length of the tempo; space: hard sides, sides that
     * alternate at every hit, the two channels an exact number of samples apart */
    MICRO_P_LOCK, MICRO_P_SPACE, MICRO_P_SPACE_DELAY,
    /* four pulses: on / off rhythms exact to the sample, each opening and closing the layers it targets */
    MICRO_P_PULSE,
    MICRO_P_COUNT = MICRO_P_PULSE + MICRO_PULSES * MICRO_PU_COUNT
};
extern const param_desc_t micro_params[MICRO_P_COUNT];
extern const engine_ops_t micro_engine;

/* One envelope segment from y0 to y1 in n samples: linear (a = 1, y += b) or exponential toward a target t past
 * (or before) the end, y = t + (y - t) * a. Its value is recomputed exactly at the start of every block (from k),
 * so long segments do not drift; within a block the recursion runs. */
#define MICRO_DELAY_MAX 128 /* a power of two above the largest space_delay (96 samples) */

typedef struct {
    float y, y0, y1, t, a, la, b;
    int n, k, lin;
} micro_seg_t;

enum { MICRO_ENV_IDLE = 0, MICRO_ENV_ATTACK, MICRO_ENV_HOLD, MICRO_ENV_DECAY, MICRO_ENV_SUSTAIN, MICRO_ENV_RELEASE };

typedef struct {
    micro_seg_t amp, pitch;  /* amplitude (0..1), pitch envelope (1 -> 0) */
    int stage, gate, pitch_on;
    float ph1, ph2, phs;     /* phases (cycles) of the main sine, the second sine, the sub */
    float semi, semi_target; /* glided pitch (MIDI note number, fractional) */
    int note;
    float vel, hit_gain;     /* velocity gain, gain of the current ratchet hit */
    int hit_in, off_in;      /* samples until the next hit / the note-off inside this block (-1: none) */
    int hits_left, hit_index, next_note, glide_next;
    float burst_left;        /* cycles left in a tone burst (< 0: no burst) */
    int tone_on;
    float gate_g, gate_phase; /* pattern gain, free-running step position */
    svf_t click_f;
    float click_amp, click_imp;
    int click_live;
    uint32_t noise;
    dist_t dist;
    svf_t noise_f;                  /* the noise layer's filter */
    float noise_amp, noise_ph;      /* its own envelope (hit, step modes), the clock of the stepped kinds */
    float noise_hold, ngate_g;      /* held value, gate gain of the noise */
    uint32_t noise_rng, lfsr;       /* its generator, the periodic kind's 7-bit shift register */
    int nstep;                      /* the step the noise gate was on at the last sample (-1: none yet) */
    micro_seg_t low_amp, low_pitch; /* the low layer: its envelope (0..1), its pitch drop (1 -> 0) */
    int low_stage, low_pitch_on;    /* MICRO_ENV_IDLE / ATTACK / HOLD / DECAY */
    float low_ph, low_semi;         /* its phase (cycles), its pitch (MIDI note number) */
    float low_ph2;                  /* phase of its second sine, a few hertz away (the beating) */
    float low_tail, low_tail_d;     /* what the last hit was still playing when this one cut it: fades in 2 ms */
    int lstep;                      /* the step its gate was on at the last sample */
    float vm_gain;
    int vm_fresh;
    float side; /* space alternate: the side of the current hit, -1 left, +1 right */
    float pu_g[MICRO_PULSES];  /* each pulse's gain, slewed (-1: set on the first sample) */
    int32_t pu_n[MICRO_PULSES];  /* free pulses: samples since the note */
    int32_t pu_q[MICRO_PULSES];  /* free pulses: steps since the note */
    int64_t pu_nb[MICRO_PULSES]; /* free pulses: the sample (since the note) where the next step starts */
} micro_voice_t;

typedef struct {
    float sr;
    float p[MICRO_P_COUNT];
    float bend;
    float bpm, beat0, beat1; /* tempo, beat position at the start and the end of the current block */
    int have_beat;
    int click_mode_set, noise_filter_set;
    smooth_t vol;
    float vol_prev;
    vpoly_t poly;
    micro_voice_t v[MICRO_VOICES];
    float dly[2][MICRO_DELAY_MAX]; /* space_delay: the last samples of each channel */
    /* the sample clock of the pulses on the beat: samples since beat 0, an integer, so that a step of a whole number
     * of samples is that many samples, forever; it follows the song's beat and resyncs on a jump or a new tempo */
    int64_t clk, clk_end;
    float clk_bpm;
    int clk_ok;
    int dly_w;
} micro_t;

#endif /* ENGINE_MICRO_H */
