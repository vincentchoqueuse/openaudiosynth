/* coupled.h — coupled-oscillator engine: each note is a network of partials, each a Stuart-Landau oscillator
 * (normal form of the Hopf bifurcation). Its modulus saturates by itself; the modulus bends its pitch
 * (pitch_by_level: louder = flatter); the phase relations with its neighbours, compared on the fundamental
 * (n:m harmonic coupling), feed or starve its modulus (amp_coupling: > 0 cooperation, < 0 competition) and pull its
 * pitch (phase_coupling: locking onto the harmonic series). Random detune and a slow random drift keep the network
 * moving; freeze switches to diffusive coupling, where detuned partials kill each other (amplitude death).
 * Couplings are integrated once per block; partial phases and levels are interpolated per sample.
 * References: Y. Kuramoto, "Chemical Oscillations, Waves, and Turbulence" (1984); A. Pikovsky, M. Rosenblum,
 * J. Kurths, "Synchronization" (2001); the experiments in tools/experiments/. */
#ifndef ENGINE_COUPLED_H
#define ENGINE_COUPLED_H

#include "../env/adsr.h"
#include "engine.h"

#define CPL_VOICES 8
#define CPL_PARTIALS 24

enum {
    CPL_P_PARTIALS = 0, CPL_P_STRETCH, CPL_P_SPREAD, CPL_P_DRIFT, CPL_P_DRIFT_TIME, CPL_P_TILT,
    CPL_P_AMP_COUPLING, CPL_P_PHASE_COUPLING, CPL_P_PITCH_BY_LEVEL, CPL_P_REACH, CPL_P_FREEZE,
    CPL_P_ATTACK, CPL_P_DECAY, CPL_P_SUSTAIN, CPL_P_RELEASE,
    CPL_P_VOLUME, CPL_P_SPREAD_PAN, CPL_P_BEND_RANGE,
    CPL_P_COUNT
};
extern const param_desc_t coupled_params[CPL_P_COUNT];
extern const engine_ops_t coupled_engine;

typedef struct {
    float theta[CPL_PARTIALS];  /* phase brought back to the fundamental (cycles, [0, 1)): audio phase = h theta */
    float inc[CPL_PARTIALS];    /* theta increment per sample at the end of the previous block */
    float r[CPL_PARTIALS];      /* modulus */
    float amp[CPL_PARTIALS];    /* output level at the end of the previous block */
    float detune[CPL_PARTIALS]; /* fixed random detune of the note (cents) */
    float drift[CPL_PARTIALS];  /* slow random walk (cents) */
    adsr_t env;
    float gain_prev, pan_l, pan_r, vel;
    int note, fresh;
    uint32_t rng;
} coupled_voice_t;

typedef struct {
    float sr;
    float p[CPL_P_COUNT];
    float bend;
    int env_dirty;
    uint32_t rng, nan_count;
    vpoly_t poly;
    coupled_voice_t v[CPL_VOICES];
} coupled_t;

#endif /* ENGINE_COUPLED_H */
