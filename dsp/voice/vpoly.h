/* vpoly.h — engine-independent polyphony manager: voice allocation (voice_alloc), click-free voice
 * stealing (3 ms fade-out, then the new note starts), sustain pedal, all-notes/sound-off and voice
 * lifetime, and monophonic play (mono / legato, last-note priority, return to the previous held note).
 * An engine keeps only its DSP state per voice and provides the callbacks below.
 *
 * Engine side, per block:            for each voice v with vpoly_active(p, v):
 *                                      render the voice into x[], multiply by vpoly_fade(p, v, g, n)
 *                                      vpoly_end_block(p, v, still_sounding)                        */
#ifndef VPOLY_H
#define VPOLY_H

#include "voice_alloc.h"
#include "voicemod.h"

typedef struct {
    /* Start (or retrigger) voice v. hard = 1: the voice was silent, reset its state; hard = 0: the
     * same note is struck again while still sounding, restart from the current state (no click). */
    void (*start)(void *eng, int v, int note, float velocity, int hard);
    void (*release)(void *eng, int v);          /* note-off: enter the release stage */
    float (*level)(const void *eng, int v);     /* current amplitude, used to pick the voice to steal */
    void (*silence)(void *eng, int v);          /* voice faded out to silence: reset envelopes */
    /* Optional (may be NULL): change the note of a sounding voice WITHOUT retriggering its
     * envelopes (legato). If NULL, start(hard = 0) is used instead. */
    void (*legato)(void *eng, int v, int note, float velocity);
} vpoly_ops_t;

typedef enum {
    VPOLY_POLY = 0, /* one voice per note */
    VPOLY_MONO,     /* one voice, every note retriggers the envelopes */
    VPOLY_LEGATO    /* one voice, overlapping notes do not retrigger (slides) */
} vpoly_mode_t;

#define VPOLY_HELD_MAX 16

typedef struct {
    uint8_t active, pending, pending_off;
    int16_t note, pending_note; /* pending_note < 0: fade to silence */
    float pending_vel, fade;
} vpoly_voice_t;

typedef struct {
    valloc_t alloc;
    vpoly_voice_t v[VALLOC_MAX];
    int n;
    float fade_step;
    const vpoly_ops_t *ops;
    void *eng;
    /* monophonic state */
    int mode;
    int16_t held[VPOLY_HELD_MAX]; /* keys held down, oldest first */
    int priority;                 /* which held key sounds in the mono modes: vpoly_priority_t */
    int nheld;
    int sustain, release_pending;
    int overlap;                  /* the last note change happened while another key was held */
    float last_vel;
    /* where the event being handled lands in the block about to be rendered (0 .. SYNTH_BLOCK-1), set by synth.c
     * around each MIDI event and 0 otherwise: engines that start notes on their sample read it in start/release */
    int onset;
    /* per-voice modulation (MSEG, LFO, MPE, microtuning), configured by the part */
    voicemod_t vm[VALLOC_MAX];
    uint8_t vm_done[VALLOC_MAX];  /* out[] computed for this block */
    const voicemod_cfg_t *vm_cfg; /* NULL: nothing modulated per voice */
} vpoly_t;

void vpoly_init(vpoly_t *p, int n_voices, float sample_rate, const vpoly_ops_t *ops, void *eng);
/* Per-voice modulation configuration (owned by the part), or NULL. */
static inline void vpoly_set_vmod(vpoly_t *p, const voicemod_cfg_t *cfg) { p->vm_cfg = cfg; }
/* Per-voice modulation of voice v for this block: VM_PITCH (st), VM_LEVEL (dB), VM_PAN, VM_BRIGHT (oct), VM_TIMBRE. */
static inline const float *vpoly_vmod(vpoly_t *p, int v)
{
    if (!p->vm_done[v]) {
        voicemod_block(&p->vm[v], p->vm_cfg);
        p->vm_done[v] = 1;
    }
    return p->vm[v].out;
}
/* MPE expression of the voice(s) playing `note`: what = VM_SRC_PRESSURE, VM_SRC_SLIDE (0..1) or -1 = bend (-1..1). */
void vpoly_expression(vpoly_t *p, int note, int what, float value);
void vpoly_note_on(vpoly_t *p, int note, float velocity);
void vpoly_note_off(vpoly_t *p, int note);
void vpoly_sustain(vpoly_t *p, int on);
void vpoly_all_notes_off(vpoly_t *p);
void vpoly_all_sound_off(vpoly_t *p);
int vpoly_active_count(const vpoly_t *p);
void vpoly_set_mode(vpoly_t *p, vpoly_mode_t mode);
/* Which of the held keys sounds in the mono modes: the last one played (default), the lowest (the classic
 * monosynths) or the highest. A key that does not take priority changes nothing; releasing the sounding key
 * moves to the next one by the same rule, without retriggering. */
typedef enum { VPOLY_PRIO_LAST = 0, VPOLY_PRIO_LOW, VPOLY_PRIO_HIGH } vpoly_priority_t;
void vpoly_set_priority(vpoly_t *p, vpoly_priority_t priority);
/* 1 if the current note was reached while another key was held (legato playing): engines use it
 * to decide whether to glide. Valid inside the start/legato callbacks. */
static inline int vpoly_overlap(const vpoly_t *p) { return p->overlap; }

static inline int vpoly_active(const vpoly_t *p, int v) { return p->v[v].active; }
static inline int vpoly_note(const vpoly_t *p, int v) { return p->v[v].note; }

/* Fills gain[0..n) with the steal fade of voice v (all 1 when not stealing). Returns 0 when the
 * gains are all 1, so the caller may skip the multiplication. */
int vpoly_fade(vpoly_t *p, int v, float *gain, int n);
/* End of the block for voice v: after a completed fade, starts the pending note (or frees the voice);
 * otherwise frees the voice when sounding == 0 (its envelope reached silence). */
void vpoly_end_block(vpoly_t *p, int v, int sounding);

#endif /* VPOLY_H */
