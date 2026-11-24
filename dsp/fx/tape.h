/* tape.h — a worn tape machine on the whole signal: wow (slow pitch wander) and flutter (fast) from a short
 * modulated delay read with linear interpolation, a soft saturation (tanh, level kept at low drive), the loss of
 * highs (one-pole low-pass) and hiss (white noise). Stereo, the two channels through the same transport (one wow,
 * one flutter). Switched on and off through a short cross-fade with the dry signal; it starts from an empty
 * transport each time it is switched on. Its delay memory is the caller's.
 * References: the wow / flutter of a transport as a delay modulation (DAFX, Zolzer ed., "Vibrato"); tape
 * saturation as a tanh shaper (V. Valimaki et al., virtual analog effects). */
#ifndef TAPE_H
#define TAPE_H

#include "../core/dsp.h"

#define TAPE_LEN 1024 /* a power of two: samples of the transport's delay, per channel (enough for the deepest wow up to 96 kHz) */

typedef struct {
    float sr, inv_sr;
    float *buf[2]; /* the caller's memory: TAPE_LEN floats per channel */
    int w;
    float wow_ph, flutter_ph, drift;    /* phases (cycles), slow random offset of the wow's rate */
    float wow, flutter, gain, norm, hiss, lp_k; /* settings as the loop uses them */
    float lp[2];
    float d_prev;                       /* delay at the end of the last block (samples) */
    float mix, mix_prev;                /* 1 on, 0 off; ramped per block */
    uint32_t rng;
} tape_t;

/* mem must hold TAPE_MEM_FLOATS floats (no allocation). */
#define TAPE_MEM_FLOATS (2 * TAPE_LEN)
void tape_init(tape_t *t, float sample_rate, float *mem);
void tape_reset(tape_t *t);
/* on: 0 / 1; wow, flutter, drive, hiss: 0..1; tone_hz: the low-pass (the loss of highs). */
void tape_set(tape_t *t, int on, float wow, float flutter, float drive, float hiss, float tone_hz);
/* in place; passes the signal through untouched while it is and was off */
void tape_process(tape_t *t, float *L, float *R, int n);

#endif /* TAPE_H */
