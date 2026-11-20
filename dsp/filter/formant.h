/* formant.h — vowel filter: three band-pass TPT state-variable filters in parallel at the formants of a vowel,
 * morphing continuously a -> e -> i -> o -> u, shiftable in octaves (smaller or larger "throat") with a
 * sharpness control. Coefficients glide over each block (the SVF interpolates), so the vowel can move.
 * Formant values: G. Peterson & H. Barney (1952), average adult vowels. */
#ifndef FORMANT_H
#define FORMANT_H

#include "svf_tpt.h"

typedef struct {
    svf_t bp[3];
    float gain[3];
} formant_t;

void formant_init(formant_t *f, float sample_rate);
void formant_reset(formant_t *f);
/* vowel 0..1 (a, e, i, o, u), shift in octaves (0 = adult voice), q = sharpness of the formants (2..20). */
void formant_set(formant_t *f, float vowel, float shift_oct, float q);
/* in and out may be the same buffer. */
void formant_process(formant_t *f, const float *in, float *out, int n);

#endif /* FORMANT_H */
