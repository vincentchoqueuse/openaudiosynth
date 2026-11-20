/* drift.h — "analog flavor": slow random per-voice drift, plus a small fixed detune.
 * A random target is drawn every 0.2 to 0.8 s, then approached by a one-pole (tau 0.3 s).
 * Called once per block; output in [-1, 1], to be multiplied by an amount (cents, octaves...). */
#ifndef DRIFT_H
#define DRIFT_H

#include "../core/fastmath.h"

typedef struct {
    float y, target, coef, offset, ctrl_rate;
    int count;
    uint32_t rng;
} drift_t;

static inline void drift_init(drift_t *d, float ctrl_rate, uint32_t seed)
{
    d->rng = dsp_seed(seed);
    d->ctrl_rate = ctrl_rate;
    d->coef = 1.0f - fm_exp2(-FM_LOG2E / (0.3f * ctrl_rate));
    d->offset = 0.5f * dsp_rand_bipolar(&d->rng); /* fixed per-voice detune */
    d->y = d->target = 0.0f;
    d->count = 0;
}

static inline float drift_next(drift_t *d)
{
    if (--d->count <= 0) {
        d->target = dsp_rand_bipolar(&d->rng);
        d->count = (int)((0.2f + 0.6f * dsp_rand_unipolar(&d->rng)) * d->ctrl_rate) + 1;
    }
    d->y += (d->target - d->y) * d->coef;
    return dsp_clampf(0.5f * d->y + d->offset, -1.0f, 1.0f);
}

#endif /* DRIFT_H */
