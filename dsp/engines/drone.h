/* drone.h — drone engine: each note is a whole drone that never repeats. Four layers: partials (harmonic sines,
 * glassy and slightly stretched when cold, with two beating twins), a body (two detuned saws in a low-pass, faded in
 * as it warms up), air (noise in two band-passes on the note's harmonics) and sparkles (rare pings of a high
 * harmonic). Every partial's level, pitch and place wander on smoothed random walks (no cycle to hear), and the two
 * slow LFOs of a note have periods in the golden ratio. Seven macros move it all: temperature (glacial .. warm),
 * density, motion, depth, brightness, sparkle, width. Envelopes up to 3 minutes.
 * References: the Ornstein-Uhlenbeck process for the walks; additive synthesis of harmonic series (J.-C. Risset). */
#ifndef ENGINE_DRONE_H
#define ENGINE_DRONE_H

#include "../filter/svf_tpt.h"
#include "../osc/polyblep.h"
#include "engine.h"

#define DRN_VOICES 8
#define DRN_PARTIALS 12
#define DRN_SINES (DRN_PARTIALS + 2) /* the partials, then the beating twins of partials 1 and 2 */

enum {
    DRN_P_TEMPERATURE = 0, DRN_P_DENSITY, DRN_P_MOTION, DRN_P_DEPTH, DRN_P_BRIGHTNESS, DRN_P_SPARKLE, DRN_P_WIDTH,
    DRN_P_PARTIALS_LEVEL, DRN_P_BODY_LEVEL, DRN_P_AIR_LEVEL, DRN_P_SPARKLE_LEVEL,
    DRN_P_ATTACK, DRN_P_RELEASE, DRN_P_VOLUME, DRN_P_BEND_RANGE,
    DRN_P_AIR_TONE, DRN_P_AIR_RESO, /* the air's bands: moved in octaves, their sharpness */
    DRN_P_COUNT
};
extern const param_desc_t drone_params[DRN_P_COUNT];
extern const engine_ops_t drone_engine;

/* A smoothed random walk: an Ornstein-Uhlenbeck process (x) through a one-pole (y), unit deviation. */
typedef struct {
    float x, y;
} drone_walk_t;

typedef struct {
    float phase[DRN_SINES], inc[DRN_SINES], al[DRN_SINES], ar[DRN_SINES]; /* per sine, end of the previous block */
    float detune[DRN_SINES];                                              /* fixed random detune of the note (cents) */
    drone_walk_t w_amp[DRN_SINES], w_pitch[DRN_SINES], w_pan[DRN_SINES];
    osc_t saw[2];
    svf_t body_lp, air_bp[2];
    uint32_t noise;
    float lfo[2];              /* phases of the two slow LFOs (cycles) */
    float ping_phase, ping_inc, ping_env, ping_peak, ping_pan, ping_up, ping_down, ping_l, ping_r;
    int ping_rising;
    float env, env_prev;       /* block-rate envelope, interpolated over the block */
    int gate, note, fresh;
    float vel, gain_prev;
    uint32_t rng;
} drone_voice_t;

typedef struct {
    float sr;
    float p[DRN_P_COUNT];
    float bend;
    uint32_t nan_count;
    vpoly_t poly;
    drone_voice_t v[DRN_VOICES];
} drone_t;

#endif /* ENGINE_DRONE_H */
