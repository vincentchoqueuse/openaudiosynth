/* sampler.h — a drum sampler: plays the samples of a kit (core/kit.h, given by synth_part_kit), one per MIDI
 * note. The first 16 samples of the kit are pads with their own settings: speed (pitch), start and end points,
 * reverse, loop, decay, level, pan, low-pass cutoff and choke group (a closed hi-hat stops the open one);
 * further samples play with the default settings. One-shot: a pad plays to its end whatever the note-off,
 * unless it loops (it then stops at the note-off, after the release). Band-limited playback (osc/splay.h). */
#ifndef ENGINE_SAMPLER_H
#define ENGINE_SAMPLER_H

#include "../core/kit.h"
#include "../core/smooth.h"
#include "../filter/svf_tpt.h"
#include "../osc/splay.h"
#include "engine.h"

#define SAMPLER_VOICES 12
#define SAMPLER_PADS 16

enum { SAMPLER_P_VOLUME = 0, SAMPLER_P_TUNE, SAMPLER_P_VELOCITY, SAMPLER_P_RESONANCE, SAMPLER_P_RELEASE, SAMPLER_P_PAD0 };
/* the settings of a pad */
enum {
    SAMPLER_TUNE = 0, /* semitones: playback speed 2^(tune/12) */
    SAMPLER_START,    /* 0..1 of the sample */
    SAMPLER_END,      /* 0..1 of the sample */
    SAMPLER_REVERSE,
    SAMPLER_LOOP,
    SAMPLER_DECAY,    /* s to -60 dB */
    SAMPLER_LEVEL,    /* dB */
    SAMPLER_PAN,      /* -1..1 */
    SAMPLER_CUTOFF,   /* Hz, low-pass (20 kHz: open) */
    SAMPLER_CHOKE,    /* group 1..8, 0 none */
    SAMPLER_PAD_N
};
#define SAMPLER_P(pad, k) (SAMPLER_P_PAD0 + (pad) * SAMPLER_PAD_N + (k))
#define SAMPLER_P_COUNT SAMPLER_P(SAMPLER_PADS, 0)

extern const param_desc_t sampler_params[SAMPLER_P_COUNT];
extern const engine_ops_t sampler_engine;

typedef enum { SAMPLER_IDLE = 0, SAMPLER_ATTACK, SAMPLER_DECAY_STAGE, SAMPLER_RELEASE_STAGE } sampler_stage_t;

typedef struct {
    splay_t play;
    splay_t tail;      /* the previous hit of a retriggered pad, fading out */
    float tail_gain;
    svf_t lp;
    float env, release_coef;
    int stage;         /* sampler_stage_t */
    int entry;         /* kit entry playing, -1 none */
    float vel_gain;
    float rate_prev, gain_prev, cut_prev;
    int fresh;         /* first block: no ramp from the previous note's values */
} sampler_voice_t;

typedef struct {
    const int16_t *data;
    int32_t frames;
} sampler_sample_t;

typedef struct {
    float sr;
    float rate_scale;                  /* KIT_RATE / sr: a sample keeps its pitch at any output rate */
    float p[SAMPLER_P_COUNT];
    smooth_t vol;
    float vol_prev, bend_semi;
    uint32_t nan_count;
    int count;                         /* samples in the kit */
    sampler_sample_t smp[KIT_MAX];
    int8_t note_map[128];              /* kit entry of each note, -1 none */
    vpoly_t poly;
    sampler_voice_t v[SAMPLER_VOICES];
} sampler_t;

#endif /* ENGINE_SAMPLER_H */
