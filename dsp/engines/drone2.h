/* drone2.h — drone field engine: one shared field of sine partials that behave. The held notes dig a landscape of
 * wells (their harmonics, stretched or odd if asked); every partial owns one well and travels to it at a glide
 * speed, so a new chord makes the partials migrate (long glissandi) instead of cross-fading. A partial leaves for a
 * clearly better free well, now and then on its own (restlessness), dies and is reborn near another (renewal),
 * moves with its neighbours (flock) and competes for room with them (an ecology: Lotka-Volterra competition whose
 * niche overlap is the distance in frequency). From silence the partials bloom out of a tight cluster.
 * References: the competitive Lotka-Volterra equations; flocking (C. Reynolds, 1987: alignment); the harmonic
 * series as the attractor of a spectrum. */
#ifndef ENGINE_DRONE2_H
#define ENGINE_DRONE2_H

#include "engine.h"

#define D2_NOTES 8     /* polyphony: the notes that dig wells */
#define D2_HARM 24     /* wells per note at most */
#define D2_WELLS (D2_NOTES * D2_HARM)
#define D2_PARTIALS 32 /* the field's partials at most */

enum {
    D2_P_PARTIALS = 0, D2_P_HARMONICS, D2_P_BRIGHTNESS, D2_P_STRETCH, D2_P_ODD,
    D2_P_TRAVEL, D2_P_TEMPERATURE, D2_P_RESTLESS, D2_P_FLOCK, D2_P_ECOLOGY, D2_P_RENEWAL, D2_P_BLOOM,
    D2_P_WIDTH, D2_P_ATTACK, D2_P_RELEASE, D2_P_VOLUME, D2_P_BEND_RANGE,
    D2_P_COUNT
};
extern const param_desc_t drone2_params[D2_P_COUNT];
extern const engine_ops_t drone2_engine;

typedef struct {
    float x, v;           /* place (log2 Hz) and speed (octaves per second) */
    float amp, amp_prev;  /* level at the end of this block and of the previous one */
    float phase, inc;     /* sine phase (cycles), increment at the end of the previous block */
    float gl, gr;         /* stereo gains at the end of the previous block */
    float pan, detune;    /* fixed place in the stereo field, fixed offset (-1..1, scaled by temperature) */
    float walk_x, walk_y; /* pitch wobble: a smoothed random walk */
    float eco;            /* abundance in the ecology (about 0..1) */
    float life;           /* seconds left before it dies (renewal) */
    int well;             /* owned well (note * D2_HARM + harmonic - 1), -1: none */
    int placed;           /* its place means something (it sounded): distances count when choosing a well */
    int dying;
} drone2_partial_t;

typedef struct {
    int gate, note;
    float vel, env, fade;
} drone2_note_t;

typedef struct {
    float sr;
    float p[D2_P_COUNT];
    float bend;
    uint32_t rng, nan_count;
    int next;                 /* round robin of the partials that look for a better well */
    float gain_prev;
    float wc[D2_WELLS], ww[D2_WELLS]; /* wells: centre (log2 Hz) and weight (0: no well) */
    int8_t owner[D2_WELLS];
    float asym[D2_PARTIALS][D2_PARTIALS]; /* the ecology's fixed asymmetry of competition */
    drone2_partial_t q[D2_PARTIALS];
    drone2_note_t n[D2_NOTES];
    vpoly_t poly;
} drone2_t;

#endif /* ENGINE_DRONE2_H */
