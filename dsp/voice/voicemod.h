/* voicemod.h — per-voice modulation, the same for every engine: each voice has its own MSEG and LFO, its note,
 * velocity and random value, and its MPE expression (per-note pitch bend, pressure, slide); 4 routes send
 * them to five voice destinations every engine understands: pitch (semitones), level (dB), pan, brightness
 * (octaves of the engine's main brightness control) and timbre (-1..1 on the engine's main timbre control).
 * Microtuning is a per-voice pitch offset too. Lives in vpoly; engines read vpoly_vmod(). */
#ifndef VOICEMOD_H
#define VOICEMOD_H

#include "../env/mseg.h"

enum { VM_PITCH = 0, VM_LEVEL, VM_PAN, VM_BRIGHT, VM_TIMBRE, VM_DST_COUNT };
enum { VM_SRC_NONE = 0, VM_SRC_MSEG, VM_SRC_LFO, VM_SRC_VELOCITY, VM_SRC_NOTE, VM_SRC_RANDOM, VM_SRC_PRESSURE,
       VM_SRC_SLIDE, VM_SRC_COUNT };
#define VM_ROUTES 4

/* Full-scale effect of amount 1 on each destination. */
#define VM_PITCH_RANGE 12.0f /* semitones */
#define VM_LEVEL_RANGE 24.0f /* dB */
#define VM_BRIGHT_RANGE 4.0f /* octaves */

typedef struct {
    mseg_shape_t shape;               /* the per-voice MSEG */
    float lfo_rate;                   /* Hz, or a division when lfo_div > 0 */
    int lfo_div, lfo_wave;            /* tempo division (0 = free), shape 0..4 (sine, tri, saw, square, sh) */
    int src[VM_ROUTES], dst[VM_ROUTES];
    float amount[VM_ROUTES];
    int routes;                       /* number of usable routes (0: no per-voice modulation) */
    const float *tuning;              /* 12 offsets in semitones by pitch class from tuning_root, or NULL */
    int tuning_root;
    int mpe;                          /* per-note pitch bend, pressure, slide are used */
    float mpe_bend_range;             /* semitones */
    float dt, beats_per_s;            /* block length (s) and tempo (beats per second), set each block */
} voicemod_cfg_t;

typedef struct {
    mseg_t env;
    float lfo_phase, lfo_sh;
    float src[VM_SRC_COUNT];
    float tune;                       /* microtuning offset of the note (semitones) */
    float bend;                       /* MPE per-note bend, -1..1 */
    float out[VM_DST_COUNT];
    float gain_prev;                  /* linear level reached at the end of the previous block */
    float pan_prev;                   /* pan offset (-1..1) reached at the end of the previous block */
    int fresh;                        /* the level ramp starts from the first value */
    uint32_t rng;
} voicemod_t;

void voicemod_init(voicemod_t *m, uint32_t seed);
void voicemod_start(voicemod_t *m, const voicemod_cfg_t *c, int note, float velocity, int hard);
void voicemod_note(voicemod_t *m, const voicemod_cfg_t *c, int note); /* legato: new note, envelope goes on */
void voicemod_release(voicemod_t *m, const voicemod_cfg_t *c);
/* Once per block: updates out[] (all zero when nothing is configured). */
void voicemod_block(voicemod_t *m, const voicemod_cfg_t *c);
/* 1 if this configuration can move anything at all (routes, tuning, MPE). */
int voicemod_cfg_active(const voicemod_cfg_t *c);

#endif /* VOICEMOD_H */
