/* lfo.h — low-frequency oscillator evaluated at control rate (one value per block).
 * Shapes: sine, triangle, saw, square, sample-and-hold. Bipolar output in [-1, 1].
 * No anti-aliasing needed: consumers interpolate the value over the block. */
#ifndef LFO_H
#define LFO_H

#include "../core/dsp.h"

typedef enum { LFO_SINE = 0, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_SH, LFO_WAVE_COUNT } lfo_wave_t;

typedef struct {
    float phase, inc, inv_sr, sh;
    uint32_t rng;
    int wave;
} lfo_t;

void lfo_init(lfo_t *l, float sample_rate, uint32_t seed);
void lfo_reset(lfo_t *l, float phase);
void lfo_set(lfo_t *l, float rate_hz, lfo_wave_t wave);
/* Advances by n samples and returns the value at the end of the step. */
float lfo_advance(lfo_t *l, int n);

#endif /* LFO_H */
