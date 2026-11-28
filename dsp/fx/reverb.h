/* reverb.h — reverb of four types. Plate, room and hall: J. Dattorro's figure-of-eight tank ("Effect Design Part 1:
 * Reverberator and Other Filters", JAES 45(9), 1997: predelay, input bandwidth filter, 4 input diffusers, two
 * modulated allpasses and damping), its size, diffusion and early reflections set by the type. Spring: two
 * feedback loops through a cascade of first-order allpasses, whose dispersion makes each pass chirp (after Parker
 * and Valimaki's spectral-delay-filter models of spring reverb). Mono in, stereo out (wet only); low cut on the input,
 * width on the output. Delays scaled from the paper's 29761 Hz to the actual rate. Memory is provided by the caller
 * (no allocation); the size moves inside the memory given at init, gliding. */
#ifndef REVERB_H
#define REVERB_H

#include "../core/dsp.h"

#define REVERB_MAX_PREDELAY_S 0.25f
#define REVERB_MAX_SIZE 2.0f /* size scales the tank delays (1 = paper's plate) */
#define REVERB_SPRING_STAGES 48 /* first-order allpasses in each spring's loop: its chirp */
#define REVERB_SPRING_MAX_S 0.09f /* the longest spring loop (size x2) */
#define REVERB_ER 6 /* early reflections per side (room, hall) */

typedef enum { REVERB_PLATE = 0, REVERB_ROOM, REVERB_HALL, REVERB_SPRING, REVERB_TYPES } reverb_type_t;

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
    /* the type: tank scale (allocated, present gliding to wanted), the lengths it gives, diffusion, reflections */
    int type;
    float max_ks, plate_ks, ks, ks_target, size_mult;
    int len_ap1[2], len_d1[2], len_ap2[2], len_d2[2], er_d[2][REVERB_ER];
    float in_g[4], dd1, dd2, er_gain;
    /* input low cut (one pole, off at its lowest) and output width (1 = as the tank gives it) */
    float lc_coef, lc_state, width;
    int lowcut;
    /* spring: two loops, each a line and a cascade of allpasses, damped */
    reverb_line_t spr[2];
    int spr_len[2];
    float spr_z[2][REVERB_SPRING_STAGES], spr_lp[2], spr_fb[2];
} reverb_t;

/* Floats of memory needed for a sample rate and maximum size. */
size_t reverb_mem_floats(float sample_rate, float max_size);
/* mem must hold reverb_mem_floats(sample_rate, size) floats. size in 0.5..REVERB_MAX_SIZE: the plate's, and the
 * largest any type reaches. */
void reverb_init(reverb_t *r, float sample_rate, float size, float *mem);
/* The same with more memory than the plate needs: mem holds reverb_mem_floats(sample_rate, max_size) floats, the
 * plate is `size` (at most max_size) and a hall can grow up to max_size. */
void reverb_init_max(reverb_t *r, float sample_rate, float size, float max_size, float *mem);
void reverb_reset(reverb_t *r);
/* decay 0..0.99 (tail length), damping_hz: high-frequency loss in the tank (darker when lower),
 * predelay_s: 0..0.25 s. */
void reverb_set(reverb_t *r, float decay, float damping_hz, float predelay_s);
/* Freeze: the tank becomes (almost) lossless and the input is closed, so the current tail holds
 * indefinitely; turning it off lets the tail decay normally. Transitions are smoothed (~50 ms). */
void reverb_set_freeze(reverb_t *r, int on);
/* type (REVERB_PLATE..), size 0..1 (0.5 = the type's own; x0.5 .. x2 around it, within the memory given) */
void reverb_set_type(reverb_t *r, int type, float size);
/* lowcut_hz: 20 (off) .. 1000 Hz, on the input; width 0 (mono) .. 1 (the reverb's own stereo) */
void reverb_set_tone(reverb_t *r, float lowcut_hz, float width);
/* Mono input, wet stereo output (outL/outR are overwritten). */
void reverb_process(reverb_t *r, const float *in, float *outL, float *outR, int n);

#endif /* REVERB_H */
