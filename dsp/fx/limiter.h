/* limiter.h — stereo look-ahead peak limiter for the master: an input gain, then a gain that never lets a sample
 * past the ceiling. The gain each sample needs (ceiling / stereo peak) is held at its minimum over the look-ahead
 * window, released exponentially, then averaged over the same window (a box filter): the averaged gain reaches the
 * needed one exactly when the loud sample, delayed by the window, comes out, so the attack is smooth and the peak
 * is never exceeded. Latency: LIMITER_LOOKAHEAD_S. Reference: the classic hold + moving-average look-ahead design
 * (e.g. D. Giannoulis, M. Massberg, J. Reiss, "Digital Dynamic Range Compressor Design", JAES 2012). */
#ifndef LIMITER_H
#define LIMITER_H

#include "../core/dsp.h"

#define LIMITER_LOOKAHEAD_S 0.0015f
#define LIMITER_MAX 256 /* window length at most (1.5 ms up to 170 kHz) */

typedef struct {
    float sr;
    int len;                                   /* window length in samples */
    float dl[LIMITER_MAX], dr[LIMITER_MAX];    /* audio delayed by len - 1 */
    int dpos;
    float mv[LIMITER_MAX];                     /* sliding minimum: a monotonic queue of values ... */
    int mi[LIMITER_MAX];                       /* ... and their sample index */
    int qh, qn, t;                             /* queue head, size, running sample index */
    float box[LIMITER_MAX];                    /* the released gain over the window, and its sum */
    int bpos;
    float env;
    float ceiling, gain, rel;                  /* targets: ceiling and input gain (linear), release coefficient */
    float gain_prev, ceil_prev;
    float reduction;                           /* the smallest gain of the last block (meter, 1 = none) */
} limiter_t;

void limiter_init(limiter_t *l, float sample_rate);
void limiter_reset(limiter_t *l);
/* gain_db: into the limiter (0..+24); ceiling_db: the highest peak out (<= 0); release_s: back to unity gain */
void limiter_set(limiter_t *l, float gain_db, float ceiling_db, float release_s);
/* in place allowed; n <= SYNTH_BLOCK */
void limiter_process(limiter_t *l, float *L, float *R, int n);

#endif /* LIMITER_H */
