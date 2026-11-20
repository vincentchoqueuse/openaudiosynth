/* mseg.h — multi-segment envelope (loopable, any number of breakpoints): up to MSEG_POINTS points, each reached from the previous
 * one in `time` (seconds, or beats when the caller advances it in beats) along a curve (-1..1: 0 = linear,
 * > 0 = fast start then slow, < 0 = slow start then fast). A sustain point holds while the gate is on; loop
 * points repeat a section either while the gate is on or forever. Retrigger and release start from the
 * current value (no jump). Evaluated at control rate. */
#ifndef MSEG_H
#define MSEG_H

#include "../core/dsp.h"

#define MSEG_POINTS 16

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

#endif /* MSEG_H */
