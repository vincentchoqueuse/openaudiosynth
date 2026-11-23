/* synth.h — the single public API of the synthesis engine.
 * The calls on one instance must come from one thread at a time (normally the audio thread): a multi-threaded
 * host forwards MIDI and parameter changes to it through a lock-free queue (see host/).
 * Parameter names, ranges and help: docs/catalog.md (generated from the descriptor tables). */
#ifndef SYNTH_H
#define SYNTH_H

#include <stddef.h>
#include <stdint.h>

#include "core/dsp.h" /* SYNTH_BLOCK */

typedef enum {
    SYNTH_ENGINE_NONE = -1, /* a part with no engine (off) */
    SYNTH_ENGINE_SINE = 0, /* reference: polyphonic sine */
    SYNTH_ENGINE_VA,       /* subtractive: 2 oscillators (incl. supersaw) + noise, SVF, 2 ADSR, LFOs */
    SYNTH_ENGINE_MODAL,    /* physical modelling: struck/bowed resonator banks (bells, bars, glass, piano) */
    SYNTH_ENGINE_DRUMS,    /* drum machine: analog, hybrid and glitch kits, GM drum notes */
    SYNTH_ENGINE_ACID,     /* monophonic acid bass: resonant 18 dB ladder, accent, slide */
    SYNTH_ENGINE_MONO,     /* classic monosynth: 3 oscillators, 24 dB ladder, contours, glide */
    SYNTH_ENGINE_POLY,     /* classic polysynth: sync, poly-mod, 24 dB ladder, unison */
    SYNTH_ENGINE_FM6,      /* 6-operator FM: 32 algorithms, rate/level envelopes, sysex import */
    SYNTH_ENGINE_WT,       /* wavetable: 2 morphing wavetable oscillators, warps, unison, mod matrix */
    SYNTH_ENGINE_COUPLED,  /* coupled-oscillator networks: Stuart-Landau partials, modulus <-> phase coupling */
    SYNTH_ENGINE_SELFOSC,  /* self-oscillating Van der Pol / Duffing oscillators forced by the note */
    SYNTH_ENGINE_SAMPLER,  /* drum sampler: a kit of samples (synth_part_kit), 16 pads with their own settings */
    SYNTH_ENGINE_DRONE,    /* drone machine: wandering partials, warm body, air, sparkles; glacial .. warm */
    SYNTH_ENGINE_MICRO,    /* precision sines: per-sample envelopes, fixed pitch, clicks, bursts, ratchets, gates */
    SYNTH_ENGINE_DRONE2,   /* drone field: sine partials that own the wells dug by the notes and migrate between them */
    SYNTH_ENGINE_COUNT
} synth_engine_t;

/* Parameter identifiers:
 *   0 .. 255  : parameters of the current engine (e.g. VA_P_CUTOFF in engines/va.h);
 *   >= 256    : global parameters below (master and effect bus), independent of the engine. */
#define SYNTH_PARAM_GLOBAL 256
enum {
    SYNTH_PARAM_MASTER_DB = SYNTH_PARAM_GLOBAL, /* -60 .. +6 dB, default 0 dB */
    /* effect bus: chorus -> tape delay -> plate reverb (all off by default) */
    SYNTH_PARAM_CHORUS_MODE,     /* off, I, II, I+II, ensemble          (CC 93 sets the mix) */
    SYNTH_PARAM_CHORUS_MIX,
    SYNTH_PARAM_DELAY_MIX,       /* CC 94 */
    SYNTH_PARAM_DELAY_TIME,      /* seconds */
    SYNTH_PARAM_DELAY_RATIO,
    SYNTH_PARAM_DELAY_FEEDBACK,
    SYNTH_PARAM_DELAY_TONE,
    SYNTH_PARAM_DELAY_DRIVE,
    SYNTH_PARAM_DELAY_WOW,
    SYNTH_PARAM_DELAY_PINGPONG,
    SYNTH_PARAM_REVERB_MIX,      /* CC 91 */
    SYNTH_PARAM_REVERB_DECAY,
    SYNTH_PARAM_REVERB_DAMPING,
    SYNTH_PARAM_REVERB_PREDELAY,
    SYNTH_PARAM_REVERB_FREEZE,   /* CC 69 >= 64 */
    /* tempo: internal BPM or MIDI clock (0xF8 ticks, 0xFA start, 0xFB continue, 0xFC stop via synth_midi) */
    SYNTH_PARAM_TEMPO,           /* 20 .. 300 BPM, default 120 */
    SYNTH_PARAM_CLOCK,           /* internal / midi */
    SYNTH_PARAM_DELAY_SYNC,      /* off, or a division (1/16, 1/8d, 1/4...): the delay time follows the tempo */
    /* microtuning (octave-repeating, 12 notes): a preset or 12 custom offsets in cents */
    SYNTH_PARAM_TUNING,          /* equal, just, pythagorean, meantone, werckmeister, custom */
    SYNTH_PARAM_TUNING_ROOT,     /* pitch class the tuning is built on (C .. B) */
    SYNTH_PARAM_TUNE_0,          /* custom: offset of each degree from root, -100 .. +100 cents (12 ids) */
    SYNTH_PARAM_TUNE_11 = SYNTH_PARAM_TUNE_0 + 11,
    SYNTH_PARAM_A4_HZ,           /* concert pitch: frequency of A4, 415 .. 466 Hz, default 440 */
    /* output filter: on every part's output, after its engine and before the effects (off by default) */
    SYNTH_PARAM_OUT_FILTER,      /* off, lp, hp, bp (12 dB state-variable) */
    SYNTH_PARAM_OUT_CUTOFF,      /* 20 .. 20000 Hz */
    SYNTH_PARAM_OUT_RESO,        /* 0 .. 1 */
    /* crush: after the output filter, the resolution and the sample rate brought down (24 bits, 48 kHz = off) */
    SYNTH_PARAM_CRUSH_BITS,      /* 2 .. 24 bits */
    SYNTH_PARAM_CRUSH_RATE,      /* 200 .. 48000 Hz (sample and hold) */
    SYNTH_PARAM_GLOBAL_END
};

