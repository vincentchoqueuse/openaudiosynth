/* mono.h — classic monophonic synthesizer modelled on the early-70s portable modular-derived monosynth and
 * its current reissue, panel by panel from the public owner's manual: 3 free-running oscillators (6 waveforms,
 * footage LO and 32'..2', reverse saw on osc 3, osc 3 usable as a modulation source with keyboard control
 * off), white or pink noise, a mixer that overloads the filter and can take the instrument's own output back
 * in (the external input left unplugged), a 24 dB transistor-ladder filter whose emphasis thins the bass and
 * self-oscillates, keyboard control of the filter, attack/decay/sustain contours with the "decay as release"
 * switch, the modulation mix (osc 3 or filter contour against noise or the added LFO) sent by the wheel to the
 * oscillators and the filter, glide, single or multiple triggering, low / last / high note priority.
 * Behaviour reconstructed from the documentation; no code reused. */
#ifndef ENGINE_MONO_H
#define ENGINE_MONO_H

#include "../core/smooth.h"
#include "../env/adsr.h"
#include "../filter/ladder.h"
#include "../osc/noise.h"
#include "../osc/polyblep.h"
#include "../voice/analog.h"
#include "engine.h"

enum {
    MONO_P_OSC1_RANGE = 0, MONO_P_OSC1_WAVE, MONO_P_OSC1_LEVEL,
    MONO_P_OSC2_RANGE, MONO_P_OSC2_WAVE, MONO_P_OSC2_DETUNE, MONO_P_OSC2_LEVEL,
    MONO_P_OSC3_RANGE, MONO_P_OSC3_WAVE, MONO_P_OSC3_DETUNE, MONO_P_OSC3_LEVEL, MONO_P_OSC3_KEYTRACK,
    MONO_P_NOISE_LEVEL, MONO_P_MIX_DRIVE,
    MONO_P_CUTOFF, MONO_P_EMPHASIS, MONO_P_CONTOUR, MONO_P_KEYTRACK,
    MONO_P_FENV_A, MONO_P_FENV_D, MONO_P_FENV_S,
    MONO_P_AMP_A, MONO_P_AMP_D, MONO_P_AMP_S, MONO_P_RELEASE,
    MONO_P_GLIDE, MONO_P_VOICE_MODE, MONO_P_MOD_MIX, MONO_P_MOD_PITCH, MONO_P_MOD_CUTOFF,
    MONO_P_DRIFT, MONO_P_VELOCITY, MONO_P_VOLUME, MONO_P_BEND_RANGE,
    MONO_P_NOISE_COLOR, MONO_P_MOD_A, MONO_P_MOD_B, MONO_P_LFO_RATE, MONO_P_LFO_WAVE,
    MONO_P_FEEDBACK, MONO_P_PRIORITY, MONO_P_OVERSAMPLE,
    /* analog imperfections of each oscillator (dsp/voice/analog.h); drift above is its walk's deviation */
    MONO_P_TOLERANCE, MONO_P_DRIFT_TIME, MONO_P_JITTER,
    MONO_P_COUNT
};
extern const param_desc_t mono_params[MONO_P_COUNT];
#define MONO_GLIDE_OFF 0.001f /* s: the glide knob's bottom, glide off */
extern const engine_ops_t mono_engine;

typedef struct {
    float sr;
    float p[MONO_P_COUNT];
    smooth_t sm_cut, sm_emph, sm_lvl[4];
    float lvl_prev[4];
    osc_t osc[3], osc_shark[3]; /* shark = triangle + saw blend: second oscillator per slot */
    noise_t noise;
    float pink[3];        /* pink noise filter states (audio) */
    float mod_noise;      /* low-passed noise used as a modulation source (pink, or red) */
    float lfo_phase;      /* the modulation LFO, 0..1 */
    float fb_state[2];    /* output fed back to the mixer: last sample, high-pass state */
    ladder_t filter;
    adsr_t fenv, amp;
    analog_t analog[3]; /* the imperfections of each oscillator */
    float pitch, vel, gain_prev, bend, modwheel;
    int has_note, env_dirty;
    uint32_t nan_count;
    vpoly_t poly;
} mono_t;

#endif /* ENGINE_MONO_H */
