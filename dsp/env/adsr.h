/* adsr.h — ADSR envelope with analog-style exponential segments (RC charge/discharge).
 * Each segment aims at a target beyond its end point ("target ratio", cf. N. Redmon,
 * EarLevel Engineering): the set time is exactly that of the full segment.
 * A retrigger restarts from the current level (no click). Times clamped to 0.5 ms .. 30 s. */
#ifndef ADSR_H
#define ADSR_H

#include "../core/dsp.h"

#define ADSR_TIME_MIN 0.0005f
#define ADSR_TIME_MAX 30.0f

typedef enum { ADSR_IDLE = 0, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE } adsr_stage_t;

typedef struct {
    float level;
    int stage;
    float sr;
    float a_coef, a_base;
    float d_coef, d_base;
    float s_base, sustain;
    float r_coef, r_base;
} adsr_t;

void adsr_init(adsr_t *e, float sample_rate);
void adsr_reset(adsr_t *e); /* level to 0, IDLE state */
/* Times in seconds, sustain in [0, 1]. May be called at any time. */
void adsr_set(adsr_t *e, float attack_s, float decay_s, float sustain, float release_s);
void adsr_gate(adsr_t *e, int on);
void adsr_process(adsr_t *e, float *restrict out, int n);

static inline int adsr_active(const adsr_t *e) { return e->stage != ADSR_IDLE; }

#endif /* ADSR_H */
