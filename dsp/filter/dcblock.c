/* dcblock.c — DC blocker (see dcblock.h). */
#include "dcblock.h"
#include "../core/fastmath.h"

void dcblock_init(dcblock_t *d, float sample_rate)
{
    d->r = 1.0f - FM_TWO_PI * 10.0f / sample_rate;
    dcblock_reset(d);
}

void dcblock_reset(dcblock_t *d) { d->x1 = d->y1 = 0.0f; }

DSP_FAST_CODE void dcblock_process(dcblock_t *d, const float *in, float *out, int n)
{
    float x1 = d->x1, y1 = d->y1;
    const float r = d->r;
    for (int i = 0; i < n; i++) {
        const float x = in[i];
        y1 = x - x1 + r * y1;
        x1 = x;
        out[i] = y1;
    }
    d->x1 = x1;
    d->y1 = y1;
}
