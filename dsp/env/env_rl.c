/* env_rl.c — rate/level envelope (see env_rl.h). The rate curve was chosen by ear for the usual decay
 * ranges (electric pianos ~ rates 25-35), not measured on the original hardware. */
#include "env_rl.h"
#include "../core/fastmath.h"

float env_rl_time(float rate) { return 60.0f * fm_exp2(-dsp_clampf(rate, 0.0f, 99.0f) * (1.0f / 7.3f)); }

void env_rl_step(env_rl_t *e, const float rates[4], const float levels[4], float rate_add, float full_scale, float dt)
{
    if (e->stage > 3)
        return;
    const float target = levels[e->stage];
    const float slope = full_scale / env_rl_time(rates[e->stage] + rate_add) * dt;
    if (e->level < target)
        e->level = dsp_minf(target, e->level + slope);
    else
        e->level = dsp_maxf(target, e->level - slope);
    if (e->level == target) {
        if (e->stage < 2)
            e->stage++;
        else if (e->stage == 3)
            e->stage = ENV_RL_IDLE; /* release finished; stage 2 holds until key-off */
    }
}
