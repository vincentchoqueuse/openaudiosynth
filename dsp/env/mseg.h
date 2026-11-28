/* mseg.h — multi-segment envelope (loopable, any number of breakpoints): up to MSEG_POINTS points, each reached from the previous
 * one in `time` (seconds, or beats when the caller advances it in beats) along a curve (-1..1: 0 = linear,
 * > 0 = fast start then slow, < 0 = slow start then fast). A sustain point holds while the gate is on; loop
 * points repeat a section either while the gate is on or forever. Retrigger and release start from the
 * current value (no jump). Evaluated at control rate. */
#ifndef MSEG_H
#define MSEG_H

#include "../core/dsp.h"
#include "../core/fastmath.h"

#define MSEG_POINTS 16

/* the shape of a segment, 0..1 -> 0..1: y = (2^(a x) - 1) / (2^a - 1), a = -8 curve (0 = a line, > 0 = fast start
 * then slow, an exponential; < 0 = slow start then fast, a logarithmic) */
static inline float mseg_curve(float x, float curve)
{
    const float a = -8.0f * dsp_clampf(curve, -1.0f, 1.0f);
    if (dsp_absf(a) < 1e-3f)
        return x;
    return (fm_exp2(a * x) - 1.0f) / (fm_exp2(a) - 1.0f);
}

typedef enum { MSEG_ONESHOT = 0, MSEG_LOOP_HELD, MSEG_LOOP_FREE, MSEG_MODE_COUNT } mseg_mode_t;

typedef struct {
    float time[MSEG_POINTS];  /* time to reach point k from point k-1 (time[0] unused) */
    float level[MSEG_POINTS]; /* 0..1 */
    float curve[MSEG_POINTS]; /* shape of the segment ending at point k */
    int count;                /* 2..MSEG_POINTS */
    int sustain;              /* point held while the gate is on, -1 = none */
    int loop_start, loop_end; /* section repeated (loop_start < loop_end), used by the loop modes */
    int mode;                 /* mseg_mode_t */
} mseg_shape_t;

typedef struct {
    int seg;           /* segment in progress: from point seg-1 to point seg; count = finished */
    float pos;         /* time spent in it */
    float from;        /* level the segment starts from */
    float value;       /* current output */
    int gate;
} mseg_t;

void mseg_reset(mseg_t *m, const mseg_shape_t *s);         /* idle at the first point's level */
void mseg_gate(mseg_t *m, const mseg_shape_t *s, int on);   /* note on: restart; note off: leave sustain/loop */
/* Advances by dt (seconds or beats) and returns the value. Bounded work whatever the times. */
float mseg_advance(mseg_t *m, const mseg_shape_t *s, float dt);

/* Where it is along the shape's time axis, 0..1 of the whole shape (for a display), or -1 when idle or finished.
 * Not for the audio path's decisions: a loop jumps back. */
static inline float mseg_progress(const mseg_t *m, const mseg_shape_t *s)
{
    if (m->seg < 1 || m->seg >= s->count)
        return -1.0f;
    float total = 0.0f, done = 0.0f;
    for (int k = 1; k < s->count; k++) {
        total += s->time[k];
        if (k < m->seg)
            done += s->time[k];
    }
    if (total <= 0.0f)
        return -1.0f;
    const float x = (done + (m->pos < s->time[m->seg] ? m->pos : s->time[m->seg])) / total;
    return x < 1.0f ? x : 1.0f;
}

#endif /* MSEG_H */
