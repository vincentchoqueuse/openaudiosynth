/* modal.h — modal synthesis engine: each voice is a bank of up to 32 damped resonators (modes)
 * excited by a mallet strike (raised-cosine pulse + noise burst) and/or a continuous "bow" (filtered
 * noise). Mode frequency ratios come from physical models (ideal string, stiff string, free bar,
 * tuned marimba bar, church bell, circular membrane, bowl, closed tube); decay, damping of high
 * modes, brightness and strike position shape the timbre.
 * References: J.-M. Adrien, "The missing link: modal synthesis" (1991); P. Cook, "Real Sound
 * Synthesis for Interactive Applications" (2002); N. Fletcher & T. Rossing, "The Physics of
 * Musical Instruments" (mode ratios, stiff-string inharmonicity f_k = k f0 sqrt(1 + B k^2));
 * G. Weinreich, "Coupled piano strings" (JASA 1977) for the unison beating and double decay. */
#ifndef ENGINE_MODAL_H
#define ENGINE_MODAL_H

#include "../filter/resonator.h"
#include "../core/smooth.h"
#include "engine.h"

#define MODAL_VOICES 12 /* pianos with long releases need more than 8 */
#define MODAL_MODES 32

enum {
    MODAL_P_MODEL = 0, MODAL_P_MODES, MODAL_P_STRETCH,
    MODAL_P_DECAY, MODAL_P_DAMPING, MODAL_P_RELEASE,
    MODAL_P_BRIGHTNESS, MODAL_P_POSITION, MODAL_P_HARDNESS, MODAL_P_NOISE, MODAL_P_VEL_BRIGHT,
    MODAL_P_BOW, MODAL_P_BOW_ATTACK,
    MODAL_P_VARIATION, MODAL_P_VOLUME, MODAL_P_SPREAD, MODAL_P_BEND_RANGE,
    /* piano-oriented additions (string model): stiffness, unison strings, pitch-dependent decay */
    MODAL_P_INHARMONICITY, MODAL_P_UNISON, MODAL_P_DECAY_KEYTRACK, MODAL_P_AFTERSOUND,
    /* string nonlinearity: the vibration raises the tension (pitch glide, energy shared between modes) */
    MODAL_P_TENSION,
    MODAL_P_COUNT
};
extern const param_desc_t modal_params[MODAL_P_COUNT];
extern const engine_ops_t modal_engine;

typedef struct {
    reso_t mode[MODAL_MODES];               /* one complex resonator per mode */
    float g_strike[MODAL_MODES], g_bow[MODAL_MODES];
    float jitter[MODAL_MODES];              /* per-note random detune factors */
    float vm_pitch, vm_bright, vm_timbre;   /* per-voice modulation the modes were built with */
    float c0[MODAL_MODES], s0[MODAL_MODES]; /* rotation of each mode at rest (the tension bends it) */
    float inc[MODAL_MODES], w[MODAL_MODES]; /* mode frequency (cycles per sample), weight (f / f0)^2 */
    float ratio[MODAL_MODES], decay[MODAL_MODES]; /* frequency ratio to f0 and decay factor per sample of each mode,
                                                  * kept so a pitch change recomputes only the frequencies */
    float last_ratio, cut_ratio;            /* ratio of the highest partial kept, of the first one cut at 0.45 SR (0: none) */
    int nmodes;
    uint32_t version;                       /* parameter version the coefficients were built for */
    int released, coeffs_released;
    float vel, gain_prev, pan_l, pan_r;
    int strike_pos, strike_len;
    float strike_amp, noise_env, noise_decay;
    float bow_env, lp;
    uint32_t rng;
    float energy;
} modal_voice_t;

typedef struct {
    float sr;
    float p[MODAL_P_COUNT];
    uint32_t version; /* incremented when a parameter or the bend changes */
    float bend;
    smooth_t vol;
    float vol_prev;
    uint32_t rng;
    vpoly_t poly;
    modal_voice_t v[MODAL_VOICES];
} modal_t;

#endif /* ENGINE_MODAL_H */
