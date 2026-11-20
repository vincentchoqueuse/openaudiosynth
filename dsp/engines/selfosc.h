/* selfosc.h — self-oscillating engine: each voice is a Van der Pol / Duffing oscillator forced by the note
 * (dsp/osc/selfosc.h): drive morphs a sine into a relaxation wave, stiffness makes the pitch follow the
 * amplitude, entrain locks it to the note (or lets it beat against it when detuned), velocity can add drive.
 * Then a filter slot (vfilter: lp/bp/hp/notch, formant, scream; or a feedback comb, dsp/filter/comb) and an amp
 * envelope. */
#ifndef ENGINE_SELFOSC_H
#define ENGINE_SELFOSC_H

#include "../env/adsr.h"
#include "../filter/comb.h"
#include "../filter/vfilter.h"
#include "../osc/selfosc.h"
#include "engine.h"

#define SO_VOICES 8

enum {
    SO_P_DRIVE = 0, SO_P_STIFFNESS, SO_P_ENTRAIN, SO_P_DETUNE, SO_P_VEL_DRIVE,
    SO_P_FILTER, SO_P_CUTOFF, SO_P_RESO, SO_P_CHARACTER, SO_P_KEYTRACK,
    SO_P_ATTACK, SO_P_DECAY, SO_P_SUSTAIN, SO_P_RELEASE,
    SO_P_VOLUME, SO_P_SPREAD, SO_P_BEND_RANGE,
    SO_P_COUNT
};
extern const param_desc_t selfosc_params[SO_P_COUNT];
extern const engine_ops_t selfosc_engine;

typedef struct {
    selfosc_t osc;
    vfilter_t filter;
    comb_t comb;
    int comb_on; /* the comb ran in the previous block (else its line holds stale sound) */
    adsr_t env;
    float vel, gain_prev, pan_l, pan_r;
    int note;
} so_voice_t;

typedef struct {
    float sr;
    float p[SO_P_COUNT];
    float bend;
    int env_dirty;
    uint32_t rng, nan_count;
    vpoly_t poly;
    so_voice_t v[SO_VOICES];
} selfosc_engine_t;

#endif /* ENGINE_SELFOSC_H */
