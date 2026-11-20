/* gull.h — seabird call engine (a joke engine that takes itself seriously): each note is a call, a series of
 * syllables ("kee-ow, kyow kyow...") whose pitch jumps up then slides down, voiced by a narrow anti-aliased pulse
 * (nasal) through a vowel formant filter that moves from "ee" to "ow" along each syllable, roughened by a
 * subharmonic (period doubling, as in a breaking voice) and pitch jitter, with some breath noise. Every syllable
 * is a little different (randomness); several notes make a colony. Calls of gulls: harmonic, harsh, arched
 * pitch contours of 0.1 to 0.5 s (bioacoustics field guides), rebuilt here by ear, not from recordings. */
#ifndef ENGINE_GULL_H
#define ENGINE_GULL_H

#include "../env/adsr.h"
#include "../filter/vfilter.h"
#include "../osc/polyblep.h"
#include "engine.h"

#define GULL_VOICES 8

enum {
    GULL_P_CALLS = 0, GULL_P_LOOP, GULL_P_SYLLABLE, GULL_P_GAP, GULL_P_ACCEL,
    GULL_P_RISE, GULL_P_FALL, GULL_P_PEAK,
    GULL_P_ROUGH, GULL_P_JITTER, GULL_P_NOISE, GULL_P_PW,
    GULL_P_FORMANT, GULL_P_RESO, GULL_P_VOWEL, GULL_P_VOWEL_END,
    GULL_P_RANDOM, GULL_P_RELEASE, GULL_P_VOLUME, GULL_P_SPREAD, GULL_P_BEND_RANGE,
    GULL_P_COUNT
};
extern const param_desc_t gull_params[GULL_P_COUNT];
extern const engine_ops_t gull_engine;

typedef struct {
    osc_t osc;
    vfilter_t filter;
    adsr_t env;                /* the key: a fast fade in, the release */
    float sub_phase;           /* the subharmonic's phase (half the pitch), cycles */
    float t, len, gap;         /* time in the current syllable, its length and the silence after it, s */
    float len_base;            /* nominal length of this syllable (before the random change), s */
    float rise, fall, offset;  /* this syllable's contour (semitones) and its random pitch offset */
    int syllable, calling;     /* syllables started in this call; 0 once the call is over */
    int calls_n;               /* syllables of this call */
    float amp_prev, jitter, jitter_target;
    float vel, gain_prev, pan_l, pan_r;
    uint32_t rng;
    int note;
} gull_voice_t;

typedef struct {
    float sr;
    float p[GULL_P_COUNT];
    float bend;
    int env_dirty;
    uint32_t rng, nan_count;
    vpoly_t poly;
    gull_voice_t v[GULL_VOICES];
} gull_engine_t;

#endif /* ENGINE_GULL_H */
