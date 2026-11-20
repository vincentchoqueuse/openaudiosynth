/* chorus.c — chorus/ensemble (see chorus.h). One shared delay buffer, several modulated taps read
 * with linear interpolation (delays of a few ms: the interpolation low-pass is negligible next to
 * the 9 kHz BBD-style low-pass). Delay ranges are chosen by ear around the usual 1.5-6 ms. */
#include "chorus.h"
#include "../core/fastmath.h"

static float tri(float p) { return 1.0f - 4.0f * dsp_absf(p - 0.5f); } /* -1..1 */

void chorus_init(chorus_t *c, float sr)
{
    c->sr = sr;
    c->lp_coef = 1.0f - fm_exp(-FM_TWO_PI * 9000.0f / sr);
    c->mode = CHORUS_OFF;
    c->mix = c->mix_prev = 0.0f;
    c->inc_slow = c->inc_fast = 0.0f;
    chorus_reset(c);
}

void chorus_reset(chorus_t *c)
{
    for (int i = 0; i < CHORUS_BUF; i++)
        c->buf[i] = 0.0f;
    c->pos = 0;
    c->ph_slow = 0.0f;
    c->ph_fast = 0.0f;
    c->lp[0] = c->lp[1] = c->lp[2] = 0.0f;
}

void chorus_set(chorus_t *c, chorus_mode_t mode, float mix)
{
    c->mode = (unsigned)mode < (unsigned)CHORUS_MODE_COUNT ? (int)mode : CHORUS_OFF;
    c->mix = c->mode == CHORUS_OFF ? 0.0f : dsp_clampf(mix, 0.0f, 1.0f);
    switch (c->mode) {
    case CHORUS_I: c->inc_slow = 0.5f / c->sr; break;
    case CHORUS_II: c->inc_slow = 0.85f / c->sr; break;
    case CHORUS_I_II: c->inc_slow = 0.5f / c->sr; c->inc_fast = 0.85f / c->sr; break;
    case CHORUS_ENSEMBLE: c->inc_slow = 0.6f / c->sr; c->inc_fast = 6.0f / c->sr; break;
    default: break;
    }
}

static inline float tap(const chorus_t *c, float delay)
{
    const int di = (int)delay;
    const float t = delay - (float)di;
    const float a = c->buf[(c->pos - di) & (CHORUS_BUF - 1)];
    const float b = c->buf[(c->pos - di - 1) & (CHORUS_BUF - 1)];
    return a + (b - a) * t;
}

DSP_FAST_CODE void chorus_process(chorus_t *c, const float *inL, const float *inR, float *outL, float *outR, int n)
{
    const float ms = c->sr * 0.001f;
    const float m0 = c->mix_prev, dm = (c->mix - c->mix_prev) / (float)n;
    c->mix_prev = c->mix;
    if (c->mode == CHORUS_OFF && m0 <= 0.0f) {
        for (int i = 0; i < n; i++) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }
    float mix = m0;
    for (int i = 0; i < n; i++) {
        mix += dm;
        const float x = 0.5f * (inL[i] + inR[i]);
        c->buf[c->pos & (CHORUS_BUF - 1)] = x;
        c->ph_slow += c->inc_slow;
        if (c->ph_slow >= 1.0f)
            c->ph_slow -= 1.0f;
        c->ph_fast += c->inc_fast;
        if (c->ph_fast >= 1.0f)
            c->ph_fast -= 1.0f;

        float wl, wr;
        switch (c->mode) {
        case CHORUS_ENSEMBLE: {
            float w[3];
            for (int k = 0; k < 3; k++) {
                const float off = (float)k * (1.0f / 3.0f);
                const float m = 0.8f * fm_sin(c->ph_slow + off) + 0.2f * fm_sin(c->ph_fast + off);
                c->lp[k] += (tap(c, (5.0f + 1.6f * m) * ms) - c->lp[k]) * c->lp_coef;
                w[k] = c->lp[k];
            }
            wl = 0.6f * w[0] + 0.4f * w[1];
            wr = 0.6f * w[2] + 0.4f * w[1];
            break;
        }
        default: {
            float m = tri(c->ph_slow);
            float depth = c->mode == CHORUS_II ? 1.2f : 1.85f; /* ms */
            if (c->mode == CHORUS_I_II) {
                m = 0.6f * m + 0.4f * tri(c->ph_fast);
                depth = 1.6f;
            }
            /* antiphase: L and R taps move in opposite directions around 3.5 ms */
            c->lp[0] += (tap(c, (3.5f + depth * m) * ms) - c->lp[0]) * c->lp_coef;
            c->lp[1] += (tap(c, (3.5f - depth * m) * ms) - c->lp[1]) * c->lp_coef;
            wl = c->lp[0];
            wr = c->lp[1];
            break;
        }
        }
        c->pos = (c->pos + 1) & (CHORUS_BUF - 1);
        outL[i] = inL[i] + mix * wl;
        outR[i] = inR[i] + mix * wr;
    }
}
