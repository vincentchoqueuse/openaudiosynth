/* chorus.h — analog-style chorus/ensemble on short modulated delay lines (mono in, stereo out).
 * Modes: I and II = two lines driven by one triangle LFO in antiphase (L/R), slow/deeper vs faster,
 * in the manner of classic BBD chorus units; I+II = both LFOs summed; ENSEMBLE = three lines whose
 * delays follow a slow and a fast sine 120 degrees apart (string-machine ensemble).
 * The wet path is low-passed (~9 kHz, BBD bandwidth). Reference: Zolzer (ed.), DAFX, "Chorus". */
#ifndef CHORUS_H
#define CHORUS_H

#include "../core/dsp.h"

#define CHORUS_BUF 2048 /* power of 2, >= 20 ms at 96 kHz */

typedef enum { CHORUS_OFF = 0, CHORUS_I, CHORUS_II, CHORUS_I_II, CHORUS_ENSEMBLE, CHORUS_MODE_COUNT } chorus_mode_t;

typedef struct {
    float buf[CHORUS_BUF];
    int pos;
    float sr;
    float ph_slow, ph_fast, inc_slow, inc_fast;
    float lp_coef, lp[3];
    float mix, mix_prev;
    int mode;
} chorus_t;

void chorus_init(chorus_t *c, float sample_rate);
void chorus_reset(chorus_t *c);
/* mix: wet amount 0..1 added to the dry signal (dry stays at unity). */
void chorus_set(chorus_t *c, chorus_mode_t mode, float mix);
/* outL/outR may alias inL/inR. */
void chorus_process(chorus_t *c, const float *inL, const float *inR, float *outL, float *outR, int n);

#endif /* CHORUS_H */
