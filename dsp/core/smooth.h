/* smooth.h — parameter smoothing with a one-pole low-pass filter, evaluated once per block
 * (control rate). Typical time constant: 5 ms. Consumers then interpolate linearly
 * sample by sample. */
#ifndef SMOOTH_H
#define SMOOTH_H

#include "fastmath.h"

typedef struct {
    float y;    /* current value */
    float coef; /* 1 - exp(-1/(tau*rate)) */
} smooth_t;

/* tau_s: time constant in seconds; rate_hz: rate at which smooth_next is called. */
static inline void smooth_init(smooth_t *s, float tau_s, float rate_hz, float initial)
{
    s->coef = 1.0f - fm_exp2(-FM_LOG2E / (tau_s * rate_hz));
    s->y = initial;
}

static inline float smooth_next(smooth_t *s, float target)
{
    s->y += (target - s->y) * s->coef;
    return s->y;
}

static inline void smooth_snap(smooth_t *s, float v) { s->y = v; }

#endif /* SMOOTH_H */