/* ---- Parts (multitimbral) ----
 * SYNTH_PARTS parts play at the same time, each with its own engine, parameters, MIDI channel, key range
 * and mixer strip; they share the effect bus. At init only part 0 is on (engine va, omni, send 1), so the
 * single-part functions below behave as before: synth_set_engine and synth_set_param address part 0. */
#ifndef SYNTH_PARTS
#define SYNTH_PARTS 8 /* a target lowers it: one part's state is ~40 KB (the largest engine), an instance ~340 KB */
#endif

#define SYNTH_PART_PARAM 512
enum {
    SYNTH_PART_LEVEL = SYNTH_PART_PARAM, /* -60 .. +12 dB, default 0 */
    SYNTH_PART_PAN,                      /* -1 .. 1 balance, default 0 */
    SYNTH_PART_MUTE,                     /* off / on */
    SYNTH_PART_CHANNEL,                  /* 0 = omni, 1..16; default: part k listens to channel k + 1, part 0 omni */
    SYNTH_PART_KEY_LO,                   /* lowest note played by the part (splits), default 0 */
    SYNTH_PART_KEY_HI,                   /* highest note, default 127 */
    SYNTH_PART_TRANSPOSE,                /* -48 .. +48 semitones */
    SYNTH_PART_FX_SEND,                  /* 0..1 of the part into the effect bus (the rest bypasses it), default 1 */
    SYNTH_PART_MPE,                      /* off / on: notes on channels 2..16 carry their own bend, pressure, slide (CC 74) */
    SYNTH_PART_MPE_BEND,                 /* per-note bend range, 0 .. 96 semitones, default 48 */
    SYNTH_PART_PARAM_END
};

/* Modulation of a part (same for every engine): 8 macros, 2 LFOs (free or tempo-synced), 2 MSEGs, MIDI
 * sources, 8 routes onto the engine's realtime parameters. Ids SYNTH_MOD_PARAM + index; names, ranges and
 * help in docs/catalog.md ("macro1", "mlfo1_sync", "mseg1_t2", "route1_destination"...). */
#define SYNTH_MOD_PARAM 1024

/* ---- Instances ----
 * Every function takes the instance it acts on. An instance lives in memory the caller provides (dsp/ never
 * allocates): synth_create places one in synth_size() bytes (a plugin: one per plugin instance); synth_default
 * is a ready static instance (the board, the tools, the tests). Instances are independent, except the engines'
 * read-only tables (wavetables...), shared and built by the first synth_init: initialize the first instance
 * before using several from different threads. One instance is used by one thread at a time. */
typedef struct synth synth_t;

/* Bytes one instance needs (its state and its effect memory). */
size_t synth_size(void);
/* Places an instance in mem (synth_size() bytes, 16-byte aligned) and initializes it; NULL if mem does not fit. */
synth_t *synth_create(void *mem, size_t bytes, float sample_rate);
/* The static instance (DSP_FAST_DATA state, DSP_LARGE_DATA effect memory); call synth_init on it first. */
synth_t *synth_default(void);

/* Sets the engine of a part (SYNTH_ENGINE_NONE switches it off); same fade as synth_set_engine. */
void synth_part_engine(synth_t *s, int part, synth_engine_t engine);

/* Sets a parameter of a part: an engine parameter id (0..255), a SYNTH_PART_* id or a modulation id
 * (SYNTH_MOD_PARAM + index); value normalized in
 * [0, 1] like synth_set_param. Global ids (SYNTH_PARAM_GLOBAL..) are accepted and ignore the part. */
void synth_part_param(synth_t *s, int part, int id, float value);

/* Gives a part a sample kit (format: core/kit.h), played by the engines that play samples (sampler). The kit
 * is read where it lies, never copied: it must stay valid and unchanged until the part gets another one (or
 * NULL) or synth_init is called. Notes sounding on the part stop at once. Invalid kits are ignored (no
 * samples). synth_init removes every part's kit. */
void synth_part_kit(synth_t *s, int part, const void *kit, size_t bytes);

/* Initializes all state (no allocation). Default engine: SYNTH_ENGINE_VA, effects off. */
void synth_init(synth_t *s, float sample_rate);

/* Switches engine. If voices are sounding, a one-block fade-out precedes the switch;
 * subsequent events wait until the switch is done. Effects keep running (tails continue). */
void synth_set_engine(synth_t *s, synth_engine_t engine);

/* Complete MIDI message (status + data, running status already resolved), 1 to 3 bytes; real-time clock
 * messages (1 byte) drive the tempo when SYNTH_PARAM_CLOCK = midi.
 * sample_offset: offset in samples from the start of the next synth_render
 * (0 if unknown). Time resolution is one block (SYNTH_BLOCK samples). */
void synth_midi(synth_t *s, const uint8_t *msg, int len, int sample_offset);

/* Sets a parameter; value is normalized in [0, 1] (descriptor curve). Smoothed internally. */
void synth_set_param(synth_t *s, int id, float value);

/* Produces n stereo samples (any n; split internally into SYNTH_BLOCK blocks). */
void synth_render(synth_t *s, float *L, float *R, int n);

#endif /* SYNTH_H */
