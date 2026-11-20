/* tempo.h — tempo and beat position, from an internal BPM or from MIDI clock (24 ticks per quarter note,
 * start / continue / stop). The clock period is smoothed (one-pole over the tick intervals, outliers rejected)
 * so LFOs and delays do not jitter; the beat position locks to the tick count. Musical divisions shared by
 * every tempo-synced element (delay, LFOs, MSEGs). Reference: MIDI 1.0 specification, timing clock. */
#ifndef TEMPO_H
#define TEMPO_H

#include "../core/dsp.h"

#define TEMPO_WRAP_BEATS 1536.0f /* beat position wraps every 384 bars of 4/4 */

typedef struct {
    float sr;
    float bpm;          /* current tempo */
    float beat;         /* position in quarter notes, [0, TEMPO_WRAP_BEATS) */
    float frac;         /* internal clock: fraction of the current beat; the per-block increment is added here, */
    int whole;          /* not to beat (whole + frac), whose float grid is too coarse for it late in a song */
    int external;       /* 1 = follow MIDI clock */
    int running;        /* MIDI transport */
    uint32_t last_tick; /* sample time of the last clock tick */
    int have_tick;
    float period;       /* smoothed samples per tick (0 = unknown) */
    uint32_t ticks;     /* ticks since start */
    float tick_beat;    /* beat position of the last tick */
    float since_tick;   /* samples since the last tick (position extrapolated between ticks) */
} tempo_t;

void tempo_init(tempo_t *t, float sample_rate, float bpm);
/* Internal tempo (used when not following the clock, or before the clock has a period). */
void tempo_set_bpm(tempo_t *t, float bpm);
void tempo_set_external(tempo_t *t, int on);
/* MIDI real-time messages, with their time in samples. */
void tempo_clock(tempo_t *t, uint32_t time);
void tempo_start(tempo_t *t);    /* 0xFA: back to beat 0 */
void tempo_continue(tempo_t *t); /* 0xFB */
void tempo_stop(tempo_t *t);     /* 0xFC: the position holds; LFOs keep their phase */
/* Advances by n samples (call once per block). */
void tempo_advance(tempo_t *t, int n);

/* Musical divisions: labels and length in quarter notes. Index 0 = "off" (free running). */
#define TEMPO_DIV_COUNT 17
extern const char *const tempo_div_labels[TEMPO_DIV_COUNT + 1];
extern const float tempo_div_beats[TEMPO_DIV_COUNT];

/* Seconds of division div (> 0) at the current tempo. */
static inline float tempo_div_seconds(const tempo_t *t, int div)
{
    return tempo_div_beats[div] * 60.0f / t->bpm;
}

#endif /* TEMPO_H */
