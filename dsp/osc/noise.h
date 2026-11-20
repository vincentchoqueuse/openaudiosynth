/* noise.h — uniform white noise in [-1, 1) via xorshift32 (Marsaglia 2003).
 * Deterministic: same seed -> bit-identical sequence. */
#ifndef NOISE_H
#define NOISE_H

#include "../core/dsp.h"

typedef struct {
    uint32_t state;
} noise_t;

static inline void noise_init(noise_t *z, uint32_t seed) { z->state = dsp_seed(seed); }

static inline void noise_process(noise_t *z, float *restrict out, int n)
{
    uint32_t s = z->state;
    for (int i = 0; i < n; i++)
        out[i] = dsp_rand_bipolar(&s);
    z->state = s;
}

#endif /* NOISE_H */
