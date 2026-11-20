/* sine.h — reference engine (M0): polyphonic sine with one ADSR.
 * Used to validate the full chain (MIDI, voices, master) and tuning accuracy. */
#ifndef ENGINE_SINE_H
#define ENGINE_SINE_H

#include "../core/smooth.h"
#include "../env/adsr.h"
#include "../osc/polyblep.h"
#include "engine.h"

#define SINE_VOICES 8

enum { SINE_P_ATTACK = 0, SINE_P_DECAY, SINE_P_SUSTAIN, SINE_P_RELEASE, SINE_P_VOLUME, SINE_P_COUNT };
extern const param_desc_t sine_params[SINE_P_COUNT];
extern const engine_ops_t sine_engine;

typedef struct {
    float velgain;
    osc_t osc;
    adsr_t env;
} sine_voice_t;

typedef struct {
    float sr;
    float p[SINE_P_COUNT];
    smooth_t vol;
    float vol_prev, bend_semi;
    int env_dirty;
    uint32_t nan_count;
    vpoly_t poly;
    sine_voice_t v[SINE_VOICES];
} sine_engine_t;

#endif /* ENGINE_SINE_H */
