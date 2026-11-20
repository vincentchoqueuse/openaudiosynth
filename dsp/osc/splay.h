/* splay.h — plays a 16-bit sample at any speed, forwards or backwards, between a start and an end point,
 * once or in a loop. Band-limited interpolation: a Kaiser-windowed sinc whose cutoff follows the speed (at
 * speed r > 1 it drops to 1/r of the source band), so a sample played faster does not alias (J. O. Smith,
 * "Digital Audio Resampling", bandlimited interpolation). A one-shot fades out over its last SPLAY_FADE
 * frames, so an end point inside the sound does not click. */
#ifndef SPLAY_H
#define SPLAY_H

#include <stdint.h>

#define SPLAY_ZC 6          /* zero crossings of the kernel on each side (tools/gen_splay_table.py) */
#define SPLAY_RES 64        /* kernel points per zero crossing */
#define SPLAY_BW 0.92f      /* passband edge, fraction of the source's Nyquist frequency */
#define SPLAY_RATE_MAX 4.0f /* fastest speed (+24 semitones): bounds the work per sample */
#define SPLAY_FADE 64       /* source frames of fade-out before the end of a one-shot */

extern const float splay_kernel[SPLAY_ZC * SPLAY_RES + 2];

typedef struct {
    const int16_t *data;
    int32_t frames;     /* length of data */
    int32_t start, end; /* region played, frames [start, end) */
    int32_t i;          /* position: i + frac (frames) */
    float frac;
    int8_t dir;         /* +1 forwards, -1 backwards */
    int8_t loop;        /* 1: wraps around the region */
    int8_t active;
} splay_t;

/* Silent until splay_start. */
void splay_init(splay_t *p);
/* Starts playing data (frames samples) from start to end (fractions 0..1 of the sample; end is pushed past
 * start), backwards from end to start when reverse, around [start, end) when loop. */
void splay_start(splay_t *p, const int16_t *data, int32_t frames, float start, float end, int reverse, int loop);
/* Loop on or off while playing (a released note leaves its loop and plays to the end). */
static inline void splay_set_loop(splay_t *p, int loop) { p->loop = (int8_t)(loop != 0); }
/* Writes n samples (full scale 1.0) at a speed (1 = original pitch) ramping from rate0 to rate1 across the
 * block, clamped to [0, SPLAY_RATE_MAX]; zeros once finished. Returns 1 while playing. */
int splay_process(splay_t *p, float *out, int n, float rate0, float rate1);

#endif /* SPLAY_H */
