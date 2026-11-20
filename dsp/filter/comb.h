/* comb.h — feedback comb filter: y = x + g * damp(y delayed by sr / f), peaks at the multiples of f (g > 0) or
 * at the odd multiples of f / 2 (g < 0: hollow). The delay is read with 4-point Hermite interpolation and
 * glides over each block, so the comb can wander (modulated tuning) without zipper or clicks; a one-pole
 * low-pass in the loop makes the high peaks decay faster. Output normalized so the peaks stay near unity.
 * Delays longer than the buffer play an octave up (same harmonic series). Reference: J. O. Smith, "Physical
 * Audio Signal Processing", feedback comb filters; Hermite interpolation after Niemitalo (2001). */
#ifndef COMB_H
#define COMB_H

#include "../core/dsp.h"

#ifndef COMB_SIZE
#define COMB_SIZE 512 /* power of two: lowest comb frequency sr / (COMB_SIZE - 4) (~95 Hz at 48 kHz) */
#endif

typedef struct {
    float buf[COMB_SIZE];
    int w;
    float lp;                 /* loop low-pass state */
    float d, td, g, tg, a, ta; /* delay (samples), feedback, damping coefficient: current and block target */
    float sr;
    int snap;
} comb_t;

void comb_init(comb_t *c, float sample_rate);
void comb_reset(comb_t *c);
/* freq_hz: the comb's fundamental; feedback -0.98..0.98; damping 0..1 (0 = bright repeats). */
void comb_set(comb_t *c, float freq_hz, float feedback, float damping);
/* in and out may be the same buffer. */
void comb_process(comb_t *c, const float *in, float *out, int n);

#endif /* COMB_H */
