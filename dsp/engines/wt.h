/* wt.h — wavetable synthesizer in the spirit of modern wavetable instruments: two wavetable oscillators
 * (9 generated tables, position morphing, unison up to 8, warps: sync, bend, mirror, FM from B, ring by B;
 * osc B's FM/ring source is the sub), sine sub one octave down, noise, one filter (12 dB state-variable
 * LP/BP/HP/notch or 24 dB ladder with drive), amp envelope, a modulation envelope, two per-voice LFOs and a
 * 6-slot modulation matrix (dsp/mod/modmatrix.h). Mono, legato and glide. No code from any product. */
#ifndef ENGINE_WT_H
#define ENGINE_WT_H

#include "../env/adsr.h"
#include "../filter/comb.h"
#include "../filter/ladder.h"
#include "../filter/svf_tpt.h"
#include "../filter/vfilter.h"
#include "../mod/lfo.h"
#include "../mod/modmatrix.h"
#include "../osc/wavetable.h"
#include "engine.h"

#define WT_VOICES 8
#define WT_SLOTS 6

/* per-oscillator parameters (osc A = 0, osc B = 1) */
enum { WT_O_TABLE = 0, WT_O_POSITION, WT_O_WARP_MODE, WT_O_WARP, WT_O_LEVEL, WT_O_OCTAVE, WT_O_SEMI, WT_O_FINE,
       WT_O_UNISON, WT_O_DETUNE, WT_O_COUNT };
#define WT_OSC(o, k) ((o) * WT_O_COUNT + (k))
/* per-slot parameters of the modulation matrix */
enum { WT_M_SRC = 0, WT_M_DST, WT_M_AMOUNT, WT_M_VIA, WT_M_COUNT };
#define WT_MOD(s, k) (WT_P_MOD + (s) * WT_M_COUNT + (k))

enum {
    WT_P_SUB = 2 * WT_O_COUNT, WT_P_NOISE,
    WT_P_FILTER_TYPE, WT_P_CUTOFF, WT_P_RESO, WT_P_DRIVE, WT_P_KEYTRACK,
    WT_P_AMP_A, WT_P_AMP_D, WT_P_AMP_S, WT_P_AMP_R,
    WT_P_ENV2_A, WT_P_ENV2_D, WT_P_ENV2_S, WT_P_ENV2_R,
    WT_P_LFO1_RATE, WT_P_LFO1_WAVE, WT_P_LFO1_RETRIG, WT_P_LFO2_RATE, WT_P_LFO2_WAVE, WT_P_LFO2_RETRIG,
    WT_P_MOD,
    WT_P_VOICE_MODE = WT_P_MOD + WT_SLOTS * WT_M_COUNT, WT_P_GLIDE, WT_P_VELOCITY, WT_P_VOLUME, WT_P_SPREAD,
    WT_P_BEND_RANGE, WT_P_OVERSAMPLE,
    WT_P_COUNT
};
extern const param_desc_t wt_params[WT_P_COUNT];
extern const engine_ops_t wt_engine;

/* modulation sources and destinations (index 0 = none) */
enum { WT_SRC_NONE = 0, WT_SRC_ENV2, WT_SRC_AMP_ENV, WT_SRC_LFO1, WT_SRC_LFO2, WT_SRC_VELOCITY, WT_SRC_NOTE,
       WT_SRC_MODWHEEL, WT_SRC_RANDOM, WT_SRC_COUNT };
enum { WT_DST_NONE = 0, WT_DST_PITCH, WT_DST_A_POS, WT_DST_A_WARP, WT_DST_A_LEVEL, WT_DST_B_POS, WT_DST_B_WARP,
       WT_DST_B_LEVEL, WT_DST_CUTOFF, WT_DST_RESO, WT_DST_SUB, WT_DST_NOISE, WT_DST_DETUNE, WT_DST_PAN, WT_DST_AMP,
       WT_DST_COUNT };

typedef struct {
    wt_osc_t osc[2];
    float sub_phase;
    svf_t svf;
    ladder_t ladder;
    comb_t comb;
    vfilter_t vf; /* formant and scream types */
    adsr_t amp, env2;
    lfo_t lfo[2];
    float pitch, vel, rnd, pan_pos, gain_prev;
    float lvl_prev[4]; /* A, B, sub, noise mix levels at the end of the previous block */
    int note;
    int comb_on; /* the comb ran in the previous block (else its line holds stale sound) */
    uint32_t rng;
} wt_voice_t;

typedef struct {
    float sr;
    float p[WT_P_COUNT];
    mod_slot_t slots[WT_SLOTS];
    const float *bank; /* factory tables (shared memory) */
    int bank_count;
    float bend, modwheel, last_note;
    int has_last, env_dirty;
    uint32_t rng, nan_count;
    vpoly_t poly;
    wt_voice_t v[WT_VOICES];
} wt_t;

#endif /* ENGINE_WT_H */
