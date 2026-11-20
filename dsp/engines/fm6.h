/* fm6.h — 6-operator phase-modulation synthesizer in the spirit of the 1983 digital FM classic:
 * 32 algorithms, 4-rate/4-level envelopes working in dB, output levels 0..99, ratio/fixed frequency
 * with fine and detune, keyboard level and rate scaling, velocity sensitivity, self feedback averaged
 * over 2 samples, pitch envelope, LFO (6 shapes, delay, pitch/amp modulation sensitivities), transpose,
 * oscillator key sync. Parameters use the instrument's native 0..99 scales so voices imported from its
 * documented sysex format map 1:1 (host/fm6_sysex.c). Behaviour reconstructed from public
 * documentation (J. Chowning 1973 for FM; the instrument's published algorithm chart and data format). */
#ifndef ENGINE_FM6_H
#define ENGINE_FM6_H

#include "../env/env_rl.h"
#include "../osc/fm_op.h"
#include "engine.h"

#define FM6_VOICES 16
#define FM6_OPS 6

/* Per-operator parameters, FM6_OP_PARAMS of them, for operators 1..6 (op 1 first). */
enum {
    FM6_R1 = 0, FM6_R2, FM6_R3, FM6_R4, FM6_L1, FM6_L2, FM6_L3, FM6_L4,
    FM6_LEVEL, FM6_MODE, FM6_COARSE, FM6_FINE, FM6_DETUNE, FM6_VEL_SENS, FM6_RATE_SCALE,
    FM6_BREAKPOINT, FM6_LDEPTH, FM6_RDEPTH, FM6_LCURVE, FM6_RCURVE, FM6_AMP_SENS,
    FM6_OP_PARAMS
};
#define FM6_OP(op, k) ((op) * FM6_OP_PARAMS + (k)) /* op = 0..5 for operators 1..6 */
enum {
    FM6_P_ALGORITHM = FM6_OPS * FM6_OP_PARAMS, FM6_P_FEEDBACK, FM6_P_OSC_SYNC, FM6_P_TRANSPOSE,
    FM6_P_PR1, FM6_P_PR2, FM6_P_PR3, FM6_P_PR4, FM6_P_PL1, FM6_P_PL2, FM6_P_PL3, FM6_P_PL4,
    FM6_P_LFO_SPEED, FM6_P_LFO_DELAY, FM6_P_LFO_PMD, FM6_P_LFO_AMD, FM6_P_LFO_SYNC, FM6_P_LFO_WAVE, FM6_P_PMS,
    FM6_P_VOLUME, FM6_P_BEND_RANGE, FM6_P_SPREAD,
    FM6_P_COUNT
};
extern const param_desc_t fm6_params[FM6_P_COUNT];
extern const engine_ops_t fm6_engine;

typedef struct {
    fm_op_t op[FM6_OPS];
    env_rl_t eg[FM6_OPS];        /* operator envelopes (level in dB, 0 = full, -96 = silent) */
    env_rl_t peg;                /* pitch envelope (level in semitones) */
    float fb1, fb2;              /* last two outputs of the feedback operator */
    float vel, gain_prev, pan_l, pan_r;
    int note, released;
    float lfo_delay_left;        /* silence before the LFO, then */
    float lfo_fade, lfo_fade_rate; /* its depth 0..1, rising by rate per second */
} fm6_voice_t;

typedef struct {
    float sr;
    float p[FM6_P_COUNT];
    float lfo_phase, lfo_sh, bend, modwheel;
    uint32_t rng, nan_count;
    vpoly_t poly;
    fm6_voice_t v[FM6_VOICES];
} fm6_t;

/* Algorithm table: for algorithm a (0..31), mod[a][op] = bit mask of operators (bit k = op k+1) that
 * modulate op (0-based); carriers[a] = mask of output operators; fb_src/fb_dst = feedback path. */
typedef struct {
    uint8_t mod[FM6_OPS];
    uint8_t carriers;
    uint8_t fb_src, fb_dst;
} fm6_algorithm_t;
extern const fm6_algorithm_t fm6_algorithms[32];

#endif /* ENGINE_FM6_H */
