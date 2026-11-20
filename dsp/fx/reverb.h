/* reverb.h — plate reverb after J. Dattorro, "Effect Design Part 1: Reverberator and Other Filters"
 * (JAES 45(9), 1997): predelay, input bandwidth filter, 4 input diffusers, figure-of-eight tank with
 * two modulated allpasses and damping. Mono in, stereo out (wet only). Delays scaled from the paper's
 * 29761 Hz to the actual rate. Memory is provided by the caller (no allocation). */
#ifndef REVERB_H
#define REVERB_H

#include "../core/dsp.h"

#define REVERB_MAX_PREDELAY_S 0.25f
#define REVERB_MAX_SIZE 2.0f /* size scales the tank delays (1 = paper's plate) */

typedef struct {
    float *buf;
    int len, pos;
} reverb_line_t;

typedef struct {
    reverb_line_t pre, in_ap[4], tank_ap1[2], tank_d1[2], tank_ap2[2], tank_d2[2];
    float bw_state, damp_state[2], fb[2];
    float bandwidth, damping, decay;
    int freeze;
    float cur_decay, cur_damp, cur_in, glide; /* smoothed toward normal or frozen values */
    float lfo_phase, lfo_inc, excursion;
    int predelay;
    float sr;
} reverb_t;

/* Floats of memory needed for a sample rate and maximum size. */
size_t reverb_mem_floats(float sample_rate, float max_size);
/* mem must hold reverb_mem_floats(sample_rate, size) floats. size in 0.5..REVERB_MAX_SIZE. */
void reverb_init(reverb_t *r, float sample_rate, float size, float *mem);
void reverb_reset(reverb_t *r);
/* decay 0..0.99 (tail length), damping_hz: high-frequency loss in the tank (darker when lower),
 * predelay_s: 0..0.25 s. */
void reverb_set(reverb_t *r, float decay, float damping_hz, float predelay_s);
/* Freeze: the tank becomes (almost) lossless and the input is closed, so the current tail holds
 * indefinitely; turning it off lets the tail decay normally. Transitions are smoothed (~50 ms). */
void reverb_set_freeze(reverb_t *r, int on);
/* Mono input, wet stereo output (outL/outR are overwritten). */
void reverb_process(reverb_t *r, const float *in, float *outL, float *outR, int n);

#endif /* REVERB_H */
