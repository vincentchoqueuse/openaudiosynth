/* lfo.c — control-rate LFO (see lfo.h). */
#include "lfo.h"
#include "../core/fastmath.h"

void lfo_init(lfo_t *l, float sample_rate, uint32_t seed)
{
    l->inv_sr = 1.0f / sample_rate;
    l->rng = dsp_seed(seed);
    l->sh = 0.0f;
    l->inc = 0.0f;
    l->wave = LFO_SINE;
    lfo_reset(l, 0.0f);
}

void lfo_reset(lfo_t *l, float phase) { l->phase = fm_frac(phase); }

void lfo_set(lfo_t *l, float rate_hz, lfo_wave_t wave)
{
    l->inc = dsp_clampf(rate_hz, 0.0f, 1000.0f) * l->inv_sr;
    l->wave = ((unsigned)wave < (unsigned)LFO_WAVE_COUNT) ? (int)wave : LFO_SINE;
}

float lfo_advance(lfo_t *l, int n)
{
    l->phase += l->inc * (float)n;
    if (l->phase >= 1.0f) {
        l->phase = fm_frac(l->phase);
        l->sh = dsp_rand_bipolar(&l->rng); /* new value every cycle */
    }
    const float p = l->phase;
    switch (l->wave) {
    case LFO_TRI:
        return 1.0f - 4.0f * dsp_absf(p - 0.5f);
    case LFO_SAW:
        return 2.0f * p - 1.0f;
    case LFO_SQUARE:
        return p < 0.5f ? 1.0f : -1.0f;
    case LFO_SH:
        return l->sh;
    case LFO_SINE:
    default:
        return fm_sin(p);
    }
}
