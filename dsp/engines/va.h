/* va.h — "va" subtractive engine (M1, inspired by a 2-oscillator virtual analog).
 * Per voice: 2 polyBLEP oscillators (or supersaws) + noise, two filter slots in series or parallel (state-variable,
 * formant, screaming 2-pole), amp and filter ADSR, pitch envelope, three desynchronized detune LFOs, analog imperfections (tolerance, drift, jitter) per oscillator.
 * Global: 1 LFO (pitch, cutoff, PWM), mod wheel -> vibrato, pitch bend; poly, mono or legato with glide. */
#ifndef ENGINE_VA_H
#define ENGINE_VA_H

#include "../core/smooth.h"
#include "../env/adsr.h"
#include "../filter/vfilter.h"
#include "../mod/lfo.h"
#include "../mod/params.h"
#include "../osc/noise.h"
#include "../osc/polyblep.h"
#include "../osc/supersaw.h"
#include "../voice/analog.h"
#include "engine.h"

#define VA_VOICES 8

/* Parameter identifiers (index into va_params). */
enum {
    VA_P_OSC1_WAVE = 0, VA_P_OSC1_OCT, VA_P_OSC1_SEMI, VA_P_OSC1_DETUNE, VA_P_OSC1_PW, VA_P_OSC1_LEVEL,
    VA_P_OSC2_WAVE, VA_P_OSC2_OCT, VA_P_OSC2_SEMI, VA_P_OSC2_DETUNE, VA_P_OSC2_PW, VA_P_OSC2_LEVEL,
    VA_P_NOISE_LEVEL,
    VA_P_FILTER_MODE, VA_P_CUTOFF, VA_P_RESO, VA_P_FENV_AMT, VA_P_KEYTRACK, VA_P_VEL_CUTOFF,
    VA_P_AMP_A, VA_P_AMP_D, VA_P_AMP_S, VA_P_AMP_R,
    VA_P_FENV_A, VA_P_FENV_D, VA_P_FENV_S, VA_P_FENV_R,
    VA_P_LFO_RATE, VA_P_LFO_WAVE, VA_P_LFO_PITCH, VA_P_LFO_CUTOFF, VA_P_LFO_PWM, VA_P_WHEEL_VIB,
    VA_P_AMP_VEL, VA_P_VOLUME, VA_P_DRIFT, VA_P_SPREAD, VA_P_BEND_RANGE,
    /* LFO 2: one per voice, moves the detune of osc 2 against osc 1; its rate and depth can be
     * modulated by LFO 1 ("LFO of LFO") */
    VA_P_LFO2_RATE, VA_P_LFO2_DETUNE, VA_P_LFO2_SPREAD, VA_P_LFO2_RATE_MOD, VA_P_LFO2_DEPTH_MOD,
    /* LFO 3 (osc 1 detune) and LFO 4 (whole-voice detune): same per-voice spread and LFO-1
     * modulation as LFO 2, but pushed in other directions so the three never line up */
    VA_P_LFO3_RATE, VA_P_LFO3_DETUNE, VA_P_LFO4_RATE, VA_P_LFO4_DETUNE,
    /* supersaw (oscillator wave VA_WAVE_SUPERSAW), shared by both oscillators */
    VA_P_SS_VOICES, VA_P_SS_DETUNE, VA_P_SS_MIX, VA_P_SS_MOTION, VA_P_SS_MOTION_RATE,
    /* pitch envelope: exponential sweep from +amount semitones to the note (drums, zaps, plucks) */
    VA_P_PENV_AMT, VA_P_PENV_DECAY,
    /* monophonic play and portamento */
    VA_P_VOICE_MODE, VA_P_GLIDE, VA_P_GLIDE_MODE,
    /* filter characters and the second filter (series or parallel) */
    VA_P_F1_CHAR, VA_P_F2_MODE, VA_P_F2_CUTOFF, VA_P_F2_RESO, VA_P_F2_CHAR, VA_P_FILTER_ROUTING,
    VA_P_OVERSAMPLE,
    /* analog imperfections of each oscillator (dsp/voice/analog.h); drift above is its walk's deviation */
    VA_P_TOLERANCE, VA_P_DRIFT_TIME, VA_P_JITTER,
    VA_P_COUNT
};
extern const param_desc_t va_params[VA_P_COUNT];
extern const engine_ops_t va_engine;

/* Oscillator wave values: 0..3 = osc_wave_t (sine, saw, pulse, tri), 4 = supersaw. */
#define VA_WAVE_SUPERSAW 4

typedef struct {
    float vel, velgain, vel_cut;
    float gain_prev; /* voice gain at the previous block (ramp) */
    float penv;      /* pitch envelope level, 1 at the attack, decays to 0 */
    float pitch;     /* current pitch in semitones (glides toward the note) */
    float pan_l, pan_r;
    osc_t osc[2];
    supersaw_t ss[2]; /* used instead of osc[k] when its wave is VA_WAVE_SUPERSAW */
    noise_t noise;
    vfilter_t f1, f2; /* filter 1 (main) and filter 2 */
    adsr_t amp, fenv;
    analog_t analog[2]; /* the imperfections of osc 1 and osc 2 */
    lfo_t dlfo[3];        /* per-voice detune LFOs: [0] osc 2 (LFO 2), [1] osc 1 (LFO 3), [2] voice (LFO 4) */
    float dlfo_factor[3]; /* per-voice rate multipliers (spread) */
} va_voice_t;

typedef struct {
    float sr;
    float p[VA_P_COUNT]; /* target physical values */
    /* control-rate smoothing; *_prev = previous block value (ramp start) */
    smooth_t sm_cut, sm_res, sm_lvl[3], sm_pw[2], sm_vol;
    float lvl_prev[3], vol_prev;
    float bend, modwheel;
    float last_note; /* last note played, where the next glide starts from */
    int has_last;
    int env_dirty;
    uint32_t rng;
    lfo_t lfo;
    vpoly_t poly;
    va_voice_t v[VA_VOICES];
} va_t;

#endif /* ENGINE_VA_H */
