/* patch.h — sound patches as versioned JSON files (host side). A patch names an engine, sets
 * parameters in physical units (or labels for enumerated ones) — engine parameters and global ones
 * (master, effects: chorus_*, delay_*, reverb_*) — and carries an audition phrase.
 * Format documented in docs/patch_format.md. */
#ifndef PATCH_H
#define PATCH_H

#include "catalog.h"
#include "demos.h"

#define PATCH_VERSION 1
#define PATCH_MAX_PARAMS 512
#define PATCH_MACROS 8 /* the part's macros (dsp/mod/partmod.h) */
#define PATCH_MACRO_NAME 32
#define PATCH_MAX_CLIP 256 /* notes of a patch's clip */

typedef struct {
    const engine_ops_t *engine;
    char name[64];
    char kit[64];                               /* sample kit by name ("synth_drums"), "" none */
    char macro_name[PATCH_MACROS][PATCH_MACRO_NAME]; /* the "macros" section's names ("" unnamed) */
    int n;
    int ids[PATCH_MAX_PARAMS];                  /* synth_set_param ids (engine or global) */
    const param_desc_t *desc[PATCH_MAX_PARAMS];
    float values[PATCH_MAX_PARAMS];             /* physical units */
    /* audition */
    char notes[2048];
    float bpm, step_beats, gate;
    int velocity;
    float humanize_vel, humanize_ms; /* random +/- velocity and 0..ms delay per note (deterministic) */
    float swing;                     /* 0..0.5: every odd step is delayed by swing x step (shuffle) */
    /* the clip (optional "clip" section): MIDI notes in beats, played instead of the audition phrase */
    int clip_n;                      /* 0: no clip */
    float clip_beats;                /* its length (the loop of the web sequencer) */
    float clip[PATCH_MAX_CLIP][4];   /* pitch, start (beats), length (beats), velocity */
} patch_t;

/* How the host finds the sample kit a patch names (host/kitfile.c's kitfile_find on the Mac): set by the tool
 * before patch_apply; NULL, or a kit it cannot find: the patch plays without samples. */
extern const uint8_t *(*patch_kit_resolver)(const char *name, size_t *bytes);

/* Loads and validates a patch. Returns 0, or -1 with a message in err (unknown parameter names
 * list the valid ones). Out-of-range values are clamped and reported in warn (may be empty). */
int patch_load(const char *path, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen);
/* Same, from JSON text already in memory (web build, embedded patches). */
int patch_parse(const char *text, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen);
struct json;
/* Same, from a patch object inside a larger document (a song's inline patch); obj stays the caller's. */
int patch_from_object(const struct json *obj, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen);

struct json;
/* Sets parameters from a JSON object {name: value}; later values override earlier ones.
 * Same error/warning conventions as patch_load. */
int patch_set_params(patch_t *p, const struct json *params, char *err, size_t errlen, char *warn, size_t warnlen);

/* Adds engine selection + parameter events at t = 0. */
void patch_apply(const patch_t *p, seq_t *s);

/* Adds the audition: the patch's clip when it has one and notes is NULL (its notes at p->bpm, over its length),
 * else the phrase (notes override p->notes when not NULL). Returns its length in seconds, or -1 with a message in
 * err on a syntax error.
 * Syntax: space-separated steps; each step is a note (C2, Eb3, F#4), a chord (C3+E3+G3), '-' (rest)
 * or '_' (tie: extends the previous step). A note suffix '!' = accent (velocity 127), '?' = soft
 * (velocity 50), per note, also inside chords ("C2!+F#2?"); '~' = slide into the next note (the note
 * is held past the next step start, so a mono/legato engine glides: "C2~ C3 D#2").
 * humanize_vel / humanize_ms add a reproducible random velocity and delay to every note (chord notes
 * get different delays: they roll slightly, like fingers). */
float patch_audition(const patch_t *p, const char *notes, seq_t *s, char *err, size_t errlen);

/* "C3", "Eb2", "F#4" (C4 = 60) -> MIDI note, or -1. */
int patch_parse_note(const char *t, int len);

#endif /* PATCH_H */
