/* engine.h — the interface every synthesis engine implements, and the engine registry.
 * synth.c only talks to engines through this table: adding an engine = one source file exporting an
 * engine_ops_t + one line in registry.c + one value in synth_engine_t.
 * Notes, sustain and all-notes/sound-off go through the engine's shared polyphony manager (vpoly). */
#ifndef ENGINE_H
#define ENGINE_H

#include "../mod/params.h"
#include "../synth.h"
#include "../voice/voice.h"

/* MIDI CC -> parameter (normalized value = CC / 127). */
typedef struct {
    uint8_t cc;
    uint16_t param;
} engine_cc_t;

/* Named note (drum instruments, key switches...). */
typedef struct {
    uint8_t note;
    const char *name;
} engine_note_t;

/* One stage of an engine's signal chain, for UIs that draw it (the DSP does not read this): an audio stage
 * (source, filter, amp...) with its audio inputs, or a modulator with the stages it acts on. A stage can be
 * switched off through one of its parameters (osc 2 level to 0, filter 2 mode to off...): off, the signal passes
 * through it unchanged (or the source is silent). */
typedef struct {
    const char *id;      /* "osc2" */
    const char *label;   /* "Osc 2" */
    const char *kind;    /* "source", "filter", "amp", "mod" (modulator), "voice" (voice settings) */
    const char *params;  /* its parameters, space-separated; a name ending in '_' is a prefix ("osc2_") */
    const char *inputs;  /* audio stages feeding it, space-separated ("" = a source) */
    const char *targets; /* modulators: the stages they act on */
    const char *enable;  /* parameter that switches the stage on and off, or NULL (always on) */
    float off, on;       /* its value when off, and the value it takes when switched on */
} engine_stage_t;

typedef struct {
    const char *name;        /* identifier used in patches ("va") */
    const char *description; /* one line, for catalogs and agents */
    const param_desc_t *params;
    int param_count;
    int voices;              /* polyphony */

    void (*init)(void *state, float sample_rate);           /* all parameters at their defaults */
    vpoly_t *(*poly)(void *state);                          /* note handling */
    void (*set_param)(void *state, int id, float norm);     /* id = index in params, value 0..1 */
    void (*pitch_bend)(void *state, float bend);            /* -1..1 */
    void (*mod_wheel)(void *state, float value);            /* 0..1, may be NULL */
    void (*render)(void *state, float *L, float *R);        /* adds one SYNTH_BLOCK into L and R */

    const engine_cc_t *cc_map;  /* default CC assignments (applied by synth.c), may be NULL */
    int cc_count;
    const engine_note_t *notes; /* named notes (e.g. drum map), may be NULL */
    int note_count;

    /* Optional read-only data shared by every instance (e.g. wavetables): shared_floats of memory in
     * synth.c's engine arena, filled once by shared_build (first synth_init, never in the audio path),
     * handed to the state by shared_attach after each init. floats may be smaller than asked when the
     * arena is short: the engine must cope (fewer tables...). */
    size_t shared_floats;
    void (*shared_build)(float *mem, size_t floats);
    void (*shared_attach)(void *state, const float *mem, size_t floats);

    /* Optional: the engine plays samples. Called by synth_part_kit and after every engine start with the part's
     * kit (core/kit.h; NULL, 0 for none). The kit stays where it is and must stay valid until replaced; the
     * engine silences its voices first (none reads the previous kit after the call). */
    void (*kit)(void *state, const uint8_t *kit, size_t bytes);

    /* Optional description of the signal chain (see engine_stage_t), for UIs; stages in signal order. */
    const engine_stage_t *stages;
    int stage_count;

    /* Optional: the tempo (BPM) and the beat position at the end of the block about to be rendered, called by
     * synth.c once per block before render, for engines with tempo-synced parts of their own. */
    void (*tempo)(void *state, float bpm, float beat);

    /* Optional, for displays (synth_probe.h): frame `frame` of the wavetable oscillator `osc` plays, as n samples of one
     * cycle (n <= 2048) into out; returns the table's frame count (out untouched when frame is out of range), 0 when
     * the engine has no table to show. Never called from the audio path. */
    int (*probe_table)(const void *state, int osc, int frame, float *out, int n);
} engine_ops_t;

/* Registry, indexed by synth_engine_t. */
extern const engine_ops_t *const engine_registry[SYNTH_ENGINE_COUNT];
/* Engine by name, or NULL. */
const engine_ops_t *engine_find(const char *name);
/* Its synth_engine_t id, or -1. */
int engine_id(const engine_ops_t *e);

#endif /* ENGINE_H */
