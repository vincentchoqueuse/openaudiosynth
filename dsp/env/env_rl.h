/* env_rl.h — 4-rate / 4-level envelope of the classic digital FM instruments: from level 4, go to levels
 * 1, 2, 3 at rates 1, 2, 3, hold level 3 until key-off, then go to level 4 at rate 4. Levels are in a
 * linear domain chosen by the caller (dB for amplitude: exponential-sounding segments; semitones for pitch);
 * rates 0..99 map to the time of a full-scale sweep (env_rl_time). Stepped once per block.
 * Reference: published documentation of the 1983 6-operator FM instrument (rate/level envelopes). */
#ifndef ENV_RL_H
#define ENV_RL_H

#include "../core/dsp.h"

#define ENV_RL_IDLE 4 /* stage after the release has reached level 4 */

typedef struct {
    float level; /* current level (caller's unit) */
    int stage;   /* 0..2 attack/decays (2 holds as sustain), 3 release, ENV_RL_IDLE */
} env_rl_t;

/* Seconds for a full-scale sweep at rate 0..99: 99 ~ 5 ms, 50 ~ 0.5 s, 30 ~ 3.5 s, 0 ~ 60 s. */
float env_rl_time(float rate);

/* Advances by dt seconds. rate_add: keyboard rate scaling added to every rate; full_scale: the sweep that
 * env_rl_time refers to (96 for dB). */
void env_rl_step(env_rl_t *e, const float rates[4], const float levels[4], float rate_add, float full_scale, float dt);

/* Key-on restarts from the current level (no click); key-off goes to the release stage. */
static inline void env_rl_gate(env_rl_t *e, int on)
{
    if (on)
        e->stage = 0;
    else if (e->stage < ENV_RL_IDLE)
        e->stage = 3;
}

#endif /* ENV_RL_H */
