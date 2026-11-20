/* delay.h — stereo "tape" echo: fractional delay lines read with 4-point Hermite interpolation,
 * delay-time changes glide like a tape speed change, wow (slow) and flutter (fast) modulation,
 * and a feedback path with low-pass/high-pass "tone" and soft saturation, so repeats get darker
 * and grainier. Stereo or ping-pong. Wet-only output. Memory supplied by the caller.
 * References: U. Zolzer (ed.), DAFX, ch. "Delays" and "Modulation"; Hermite interpolation after
 * Laakso et al., "Splitting the unit delay" (IEEE SPM 1996). */
#ifndef DELAY_H
#define DELAY_H

#include "../core/dsp.h"

typedef struct {
    float *buf[2];
    int len, pos;
    float sr, inv_sr;
    /* delay (samples) per channel: target and glide state */
    float d_target[2], d_cur[2], glide;
    float feedback, drive, inv_drive;
    float lp_coef, hp_coef, lp[2], hp[2];
    float wow_depth, flutter_depth; /* in samples */
    float wow_phase, flutter_phase, wow_inc, flutter_inc;
    float drift, drift_target;
    uint32_t rng;
    int pingpong;
} delay_t;

/* Floats of memory needed for a maximum delay time (both channels). */
size_t delay_mem_floats(float sample_rate, float max_time_s);
void delay_init(delay_t *d, float sample_rate, float max_time_s, float *mem);
void delay_reset(delay_t *d);
/* time_s: left delay; right = time_s * ratio_r (stereo mode) or time_s (ping-pong).
 * feedback 0..0.98, tone_hz: feedback low-pass cutoff (repeats darker below), drive 0..1 saturation,
 * wow 0..1 (tape wobble amount), pingpong 0/1. */
void delay_set(delay_t *d, float time_s, float ratio_r, float feedback, float tone_hz, float drive, float wow,
               int pingpong);
/* Jump to the target delay time without the tape glide (use when the effect starts from idle). */
void delay_snap(delay_t *d);
/* Mono-compatible stereo input, wet stereo output (outL/outR overwritten; may alias inL/inR). */
void delay_process(delay_t *d, const float *inL, const float *inR, float *outL, float *outR, int n);

#endif /* DELAY_H */
