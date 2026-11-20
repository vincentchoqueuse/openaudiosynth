/* poly.h — classic programmable polysynth in the spirit of the late-70s 5-voice instrument:
 * per voice osc A (saw + pulse, hard-synced to B on demand) and osc B (saw + triangle + pulse,
 * low-frequency mode, keytrack switch), noise, 24 dB ladder, filter and amp ADSR, and POLY-MOD:
 * filter envelope and osc B modulate osc A frequency and pulse width (osc B at audio rate) and the
 * cutoff. Global LFO with initial amount + mod wheel, glide, unison, analog "slop".
 * Behaviour reconstructed from public documentation; no code reused. */
#ifndef ENGINE_POLY_H
#define ENGINE_POLY_H

#include "../core/smooth.h"
#include "../env/adsr.h"
#include "../filter/ladder.h"
#include "../mod/lfo.h"
#include "../osc/noise.h"
#include "../voice/drift.h"
#include "engine.h"

#define POLY_VOICES 8

enum {
    POLY_P_OSCA_SEMI = 0, POLY_P_OSCA_SAW, POLY_P_OSCA_PULSE, POLY_P_OSCA_PW, POLY_P_OSCA_SYNC,
    POLY_P_OSCB_SEMI, POLY_P_OSCB_FINE, POLY_P_OSCB_SAW, POLY_P_OSCB_TRI, POLY_P_OSCB_PULSE, POLY_P_OSCB_PW,
    POLY_P_OSCB_LOFREQ, POLY_P_OSCB_KEYTRACK,
    POLY_P_MIX_A, POLY_P_MIX_B, POLY_P_NOISE,
    POLY_P_CUTOFF, POLY_P_RESO, POLY_P_FENV_AMT, POLY_P_KEYTRACK,
    POLY_P_FENV_A, POLY_P_FENV_D, POLY_P_FENV_S, POLY_P_FENV_R,
    POLY_P_AMP_A, POLY_P_AMP_D, POLY_P_AMP_S, POLY_P_AMP_R,
    POLY_P_PM_FENV, POLY_P_PM_OSCB, POLY_P_PM_FREQ_A, POLY_P_PM_PW_A, POLY_P_PM_FILTER,
    POLY_P_LFO_RATE, POLY_P_LFO_WAVE, POLY_P_LFO_AMOUNT, POLY_P_LFO_PITCH, POLY_P_LFO_PWM, POLY_P_LFO_FILTER,
    POLY_P_GLIDE, POLY_P_UNISON, POLY_P_UNISON_DETUNE, POLY_P_DRIFT, POLY_P_VELOCITY,
    POLY_P_VOLUME, POLY_P_SPREAD, POLY_P_BEND_RANGE, POLY_P_OVERSAMPLE,
    POLY_P_COUNT
};
extern const param_desc_t poly_params[POLY_P_COUNT];
extern const engine_ops_t poly_engine;

typedef struct {
    float pa, pb;        /* oscillator phases */
    float ya_prev;       /* osc A output delayed by one sample (to place BLEPs before a sync reset) */
    float pitch, velgain, gain_prev, pan_l, pan_r, unison_cents;
    float b_last;        /* last osc B sample (block-rate poly-mod of the filter) */
    ladder_t filter;
    adsr_t fenv, amp;
    drift_t drift;
    noise_t noise;
} poly_voice_t;

typedef struct {
    float sr;
    float p[POLY_P_COUNT];
    smooth_t sm_cut, sm_res;
    lfo_t lfo;
    float bend, modwheel, last_note;
    int has_last, env_dirty, unison;
    uint32_t rng, nan_count;
    vpoly_t poly;
    poly_voice_t v[POLY_VOICES];
} poly_t;

#endif /* ENGINE_POLY_H */
