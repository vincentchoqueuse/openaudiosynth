/* dist.h — a distortion for a synth voice: a drive into one of four shapers, 2x oversampled (half-band, as the
 * ladder's), a one-pole tone low-pass after it and a dry/wet mix. The output is scaled back by the shaper's gain
 * at a typical level, so the drive changes the colour more than the loudness. Drive and mix ramp over each block;
 * the dry signal is delayed like the oversampled one, so a partial mix does not comb-filter.
 * Shapers: soft (tanh), tube (biased tanh: even harmonics too), hard (cubic clip: flat tops, brighter), fold (sine
 * wavefolder: the louder the input, the more the waveform folds back; metallic, vocal growls when moved).
 * References: waveshaping and wavefolding as in J. Pakarinen & D. Yeh, "A Review of Digital Techniques for
 * Modeling Vacuum-Tube Guitar Amplifiers" (CMJ, 2009); F. Esqueda et al., "Virtual Analog Buchla 259 Wavefolder" (DAFx 2017). */
#ifndef DIST_H
#define DIST_H

#include "../core/dsp.h"
#include "../filter/halfband.h"

#define DIST_DRY_N 16
#define DIST_TUBE_BIAS 0.3f /* the tube's asymmetry: an offset into the tanh (0 would be symmetric) */
#define DIST_LATENCY 11.5f /* the half-band pair's delay at the base rate, in samples */

typedef enum { DIST_OFF = 0, DIST_SOFT, DIST_TUBE, DIST_HARD, DIST_FOLD, DIST_MODE_COUNT } dist_mode_t;

typedef struct {
    hb2x_t os;
    float dry[DIST_DRY_N]; /* the dry signal, delayed like the oversampled path (DIST_LATENCY samples) */
    int dpos;
    float sr, lp;              /* tone low-pass state */
    float g, comp, mix, a;     /* targets: drive gain, level compensation, wet amount, tone coefficient */
    float bias, b0;            /* the tube's bias and tanh(bias) */
    float g_prev, comp_prev, mix_prev;
    int mode;
} dist_t;

void dist_init(dist_t *d, float sample_rate);
void dist_reset(dist_t *d);
/* drive_db: gain into the shaper (0..36 dB); tone_hz: low-pass after it; mix: 0 dry .. 1 wet. */
void dist_set(dist_t *d, dist_mode_t mode, float drive_db, float tone_hz, float mix);
/* in place allowed; does nothing (out = in) when the mode is off */
void dist_process(dist_t *d, const float *in, float *out, int n);

#endif /* DIST_H */
