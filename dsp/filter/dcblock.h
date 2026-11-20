/* dcblock.h — DC blocker: 1-pole / 1-zero high-pass at ~10 Hz,
 * y[n] = x[n] - x[n-1] + R*y[n-1] with R = 1 - 2*pi*fc/SR (J. O. Smith, "Introduction to Digital Filters"). */
#ifndef DCBLOCK_H
#define DCBLOCK_H

#include "../core/dsp.h"

typedef struct {
    float x1, y1, r;
} dcblock_t;

void dcblock_init(dcblock_t *d, float sample_rate);
void dcblock_reset(dcblock_t *d);
/* in and out may be the same. */
void dcblock_process(dcblock_t *d, const float *in, float *out, int n);

#endif /* DCBLOCK_H */
