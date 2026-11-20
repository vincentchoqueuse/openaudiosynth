/* adsr.c — exponential ADSR (see adsr.h).
 * One-pole recurrence: level = level*coef + base, with base = target*(1 - coef).
 * To go from 0 to 1 in T with target 1 + r: coef = 2^(-log2((1+r)/r) / (T*SR)). */
#include "adsr.h"
#include "../core/fastmath.h"

#define RATIO_A 0.3f                /* attack: target 1.3 -> concave, RC-like curve */
#define RATIO_A_LOG2 2.11547722f    /* log2(1.3/0.3) */
#define RATIO_DR 0.0001f            /* decay/release: almost purely exponential */
#define RATIO_DR_LOG2 13.2878566f   /* log2(1.0001/0.0001) */

void adsr_init(adsr_t *e, float sample_rate)
{
    e->sr = sample_rate;
    adsr_set(e, 0.005f, 0.2f, 0.7f, 0.3f);
    adsr_reset(e);
}

void adsr_reset(adsr_t *e)
{
    e->level = 0.0f;
    e->stage = ADSR_IDLE;
}

static float coef_for(float t, float ratio_log2, float sr)
{
    t = dsp_clampf(t, ADSR_TIME_MIN, ADSR_TIME_MAX);
    return fm_exp2(-ratio_log2 / (t * sr));
}

void adsr_set(adsr_t *e, float attack_s, float decay_s, float sustain, float release_s)
{
    const float s = dsp_clampf(sustain, 0.0f, 1.0f);
    e->sustain = s;
    e->a_coef = coef_for(attack_s, RATIO_A_LOG2, e->sr);
    e->a_base = (1.0f + RATIO_A) * (1.0f - e->a_coef);
    e->d_coef = coef_for(decay_s, RATIO_DR_LOG2, e->sr);
    /* target S - r(1-S): the time from 1 to S equals decay_s whatever S is */
    e->d_base = (s - RATIO_DR * (1.0f - s)) * (1.0f - e->d_coef);
    e->s_base = s * (1.0f - e->d_coef); /* in sustain, follow S smoothly if it changes */
    e->r_coef = coef_for(release_s, RATIO_DR_LOG2, e->sr);
    e->r_base = -RATIO_DR * (1.0f - e->r_coef);
}

void adsr_gate(adsr_t *e, int on)
{
    if (on)
        e->stage = ADSR_ATTACK; /* from the current level */
    else if (e->stage != ADSR_IDLE)
        e->stage = ADSR_RELEASE;
}

DSP_FAST_CODE void adsr_process(adsr_t *e, float *restrict out, int n)
{
    float y = e->level;
    int st = e->stage;
    for (int i = 0; i < n; i++) {
        switch (st) {
        case ADSR_ATTACK:
            y = y * e->a_coef + e->a_base;
            if (y >= 1.0f) {
                y = 1.0f;
                st = ADSR_DECAY;
            }
            break;
        case ADSR_DECAY: {
            const float above = y > e->sustain;
            y = y * e->d_coef + e->d_base;
            if (y <= e->sustain) {
                if (above) /* crossed it: land on it. Already below (the sustain was just raised): no jump, */
                    y = e->sustain; /* the sustain stage glides up to it */
                st = ADSR_SUSTAIN;
            }
            break;
        }
        case ADSR_SUSTAIN:
            y = y * e->d_coef + e->s_base;
            break;
        case ADSR_RELEASE:
            y = y * e->r_coef + e->r_base;
            if (y <= 0.0f) {
                y = 0.0f;
                st = ADSR_IDLE;
            }
            break;
        default:
            y = 0.0f;
            break;
        }
        out[i] = y;
    }
    e->level = y;
    e->stage = st;
}
