/* polyblep.h — anti-aliased oscillator: saw and pulse (PWM) via 2-point polyBLEP,
 * triangle via 2-point polyBLAMP, sine via table.
 * Reference: Valimaki, Pekonen, Nam, "Perceptually informed synthesis of bandlimited classical
 * waveforms using integrated polynomial interpolation" (JASA 2012); Esqueda et al. (polyBLAMP). */
#ifndef POLYBLEP_H
#define POLYBLEP_H

#include "../core/dsp.h"

typedef enum { OSC_SINE = 0, OSC_SAW, OSC_PULSE, OSC_TRI, OSC_WAVE_COUNT } osc_wave_t;

typedef struct {
    float phase;     /* [0, 1) */
    float dt;        /* current phase increment (f/SR) */
    float dt_target; /* increment reached at the end of the next block */
    float pw, pw_target;
    float inv_sr, max_dt;
    int wave;
    int snap; /* 1: the next block starts directly at the target (note start) */
    float yd; /* osc_process_pair: the driven oscillator's output one sample late (its steps corrected on both sides) */
} osc_t;

void osc_init(osc_t *o, float sample_rate);
/* Restarts from a given phase; the pitch of the next block does not glide from the old one. */
void osc_reset(osc_t *o, float phase);
void osc_set_wave(osc_t *o, osc_wave_t w);
/* Target frequency (Hz) and pulse width (0..1): interpolated over the next block. */
void osc_set(osc_t *o, float freq_hz, float pw);
void osc_process(osc_t *o, float *restrict out, int n);
/* Two oscillators sample by sample, a driven by b: a restarts at every wrap of b when sync is set (hard sync), its
 * frequency follows b's output at audio rate (2^(fm_oct * b)) and its pulse width too (+ pwm * b). b runs as usual.
 * a's steps (its wrap, its pulse edge, the sync reset) are corrected by a two-point polyBLEP on both sides of the
 * step, so a comes out one sample late; saw and pulse are corrected, triangle and sine are not at a sync reset.
 * Writes n samples of each into out_a and out_b. */
void osc_process_pair(osc_t *a, osc_t *b, float *restrict out_a, float *restrict out_b, int sync, float fm_oct, float pwm, int n);

/* Residuals, exposed for engines that build their own oscillators (t = phase in [0,1), dt = phase
 * increment, idt ~ 1/dt). A step of height h at the wrap: add h * osc_blep(...); a slope change of
 * s per cycle: add s * dt * osc_blamp(...). */
/* polyBLEP residual of a rising unit step. idt ~= 1/dt (interpolated, hence the clamps). */
static inline float osc_blep(float t, float dt, float idt)
{
    if (t < dt) {
        const float y = 1.0f - dsp_minf(t * idt, 1.0f);
        return -0.5f * y * y;
    }
    if (t > 1.0f - dt) {
        const float y = dsp_maxf((t - 1.0f) * idt, -1.0f) + 1.0f;
        return 0.5f * y * y;
    }
    return 0.0f;
}

/* polyBLAMP residual of a unit slope break (per sample). */
static inline float osc_blamp(float t, float dt, float idt)
{
    if (t < dt) {
        const float y = 1.0f - dsp_minf(t * idt, 1.0f);
        return y * y * y * (1.0f / 6.0f);
    }
    if (t > 1.0f - dt) {
        const float y = dsp_maxf((t - 1.0f) * idt, -1.0f) + 1.0f;
        return y * y * y * (1.0f / 6.0f);
    }
    return 0.0f;
}


#endif /* POLYBLEP_H */
