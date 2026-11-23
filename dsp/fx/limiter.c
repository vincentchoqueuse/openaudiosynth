/* limiter.c — look-ahead peak limiter (see limiter.h). Per sample: the needed gain enters a sliding minimum over
 * the window (a monotonic queue, bounded by the window), then a release (instant down, exponential up), then a
 * moving average over the window; the audio is delayed by the window minus one, so the average has fully reached
 * the needed gain when the peak comes out. The average's running sum is recomputed every block (no drift). */
#include "limiter.h"
#include "../core/fastmath.h"

void limiter_init(limiter_t *l, float sr)
{
    l->sr = sr;
    l->len = dsp_clampi((int)(LIMITER_LOOKAHEAD_S * sr + 0.5f), 2, LIMITER_MAX);
    l->ceiling = l->ceil_prev = 1.0f;
    l->gain = l->gain_prev = 1.0f;
    limiter_set(l, 0.0f, 0.0f, 0.15f);
    l->gain_prev = l->gain, l->ceil_prev = l->ceiling;
    limiter_reset(l);
}

void limiter_reset(limiter_t *l)
{
    for (int i = 0; i < LIMITER_MAX; i++)
        l->dl[i] = l->dr[i] = 0.0f, l->box[i] = 1.0f;
    l->dpos = l->bpos = 0;
    l->qh = l->qn = 0;
    l->t = 0;
    l->sum = (float)l->len;
    l->env = 1.0f;
    l->reduction = 1.0f;
}

void limiter_set(limiter_t *l, float gain_db, float ceiling_db, float release_s)
{
    l->gain = fm_db2lin(dsp_clampf(gain_db, 0.0f, 24.0f));
    l->ceiling = fm_db2lin(dsp_clampf(ceiling_db, -24.0f, 0.0f));
    l->rel = 1.0f - fm_exp(-1.0f / (dsp_clampf(release_s, 0.001f, 5.0f) * l->sr));
}

DSP_FAST_CODE void limiter_process(limiter_t *l, float *L, float *R, int n)
{
    const int len = l->len, last = len - 1;
    const float g0 = l->gain_prev, dg = (l->gain - g0) / (float)n;
    const float c0 = l->ceil_prev, dc = (l->ceiling - c0) / (float)n;
    l->gain_prev = l->gain, l->ceil_prev = l->ceiling;
    const float inv_len = 1.0f / (float)len;
    float sum = 0.0f; /* exact running sum of the box, recomputed once per block */
    for (int i = 0; i < len; i++)
        sum += l->box[i];
    float lowest = 1.0f;
    for (int i = 0; i < n; i++) {
        const float g = g0 + dg * (float)(i + 1), c = c0 + dc * (float)(i + 1);
        const float xl = L[i] * g, xr = R[i] * g;
        const float peak = dsp_maxf(dsp_absf(xl), dsp_absf(xr));
        const float need = peak > c ? c / peak : 1.0f;
        /* sliding minimum over the last len samples */
        while (l->qn > 0 && l->mv[(l->qh + l->qn - 1) % LIMITER_MAX] >= need)
            l->qn--;
        const int tail = (l->qh + l->qn) % LIMITER_MAX;
        l->mv[tail] = need, l->mi[tail] = l->t;
        l->qn++;
        if (l->t - l->mi[l->qh] >= len)
            l->qh = (l->qh + 1) % LIMITER_MAX, l->qn--;
        const float held = l->mv[l->qh];
        l->t = (l->t + 1) & 0x3FFFFFFF;
        if (l->t == 0) /* the index wrapped: re-base the queue (rare) */
            for (int k = 0; k < l->qn; k++)
                l->mi[(l->qh + k) % LIMITER_MAX] -= 0x40000000;
        /* release: down at once (the box smooths it), up exponentially */
        l->env = held < l->env ? held : l->env + (held - l->env) * l->rel;
        sum += l->env - l->box[l->bpos];
        l->box[l->bpos] = l->env;
        l->bpos = l->bpos + 1 == len ? 0 : l->bpos + 1;
        const float gain = dsp_minf(sum * inv_len, 1.0f);
        lowest = dsp_minf(lowest, gain);
        /* the audio, delayed by len - 1 */
        l->dl[l->dpos] = xl, l->dr[l->dpos] = xr;
        const int rp = l->dpos >= last ? l->dpos - last : l->dpos - last + len;
        l->dpos = l->dpos + 1 == len ? 0 : l->dpos + 1;
        L[i] = l->dl[rp] * gain;
        R[i] = l->dr[rp] * gain;
    }
    l->reduction = lowest;
}
