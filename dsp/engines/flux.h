/* flux.h — feedback noise machine: one note is eight parallel channels that blend into one complex signal, from
 * slow breathing atmospheres to crackling harshness. A channel = an oscillator (a pulse, or a parabolic sine)
 * feeding a short feedback delay (0.04 .. 42 ms: a comb, a resonator, a flutter) with, in the loop, a wavefolder, a
 * level regulator (the loop holds its level instead of dying or exploding), a filter and a saturation. The channels
 * are not independent: each one is modulated by its neighbour's output (the flow: frequency, amplitude or delay
 * time, by mode) and can feed the next one's loop (cross: the eight delays become one network). Every setting of a
 * channel is a value 0..1 (a bar); four master controls reshape the eight bars of pitch, cutoff, delay time and
 * flow at once instead of offsetting them. Random impulses excite the loops (crackle), and every channel's pitch,
 * time, cutoff, level and place wander on slow random walks, so a held note never repeats.
 * References: feedback comb filters and their stability (J. O. Smith, Physical Audio Signal Processing); the
 * Ornstein-Uhlenbeck process for the walks; TPT state-variable filter (V. Zavalishin). */
#ifndef ENGINE_FLUX_H
#define ENGINE_FLUX_H

#include "engine.h"

#define FLUX_VOICES 1 /* one machine: overlapping notes slide it (legato) */
#define FLUX_CH 8
#define FLUX_DELAY 2048 /* samples per channel, 16 bits each (the loop is bounded to +-1) */

/* Settings of a channel, FLUX_CH values each (the bars), stored parameter by parameter. */
enum { FLUX_C_PITCH = 0, FLUX_C_PULSE, FLUX_C_CUTOFF, FLUX_C_TIME, FLUX_C_FEEDBACK, FLUX_C_FLOW, FLUX_C_LEVEL, FLUX_C_COUNT };

enum {
    FLUX_P_MODE = 0, FLUX_P_MAP_PITCH, FLUX_P_MAP_CUTOFF, FLUX_P_MAP_TIME, FLUX_P_MAP_FLOW,
    FLUX_P_FILTER_TYPE, FLUX_P_RESO, FLUX_P_NORMALIZE, FLUX_P_FOLD, FLUX_P_CROSS, FLUX_P_CRACKLE,
    FLUX_P_MOTION, FLUX_P_DEPTH, FLUX_P_WIDTH,
    FLUX_P_ATTACK, FLUX_P_RELEASE, FLUX_P_GLIDE, FLUX_P_VELOCITY, FLUX_P_VOLUME, FLUX_P_BEND_RANGE,
    FLUX_P_CHANNELS, /* then FLUX_C_COUNT x FLUX_CH values */
    FLUX_P_COUNT = FLUX_P_CHANNELS + FLUX_C_COUNT * FLUX_CH
};
#define FLUX_CH_P(c, k) (FLUX_P_CHANNELS + (c) * FLUX_CH + (k)) /* setting c of channel k (0-based) */
extern const param_desc_t flux_params[FLUX_P_COUNT];
extern const engine_ops_t flux_engine;

enum { FLUX_MODE_LOOP = 0, FLUX_MODE_PRE, FLUX_MODE_PARA };

/* A smoothed random walk: an Ornstein-Uhlenbeck process (x) through a one-pole (y), unit deviation. */
typedef struct {
    float x, y;
} flux_walk_t;

enum { FLUX_W_PITCH = 0, FLUX_W_TIME, FLUX_W_CUTOFF, FLUX_W_LEVEL, FLUX_W_PAN, FLUX_W_COUNT };

typedef struct {
    float ph;                 /* oscillator phase (cycles) */
    float inc, dly, fb, flow; /* per sample: phase increment, delay (samples), feedback, flow depth */
    float pw, gl, gr;         /* pulse width, gains to the left and the right */
    float ic1, ic2;           /* filter state */
    float a1, a2, a3, m0, m1, m2; /* filter coefficients and output mix */
    float env, ng;            /* level follower in the loop, regulator gain */
    float hx, hy, hc;         /* high-pass in the loop, at an eighth of the loop's fundamental: no offset piles up, and the
                                 loop's own resonance stays its strongest mode under the regulator */
    float r, y;               /* last loop signal (after the filter), last written sample */
    int imp_at;               /* sample of this block that gets an impulse (-1: none) */
    float imp;
    flux_walk_t w[FLUX_W_COUNT];
    int16_t buf[FLUX_DELAY];
} flux_chan_t;

typedef struct {
    flux_chan_t ch[FLUX_CH];
    int wr;                 /* write index of the delays */
    float semi, semi_target; /* glided pitch (MIDI note number) */
    float env, vel, gain_prev;
    float dc_xl, dc_yl, dc_xr, dc_yr; /* DC blocker of the output */
    int gate, fresh;
    uint32_t rng;
} flux_voice_t;

typedef struct {
    float sr;
    float p[FLUX_P_COUNT];
    float bend;
    uint32_t nan_count;
    vpoly_t poly;
    flux_voice_t v[FLUX_VOICES];
} flux_t;

#endif /* ENGINE_FLUX_H */
