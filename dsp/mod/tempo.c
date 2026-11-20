/* tempo.c — internal tempo and MIDI clock follower (see tempo.h). */
#include "tempo.h"

const char *const tempo_div_labels[TEMPO_DIV_COUNT + 1] = {
    "off", "1/32", "1/16t", "1/16", "1/16d", "1/8t", "1/8", "1/8d", "1/4t", "1/4", "1/4d", "1/2", "1/2d",
    "1", "2", "4", "8", NULL};
const float tempo_div_beats[TEMPO_DIV_COUNT] = {
    0.0f, 0.125f, 1.0f / 6.0f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 3.0f,
    4.0f, 8.0f, 16.0f, 32.0f};

/* the internal clock continues from wherever beat was set (init, start, MIDI clock) */
static void tempo_sync_internal(tempo_t *t)
{
    t->whole = (int)t->beat;
    t->frac = t->beat - (float)t->whole;
}

void tempo_init(tempo_t *t, float sample_rate, float bpm)
{
    t->sr = sample_rate;
    t->bpm = dsp_clampf(bpm, 20.0f, 300.0f);
    t->beat = 0.0f;
    tempo_sync_internal(t);
    t->external = 0;
    t->running = 1;
    t->last_tick = 0;
    t->have_tick = 0;
    t->period = 0.0f;
    t->ticks = 0;
    t->tick_beat = 0.0f;
    t->since_tick = 0.0f;
}

void tempo_set_bpm(tempo_t *t, float bpm)
{
    if (!t->external || t->period <= 0.0f)
        t->bpm = dsp_clampf(bpm, 20.0f, 300.0f);
}

void tempo_set_external(tempo_t *t, int on)
{
    t->external = on;
    t->have_tick = 0;
    t->period = 0.0f;
}

void tempo_clock(tempo_t *t, uint32_t time)
{
    if (!t->external)
        return;
    if (t->have_tick) {
        const float dt = (float)(uint32_t)(time - t->last_tick);
        if (t->period <= 0.0f)
            t->period = dt;
        else if (dt > 0.5f * t->period && dt < 2.0f * t->period)
            t->period += 0.08f * (dt - t->period); /* ~12 ticks (half a beat) of smoothing */
        else
            t->period = dt; /* tempo jump or pause: restart the estimate */
        if (t->period > 0.0f)
            t->bpm = dsp_clampf(60.0f * t->sr / (24.0f * t->period), 20.0f, 300.0f);
    }
    t->last_tick = time;
    t->have_tick = 1;
    if (t->running) {
        t->ticks++;
        /* lock the position to the tick count (the clock is the reference) */
        t->tick_beat = (float)(t->ticks % (uint32_t)(TEMPO_WRAP_BEATS * 24.0f)) * (1.0f / 24.0f);
        t->beat = t->tick_beat;
        tempo_sync_internal(t);
    }
    t->since_tick = 0.0f;
}

void tempo_start(tempo_t *t)
{
    t->ticks = 0;
    t->beat = t->tick_beat = 0.0f;
    tempo_sync_internal(t);
    t->since_tick = 0.0f;
    t->running = 1;
}

void tempo_continue(tempo_t *t) { t->running = 1; }
void tempo_stop(tempo_t *t) { t->running = 0; }

void tempo_advance(tempo_t *t, int n)
{
    if (!t->running)
        return;
    if (t->external && t->have_tick) {
        /* between ticks: extrapolate from the smoothed period, never past the next tick */
        t->since_tick += (float)n;
        const float ahead = t->period > 0.0f ? t->since_tick / (24.0f * t->period) : 0.0f;
        t->beat = t->tick_beat + dsp_minf(ahead, 1.0f / 24.0f);
        tempo_sync_internal(t);
        return;
    }
    t->frac += t->bpm * (1.0f / 60.0f) * (float)n / t->sr;
    while (t->frac >= 1.0f) {
        t->frac -= 1.0f;
        if (++t->whole >= (int)TEMPO_WRAP_BEATS)
            t->whole = 0;
    }
    t->beat = (float)t->whole + t->frac;
}
