/* voice_alloc.h — polyphonic voice allocation, engine independent.
 * Same note already active: retrigger it. Otherwise a free voice; otherwise steal, by priority:
 * oldest released voice, then oldest pedal-sustained voice, then the quietest.
 * The fade-out (2 to 5 ms) before reuse is done by the engine (*steal = 1). */
#ifndef VOICE_ALLOC_H
#define VOICE_ALLOC_H

#include "../core/dsp.h"

#define VALLOC_MAX 16

enum { VSLOT_FREE = 0, VSLOT_HELD, VSLOT_SUSTAINED, VSLOT_RELEASED };

typedef struct {
    int16_t note;
    uint8_t state;
    uint32_t age; /* trigger order (smaller = older) */
} valloc_slot_t;

typedef struct {
    valloc_slot_t s[VALLOC_MAX];
    int n;
    uint32_t clock;
    int sustain;
} valloc_t;

void valloc_init(valloc_t *a, int n_voices);
/* levels[v]: current amplitude of each voice (may be NULL). Returns the chosen voice;
 * *steal = 1 if it was playing another note (fade needed). */
int valloc_note_on(valloc_t *a, int note, const float *levels, int *steal);
/* Returns the voice to release, or -1 (unknown note or held by the pedal). */
int valloc_note_off(valloc_t *a, int note);
/* Sustain pedal. On release, writes the voices to release into out[]; returns their count. */
int valloc_set_sustain(valloc_t *a, int on, int *out);
/* Releases all held notes; writes the voices into out[], returns their count. */
int valloc_release_all(valloc_t *a, int *out);
/* Call when the voice has finished sounding (envelope idle). */
void valloc_voice_done(valloc_t *a, int v);

#endif /* VOICE_ALLOC_H */
