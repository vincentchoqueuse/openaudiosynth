/* havoc.h — noise machine with a wide palette: one note runs four generators, each one of sixteen very different
 * processes (a sine with phase feedback, the logistic map, an integer formula on a counter, random impulses into a
 * resonator, a whistling band of noise, a pulsar train, a cellular automaton read as a wave, a swarm of wandering
 * sines, a tone that jumps at random, a clipped random walk, a chatter of bleeps, electronic faults, a formant voice speaking no language, a data
 * transmission, a model neuron firing, an oscillator pushed past the Nyquist frequency). Each generator has its own pitch (following the note
 * or not), two shape controls whose meaning depends on the process and that its own slow oscillator can move (two
 * sines at rates without a common period, so the timbre never comes back the same way), a life (steady, random bursts, a periodic
 * pulse, a slow swell) and a place; it can be frequency-modulated and ring-modulated by the generator before it
 * (the four form a ring). The sum goes through two processors in series, each one of: filter (low, band, high), comb,
 * wavefolder, resampler with bit reduction, ring modulator, chopper, stutter, octave divider, warp, mangle. The output can come back into every generator's
 * frequency (feedback), and the machine can mutate while it plays: at random, a generator gets other shapes, another
 * pitch, another process.
 * References: G. Peterson and H. Barney, Control methods used in a study of the vowels (formant frequencies);
 * E. Izhikevich, Simple model of spiking neurons; R. May, Simple mathematical models with very complicated dynamics (the logistic map); S. Wolfram,
 * Statistical mechanics of cellular automata (the 256 elementary rules); C. Roads, Microsound (pulsar synthesis,
 * dust); TPT state-variable filter (V. Zavalishin). */
#ifndef ENGINE_HAVOC_H
#define ENGINE_HAVOC_H

#include "engine.h"

#define HAVOC_VOICES 1 /* one machine: overlapping notes slide it (legato) */
#define HAVOC_GEN 4
#define HAVOC_PROC 2
#define HAVOC_SWARM 8
#define HAVOC_DELAY 2048 /* samples of a comb processor, per side, 16 bits each */

enum {
    HAVOC_T_TONE = 0, HAVOC_T_LOGISTIC, HAVOC_T_BYTE, HAVOC_T_DUST, HAVOC_T_WIND, HAVOC_T_PULSAR, HAVOC_T_CELL,
    HAVOC_T_SWARM, HAVOC_T_COMPUTER, HAVOC_T_RUMBLE, HAVOC_T_BLEEP, HAVOC_T_GLITCH,
    HAVOC_T_TALK, HAVOC_T_MODEM, HAVOC_T_NEURON, HAVOC_T_ALIAS, HAVOC_T_COUNT
};
enum { HAVOC_LIFE_STEADY = 0, HAVOC_LIFE_BURSTS, HAVOC_LIFE_PULSE, HAVOC_LIFE_SWELL };
enum {
    HAVOC_X_OFF = 0, HAVOC_X_LP, HAVOC_X_BP, HAVOC_X_HP, HAVOC_X_COMB, HAVOC_X_FOLD, HAVOC_X_CRUSH, HAVOC_X_RING,
    HAVOC_X_CHOP, HAVOC_X_STUTTER, HAVOC_X_SUB, HAVOC_X_WARP, HAVOC_X_MANGLE, HAVOC_X_COUNT
};

/* Settings of a generator and of a processor. */
enum {
    HAVOC_G_TYPE = 0, HAVOC_G_PITCH, HAVOC_G_TRACK, HAVOC_G_A, HAVOC_G_B, HAVOC_G_FM, HAVOC_G_RING, HAVOC_G_LIFE,
    HAVOC_G_LIFE_RATE, HAVOC_G_LIFE_SHAPE, HAVOC_G_LEVEL, HAVOC_G_PAN, HAVOC_G_LFO_RATE, HAVOC_G_LFO_A, HAVOC_G_LFO_B, HAVOC_G_ROT,
    HAVOC_G_COUNT
};
enum { HAVOC_X_TYPE = 0, HAVOC_X_P1, HAVOC_X_P2, HAVOC_X_MIX, HAVOC_X_PCOUNT };

