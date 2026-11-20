/* demos.h — scripted demo sequences (notes, CCs, parameter automation) rendered offline through
 * the public API. Used by synth_render and the examples. Host only (allocates). */
#ifndef DEMOS_H
#define DEMOS_H

#include <stddef.h>
#include <stdint.h>

#include "synth.h"

enum { SEQ_MIDI = 0, SEQ_PARAM, SEQ_ENGINE, SEQ_KIT };

typedef struct {
    float t; /* seconds */
    int order; /* insertion order, keeps sorting stable */
    uint8_t kind, len;
    uint8_t msg[3];
    int id;
    float value; /* normalized for SEQ_PARAM */
    const void *kit; /* SEQ_KIT: a sample kit (dsp/core/kit.h) for part 0, kept by the caller */
    size_t kit_bytes;
} seq_event_t;

typedef struct {
    seq_event_t *ev;
    int n, cap;
} seq_t;

void seq_init(seq_t *s);
void seq_free(seq_t *s);
void seq_midi(seq_t *s, float t, uint8_t a, uint8_t b, uint8_t c);
void seq_note(seq_t *s, float t, int note, int vel, float dur);
void seq_param_norm(seq_t *s, float t, int id, float norm);
void seq_va(seq_t *s, float t, int va_param, float physical); /* physical units, see engines/va.h */
void seq_engine(seq_t *s, float t, synth_engine_t e);
void seq_kit(seq_t *s, float t, const void *kit, size_t bytes);

/* Renders a sequence (synth_init is called first). *L and *R are malloc'ed, caller frees. Returns the frame
 * count, or -1 if out of memory (*L and *R are then NULL). */
int seq_render(seq_t *s, float sample_rate, float seconds, float **L, float **R);

typedef struct {
    const char *name;
    synth_engine_t engine;
    const char *description;
    float seconds;
    void (*build)(seq_t *s);
} demo_t;

extern const demo_t demos[];
extern const int demo_count;
const demo_t *demo_find(const char *name);
/* First demo for an engine. */
const demo_t *demo_for_engine(synth_engine_t e);

#endif /* DEMOS_H */
