/* mseg.c — multi-segment envelope (see mseg.h). Curve: y = (2^(a x) - 1) / (2^a - 1), a = -8 curve. */
#include "mseg.h"
#include "../core/fastmath.h"

#define MSEG_MIN_TIME 0.001f /* a segment lasts at least this: loops cannot spin forever */

static float shape(float x, float curve) { return mseg_curve(x, curve); }

static int count_of(const mseg_shape_t *s) { return dsp_clampi(s->count, 2, MSEG_POINTS); }

void mseg_reset(mseg_t *m, const mseg_shape_t *s)
{
    m->seg = count_of(s);
    m->pos = 0.0f;
    m->value = m->from = s->level[0];
    m->gate = 0;
}

void mseg_gate(mseg_t *m, const mseg_shape_t *s, int on)
{
    const int n = count_of(s);
    m->gate = on;
    if (on) {
        m->seg = 1; /* from the current value to point 1 */
        m->from = m->value;
        m->pos = 0.0f;
        return;
    }
    /* release: leave the sustain point or the held loop and go on after it, from the current value. A loop that
     * ends on the last point has nothing after it: its current turn simply finishes. Already past that point
     * (or nothing to leave): the segment in progress goes on untouched. */
    int after = -1;
    if (s->sustain >= 0 && s->sustain < n - 1)
        after = s->sustain;
    if (s->mode == MSEG_LOOP_HELD && s->loop_end > 0 && s->loop_end < n - 1)
        after = s->loop_end > after ? s->loop_end : after;
    if (after >= 0 && m->seg <= after + 1) {
        m->seg = after + 1;
        m->from = m->value;
        m->pos = 0.0f;
    }
}

float mseg_length(const mseg_shape_t *s)
{
    float len = 0.0f;
    for (int k = 1; k < count_of(s); k++)
        len += dsp_maxf(s->time[k], MSEG_MIN_TIME);
    return len;
}

float mseg_at(const mseg_shape_t *s, float t)
{
    const int n = count_of(s);
    for (int k = 1; k < n; k++) {
        const float len = dsp_maxf(s->time[k], MSEG_MIN_TIME);
        if (t < len)
            return s->level[k - 1] + (s->level[k] - s->level[k - 1]) * shape(dsp_maxf(t, 0.0f) / len, s->curve[k]);
        t -= len;
    }
    return s->level[n - 1];
}

float mseg_advance(mseg_t *m, const mseg_shape_t *s, float dt)
{
    const int n = count_of(s);
    const int loops = (s->mode == MSEG_LOOP_FREE || (s->mode == MSEG_LOOP_HELD && m->gate)) &&
                      s->loop_start >= 0 && s->loop_end > s->loop_start && s->loop_end < n;
    for (int guard = 0; guard < 2 * MSEG_POINTS && dt > 0.0f; guard++) {
        if (m->seg >= n)
            return m->value = s->level[n - 1];
        /* holding at the sustain point */
        if (m->gate && s->sustain >= 0 && m->seg == s->sustain + 1 && !loops) {
            m->value = s->level[s->sustain];
            return m->value;
        }
        const float len = dsp_maxf(s->time[m->seg], MSEG_MIN_TIME);
        const float left = len - m->pos;
        if (dt < left) {
            m->pos += dt;
            break;
        }
        dt -= left;
        m->from = s->level[m->seg];
        m->pos = 0.0f;
        m->seg++;
        if (loops && m->seg == s->loop_end + 1) { /* end of the loop: back to its start point */
            m->seg = s->loop_start + 1;
            m->from = s->level[s->loop_start];
        }
    }
    if (m->seg >= n)
        return m->value = s->level[n - 1];
    if (m->gate && s->sustain >= 0 && m->seg == s->sustain + 1 && !loops)
        return m->value = s->level[s->sustain];
    const float len = dsp_maxf(s->time[m->seg], MSEG_MIN_TIME);
    const float x = dsp_clampf(m->pos / len, 0.0f, 1.0f);
    m->value = m->from + (s->level[m->seg] - m->from) * shape(x, s->curve[m->seg]);
    return m->value;
}
