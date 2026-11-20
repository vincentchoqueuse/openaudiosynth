/* ladder.h — 4-pole transistor-ladder low-pass in zero-delay-feedback (TPT) form, with a saturating
 * input stage, resonance up to self-oscillation, 24/18/12 dB outputs and optional 2x oversampling
 * (47-tap polyphase half-band, -56 dB stopband) for the nonlinearity.
 * References: V. Zavalishin, "The Art of VA Filter Design", ch. 5 (ladder, zero-delay feedback);
 * A. Huovilainen, "Non-linear digital implementation of the Moog ladder filter" (DAFx 2004). */
#ifndef LADDER_H
#define LADDER_H

#include "halfband.h"

typedef enum { LADDER_LP24 = 0, LADDER_LP18, LADDER_LP12, LADDER_MODE_COUNT } ladder_mode_t;


typedef struct {
    float s[4];                     /* integrator states */
    float G, k, d, tG, tk, td;      /* G = g/(1+g), k = feedback, d = 1/(1+k G^4): current / target */
    float drive, inv_drive, comp;
    float comp_amount;              /* passband compensation of the resonance: 0.35 by default, 0 = none */
    float sr;
    int mode, oversample, snap;     /* oversample: the factor, 1, 2 or 4 */
    hb4x_t hb;                      /* oversampling around the nonlinearity (2x uses its outer stage) */
} ladder_t;

void ladder_init(ladder_t *l, float sample_rate);
void ladder_reset(ladder_t *l);
void ladder_set_mode(ladder_t *l, ladder_mode_t mode);
/* Runs the filter at 1, 2 or 4 times the sample rate: higher = less aliasing from the drive and the saturated
 * resonance (cleaner self-oscillation high up), at about 2.5x then 5x the cost of 1x. Changing it restarts
 * the oversampling filters (a short gap: a setting, not something to modulate). */
void ladder_set_oversampling(ladder_t *l, int factor);
/* cutoff in Hz; resonance 0..1 (self-oscillation from ~0.95); drive 0..1 (input saturation). */
void ladder_set(ladder_t *l, float cutoff_hz, float resonance, float drive);
/* How much of the passband loss caused by the resonance is made up (default 0.35; call before ladder_set).
 * 0 = none, as in the transistor ladder itself: turning the resonance up thins the bass. */
void ladder_set_compensation(ladder_t *l, float amount);
/* in and out may be the same buffer. Coefficients are interpolated over the call (one block). */
void ladder_process(ladder_t *l, const float *in, float *out, int n);
/* The same with the output fed back into the input, one sample late, through the caller's post gain (the
 * amplifier that follows the filter): in[i] + fb * hp(out[i-1] * post[i-1]). hp is a one-pole high-pass
 * (hp_coef = 1 - exp(-2 pi f / sr)), the AC coupling that keeps the loop from latching on DC. state[2] is the
 * caller's (zero it on reset). The filter's own saturation bounds the loop. */
void ladder_process_fb(ladder_t *l, const float *in, const float *post, float fb, float hp_coef, float *state,
                       float *out, int n);

#endif /* LADDER_H */
