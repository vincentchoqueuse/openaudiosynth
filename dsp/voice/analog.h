/* analog.h — the imperfections of an analog oscillator, one state per oscillator: a static offset drawn once (the
 * tolerance of its parts), a slow Ornstein-Uhlenbeck drift (standard deviation and time constant) and a light
 * jitter (white frequency noise at block rate, so the phase wanders as a Wiener process: irregular beats).
 * Output in cents, once per block. OU update: x += -a x + sqrt(2 a) w, w of unit variance, a = dt / tau. */
#ifndef ANALOG_H
#define ANALOG_H

#include "../core/dsp.h"
#include "../core/fastmath.h"

/* the block's coefficients, shared by every oscillator of an engine */
typedef struct {
    float tolerance; /* cents at a unit offset */
    float sd;        /* drift standard deviation (cents) */
    float a, kick;   /* OU pull and noise scale per block (unit variance walk) */
    float jitter;    /* cents of white noise per block, at unit draw */
} analog_cfg_t;

typedef struct {
    float offset; /* static, -1..1 (scaled by the tolerance) */
    float x;      /* the walk, unit variance once settled */
    uint32_t rng;
} analog_t;

/* dt = block duration (s); tolerance and sd in cents, tau in seconds (> 0), jitter in cents */
static inline void analog_cfg(analog_cfg_t *c, float tolerance, float sd, float tau, float jitter, float dt)
{
    c->tolerance = tolerance;
    c->sd = sd;
    c->a = dsp_minf(dt / dsp_maxf(tau, 1e-3f), 1.0f);
    c->kick = 1.7320508f * fm_exp2(0.5f * fm_log2(2.0f * c->a)); /* sqrt(3) (uniform -> unit variance) sqrt(2 a) */
    c->jitter = 1.7320508f * jitter;
}

/* a new oscillator: its offset and a start already in the walk's spread (no settling from zero) */
static inline void analog_init(analog_t *s, uint32_t seed)
{
    s->rng = dsp_seed(seed);
    s->offset = dsp_rand_bipolar(&s->rng);
    s->x = 1.7320508f * dsp_rand_bipolar(&s->rng);
}

/* cents to add to the oscillator's pitch this block */
static inline float analog_next(analog_t *s, const analog_cfg_t *c)
{
    s->x += c->kick * dsp_rand_bipolar(&s->rng) - c->a * s->x;
    const float j = c->jitter > 0.0f ? c->jitter * dsp_rand_bipolar(&s->rng) : 0.0f;
    return c->tolerance * s->offset + c->sd * s->x + j;
}

#endif /* ANALOG_H */
