/* supersaw.h — stack of up to 7 detuned polyBLEP saws ("supersaw"), with irregular detune spacing
 * (no common beat period, cf. A. Szabo, "How to Emulate the Super Saw", 2010), random start phases,
 * loudness-normalized side mix, and one slow desynchronized LFO per saw that moves its detune
 * ("motion"). Per-saw LFOs run at control rate; pitch is interpolated by each saw over the block. */
#ifndef SUPERSAW_H
#define SUPERSAW_H

#include "polyblep.h"

#define SUPERSAW_MAX 7

typedef struct {
    osc_t saw[SUPERSAW_MAX];
    float lfo_phase[SUPERSAW_MAX];
    float lfo_factor[SUPERSAW_MAX]; /* per-saw rate multiplier, random in [0.6, 1.4] */
    float gain_center, gain_side;   /* targets for the current block */
    float g_center_prev, g_side_prev;
    int count;
    uint32_t rng;
} supersaw_t;

void supersaw_init(supersaw_t *s, float sample_rate, uint32_t seed);
/* New note: random phases for every saw and new random LFO rates/phases; no pitch glide. */
void supersaw_reset(supersaw_t *s);
/* freq_hz: centre pitch; detune_cents: outermost saw offset; side: level of the 6 side saws (0..1);
 * motion_cents / motion_hz: depth and rate of the per-saw detune LFOs; count: 1, 3, 5 or 7 saws.
 * Call once per block, before supersaw_process. */
void supersaw_set(supersaw_t *s, float freq_hz, float detune_cents, float side, float motion_cents, float motion_hz,
                  int count, int block);
void supersaw_process(supersaw_t *s, float *restrict out, int n);

#endif /* SUPERSAW_H */
