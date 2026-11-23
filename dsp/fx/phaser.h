/* phaser.h — stereo phaser: a chain of first-order all-pass filters (TPT, Zavalishin) whose break frequency an LFO
 * sweeps exponentially around a centre, with feedback from the end of the chain to its input; mixed half and half
 * with the dry signal, the phase shifts dig moving notches (two per four stages). The right channel's LFO runs a
 * quarter of a cycle ahead. The sweep is computed per block and interpolated per sample.
 * Reference: Zolzer (ed.), DAFX, "Phaser"; V. Zavalishin, The Art of VA Filter Design (one-pole all-pass). */
#ifndef PHASER_H
#define PHASER_H

#include "../core/dsp.h"

#define PHASER_MAX_STAGES 12

typedef struct {
    float sr, inv_sr;
    float s[2][PHASER_MAX_STAGES]; /* all-pass states, per channel */
    float fb[2];                   /* the chain's last output, per channel */
    float phase, inc;              /* LFO (cycles) */
    float G_prev[2];               /* all-pass coefficient at the end of the previous block, per channel */
    int stages;
    float centre, octaves, feedback, comp, mix, mix_prev;
} phaser_t;

void phaser_init(phaser_t *p, float sample_rate);
void phaser_reset(phaser_t *p);
/* mix 0..1 (0: off, 1: half dry, half phased = the deepest notches); rate_hz LFO; centre_hz of the sweep;
 * depth 0..1 (0 to 4 octaves of sweep); feedback -0.9..0.9; stages 2..PHASER_MAX_STAGES (even) */
void phaser_set(phaser_t *p, float mix, float rate_hz, float centre_hz, float depth, float feedback, int stages);
/* in place allowed; passes the signal through untouched while the mix is and was 0 */
void phaser_process(phaser_t *p, float *L, float *R, int n);

#endif /* PHASER_H */
