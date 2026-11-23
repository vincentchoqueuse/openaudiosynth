/* fm4.h — 4-operator phase-modulation synthesizer built on a free modulation matrix: every operator can modulate
 * every other one and itself (the diagonal is its feedback), and a column of output levels says which operators are
 * heard. The usual algorithms (stacks, pairs, one modulator over three carriers...) are just matrices with some
 * cells set, and every cell is a modulatable parameter, so an LFO or an MSEG can move one modulation index alone.
 * Per operator: ratio or fixed frequency, detune, an exponential ADSR, velocity and keyboard level scaling; per voice:
 * a decaying pitch envelope and a filter with its envelope. Reference: J. Chowning, "The Synthesis of Complex Audio
 * Spectra by Means of Frequency Modulation" (JAES 1973); phase modulation as on the classic digital FM instruments. */
#ifndef ENGINE_FM4_H
#define ENGINE_FM4_H

#include "../env/adsr.h"
#include "../filter/vfilter.h"
#include "../osc/fm_op.h"
#include "engine.h"

#define FM4_VOICES 16
#define FM4_OPS 4

/* Per-operator parameters, FM4_OP_PARAMS of them, for operators 1..4 (op 1 first). */
enum {
    FM4_COARSE = 0, FM4_FINE, FM4_DETUNE, FM4_MODE, FM4_FREQ,
    FM4_ATTACK, FM4_DECAY, FM4_SUSTAIN, FM4_RELEASE, FM4_VEL_SENS, FM4_KEY_SCALE,
    FM4_OP_PARAMS
};
#define FM4_OP(op, k) ((op) * FM4_OP_PARAMS + (k)) /* op = 0..3 for operators 1..4 */
/* The matrix: FM4_P_MOD(src, dst) = how much operator src modulates operator dst (0-based); src == dst: feedback.
 * FM4_P_OUT(k) = output level of operator k (0: a pure modulator). */
#define FM4_P_MATRIX (FM4_OPS * FM4_OP_PARAMS)
#define FM4_P_MOD(src, dst) (FM4_P_MATRIX + (src) * FM4_OPS + (dst))
#define FM4_P_OUTS (FM4_P_MATRIX + FM4_OPS * FM4_OPS)
#define FM4_P_OUT(k) (FM4_P_OUTS + (k))
enum {
    FM4_P_PITCH_AMOUNT = FM4_P_OUTS + FM4_OPS, FM4_P_PITCH_DECAY, FM4_P_PHASE_RESET,
    FM4_P_FILTER_TYPE, FM4_P_CUTOFF, FM4_P_RESO, FM4_P_KEYTRACK, FM4_P_FENV_AMT,
    FM4_P_FENV_A, FM4_P_FENV_D, FM4_P_FENV_S, FM4_P_FENV_R, FM4_P_OVERSAMPLE,
    FM4_P_VOLUME, FM4_P_SPREAD, FM4_P_BEND_RANGE,
    FM4_P_COUNT
};
extern const param_desc_t fm4_params[FM4_P_COUNT];
extern const engine_ops_t fm4_engine;

/* Phase deviation (cycles) of a matrix cell at 1 driven by an operator at full level: the cell's value is squared
 * (fine control of small indices), so 0.5 gives a quarter of it. */
#define FM4_MOD_MAX 2.0f
#define FM4_FB_MAX 0.5f /* the same for the feedback cells (averaged over 2 samples, as the classic instruments do) */

typedef struct {
    fm_op_t op[FM4_OPS];
    adsr_t eg[FM4_OPS];
    adsr_t fenv;
    vfilter_t vf;
    float o1[FM4_OPS], o2[FM4_OPS];  /* each operator's last two outputs (feedback, and links run a sample late) */
    float m_prev[FM4_OPS * FM4_OPS]; /* the matrix (phase deviation per unit of the source) at the end of the last block */
    float out_prev[FM4_OPS], lvl_prev[FM4_OPS];
    float vel, penv, pan_l, pan_r, gain_prev;
    int note, filter_on;
} fm4_voice_t;

typedef struct {
    float sr;
    float p[FM4_P_COUNT];
    float bend;
    uint32_t rng, nan_count;
    vpoly_t poly;
    fm4_voice_t v[FM4_VOICES];
} fm4_t;

#endif /* ENGINE_FM4_H */
