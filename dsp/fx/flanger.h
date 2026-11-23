/* flanger.h — stereo flanger: a short delay line (a fraction of a millisecond to 10 ms) swept by a sine LFO and
 * mixed half and half with the dry signal, which digs a comb of notches that glide (the jet sound); feedback
 * sharpens them into resonances. With negative feedback the delayed signal is inverted (as in the classic
 * negative flanging) so the resonances, at the odd multiples of half the delay's frequency, add up: hollower. The right channel's LFO runs a quarter of a cycle
 * ahead. Cubic (Hermite) interpolation keeps the swept delay bright. Reference: Zolzer (ed.), DAFX, "Flanger". */
#ifndef FLANGER_H
#define FLANGER_H

#include "../core/dsp.h"

#define FLANGER_BUF 1024 /* power of 2: 10 ms and the interpolation's taps up to 96 kHz (shorter above) */

typedef struct {
    float sr;
    float buf[2][FLANGER_BUF];
    int pos;
    float phase, inc;
    float d_prev[2];          /* delay (samples) at the end of the previous block, per channel */
    float fb[2];              /* the last delayed sample, per channel */
    float delay, depth, feedback, comp, mix, mix_prev;
    float pol, pol_prev;      /* the delayed signal's polarity: -1 with negative feedback (ramped over a block) */
} flanger_t;

void flanger_init(flanger_t *f, float sample_rate);
void flanger_reset(flanger_t *f);
/* mix 0..1 (0: off, 1: half dry, half delayed = the deepest notches); rate_hz LFO; delay_ms the longest delay
 * (0.5..10); depth 0..1 (how far down the sweep goes, to a twentieth of it); feedback -0.9..0.9 */
void flanger_set(flanger_t *f, float mix, float rate_hz, float delay_ms, float depth, float feedback);
/* in place allowed; passes the signal through untouched while the mix is and was 0 */
void flanger_process(flanger_t *f, float *L, float *R, int n);

#endif /* FLANGER_H */
