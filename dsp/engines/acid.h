/* acid.h — monophonic acid bass engine, in the spirit of the early-80s transistor bass line machine:
 * one saw/square oscillator, 18 dB ladder low-pass (2x oversampled) with high resonance, a decaying
 * filter envelope, accent (velocity >= 96: louder, shorter and brighter envelope, plus an "accent
 * sweep" that builds up over consecutive accents) and slide (overlapping notes glide without
 * retriggering), then a distortion stage like a pedal after it (2x oversampled, with a tone control).
 * Behaviour reconstructed from published circuit descriptions; no code reused. */
#ifndef ENGINE_ACID_H
#define ENGINE_ACID_H

#include "../core/smooth.h"
#include "../fx/dist.h"
#include "../filter/ladder.h"
#include "../osc/polyblep.h"
#include "engine.h"

enum {
    ACID_P_WAVE = 0, ACID_P_TUNE, ACID_P_CUTOFF, ACID_P_RESO, ACID_P_ENVMOD, ACID_P_DECAY,
    ACID_P_ACCENT, ACID_P_SLIDE, ACID_P_DRIVE, ACID_P_VOLUME, ACID_P_OVERSAMPLE,
    ACID_P_DIST, ACID_P_DIST_TONE, /* the distortion after the filter, like a pedal */
    ACID_P_COUNT
};
extern const param_desc_t acid_params[ACID_P_COUNT];
extern const engine_ops_t acid_engine;

typedef struct {
    float sr;
    float p[ACID_P_COUNT];
    smooth_t sm_cut, sm_res;
    osc_t osc;
    ladder_t filter;
    float pitch;          /* current pitch (semitones), glides on slides */
    float fenv, fenv_coef; /* filter envelope (decays to 0) */
    float amp, amp_target, amp_coef_up, amp_coef_down;
    float accented, acc_env, sweep;
    float gain_prev;
    dist_t dist;     /* the distortion after the amp (dsp/fx/dist.h: tube, its bias growing with the amount) */
    int gate;
    uint32_t nan_count;
    vpoly_t poly;
} acid_t;

#endif /* ENGINE_ACID_H */
