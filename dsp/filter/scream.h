/* scream.h — 2-pole low-pass in the spirit of late-70s semi-modular monosynth filters, with a screaming resonance: two
 * one-pole TPT low-passes with a high-pass in the feedback path, resonance K up to self-oscillation, and a
 * saturating feedback node that gives the aggressive, screaming character; optional 2x oversampling around
 * the nonlinearity (halfband.h). Zero-delay feedback solved per sample.
 * References: V. Zavalishin, "The Art of VA Filter Design" (TPT one-poles, ZDF feedback); W. Pirkle,
 * "Designing Software Synthesizer Plugins in C++" (Korg 35 filter structure). */
#ifndef SCREAM_H
#define SCREAM_H

#include "halfband.h"

typedef struct {
    float z1, z2, z3;          /* states: low-pass 1, low-pass 2, high-pass in the feedback */
    float G, k, tG, tk;        /* G = g / (1 + g), resonance K: current and block target */
    float drive, sr;
    int oversample, snap; /* oversample: the factor, 1, 2 or 4 */
    hb4x_t hb;
} scream_t;

void scream_init(scream_t *f, float sample_rate);
void scream_reset(scream_t *f);
/* Runs the filter at 1, 2 or 4 times the sample rate (see ladder_set_oversampling). */
void scream_set_oversampling(scream_t *f, int factor);
/* cutoff in Hz, resonance 0..1 (self-oscillation from about 0.91, bounded by the saturating node), drive 0..1 (saturation of the feedback node). */
void scream_set(scream_t *f, float cutoff_hz, float resonance, float drive);
/* in and out may be the same buffer. */
void scream_process(scream_t *f, const float *in, float *out, int n);

#endif /* SCREAM_H */