enum {
    HAVOC_P_ATTACK = 0, HAVOC_P_RELEASE, HAVOC_P_GLIDE, HAVOC_P_VELOCITY, HAVOC_P_VOLUME, HAVOC_P_BEND_RANGE,
    HAVOC_P_WIDTH, HAVOC_P_FEEDBACK, HAVOC_P_MUTATE, HAVOC_P_MUTATE_DEPTH, HAVOC_P_DARK,
    HAVOC_P_PROCS,                                             /* then HAVOC_PROC x HAVOC_X_PCOUNT */
    HAVOC_P_GENS = HAVOC_P_PROCS + HAVOC_PROC * HAVOC_X_PCOUNT, /* then HAVOC_GEN x HAVOC_G_COUNT */
    HAVOC_P_COUNT = HAVOC_P_GENS + HAVOC_GEN * HAVOC_G_COUNT
};
#define HAVOC_G_P(g, k) (HAVOC_P_GENS + (g) * HAVOC_G_COUNT + (k))   /* setting k of generator g (0-based) */
#define HAVOC_X_P(x, k) (HAVOC_P_PROCS + (x) * HAVOC_X_PCOUNT + (k)) /* setting k of processor x (0-based) */
extern const param_desc_t havoc_params[HAVOC_P_COUNT];
extern const engine_ops_t havoc_engine;

typedef struct {
    float ph, ph2, inc;   /* phases (cycles), phase increment at the end of the last block */
    float s0, s1, s2, s3; /* state of the process (feedback memory, map values, held levels) */
    float ic1, ic2;       /* filter state (dust, wind) */
    uint32_t u0;          /* integer state: the counter of the formula, the cells of the automaton */
    uint32_t rng;
    float sw_ph[HAVOC_SWARM], sw_x[HAVOC_SWARM]; /* swarm: phases and random walks */
    float life_ph, life_env, walk;               /* life: phase of the pulse, envelope; walk of the gusts */
    float jump_ph;                               /* computer: phase of the jumps; bleep, glitch: time left */
    float ev_oct, ev_sweep, ev_gain;             /* bleep, glitch: pitch (octaves), its slope (per second), level */
    int ev_kind, ev_len;                         /* bleep: waveform; glitch: fault, cells of the stuck loop */
    int life_on;
    float gain; /* life gain at the end of the last block */
    float lfo, lfo2; /* phases of the two sines that move a and b (cycles) */
    float mut_a, mut_b, mut_pitch; /* what the last mutation left on a, b and the pitch (semitones) */
    float rot_ph, rot_hold; /* rot: clock of the resampling, held sample */
    float tk_f[3], tk_t[3], tk_ic[6]; /* talk: formant frequencies (Hz), their targets, the three filters' state */
    int tk_voiced, md_left;           /* talk: voiced phoneme; modem: symbols left in the section */
    int mut_type;                  /* and on the process (added to the type, modulo their number) */
} havoc_gen_t;

typedef struct {
    float ic1[2], ic2[2], hold[2]; /* per side: filter state, held sample of the resampler */
    float cph, ph, gate, dly;      /* resampler clock, phase (ring, chopper), chopper gain, comb delay (samples) */
    int wr;
    int stuck, pos, len; /* stutter: repeating, place in the fragment, its length (samples) */
    float wait;          /* stutter: seconds to its next change */
    float sub_env[2], sub_lp[2]; /* sub: level follower, smoothed square, per side */
    int sub_sgn[2], sub_ff[2];   /* sub: sign of the input, the two dividers (bits 0 and 1) */
    uint32_t rng;
    int16_t buf[2][HAVOC_DELAY];
} havoc_proc_t;

typedef struct {
    havoc_gen_t gen[HAVOC_GEN];
    havoc_proc_t proc[HAVOC_PROC];
    float fb[SYNTH_BLOCK];   /* last block of the last generator: it modulates the first */
    float ofb[SYNTH_BLOCK];  /* last block of the machine's output (mono): the feedback */
    uint32_t rng;            /* the mutations */
    float semi, semi_target; /* glided pitch (MIDI note number) */
    float env, vel, gain_prev;
    float dc_xl, dc_yl, dc_xr, dc_yr; /* DC blocker of the output */
    float dk[4];                      /* the dark filter: two states per side */
    int gate, fresh;
} havoc_voice_t;

typedef struct {
    float sr;
    float p[HAVOC_P_COUNT];
    float bend;
    uint32_t nan_count;
    vpoly_t poly;
    havoc_voice_t v[HAVOC_VOICES];
} havoc_t;

#endif /* ENGINE_HAVOC_H */
