/* delay.c — tape echo (see delay.h).
 * Per sample: delay time glides toward its target (one-pole, ~0.25 s: changing the time bends the
 * pitch of the repeats like a tape machine), plus wow (0.4-0.7 Hz sine + slow random drift) and
 * flutter (~6.5 Hz); read with Hermite interpolation; feedback through HP (~80 Hz) + LP (tone) and
 * a normalized tanh saturator; ping-pong crosses the feedback between channels. */
#include "delay.h"
#include "../core/fastmath.h"

#define GLIDE_S 0.25f
#define WOW_MAX_S 0.0025f      /* +/-2.5 ms at wow = 1 */
#define FLUTTER_MAX_S 0.00015f /* +/-0.15 ms at wow = 1 */
#define HP_HZ 80.0f

size_t delay_mem_floats(float sr, float max_time_s)
{
    return 2u * (size_t)((max_time_s + WOW_MAX_S * 2.0f) * sr + 8.0f);
}

void delay_init(delay_t *d, float sr, float max_time_s, float *mem)
{
    d->sr = sr;
    d->inv_sr = 1.0f / sr;
    d->len = (int)((max_time_s + WOW_MAX_S * 2.0f) * sr + 8.0f);
    d->buf[0] = mem;
    d->buf[1] = mem + d->len;
    d->glide = 1.0f - fm_exp2(-FM_LOG2E / (GLIDE_S * sr));
    d->hp_coef = 1.0f - fm_exp(-FM_TWO_PI * HP_HZ * d->inv_sr);
    d->wow_inc = 0.55f * d->inv_sr;
    d->flutter_inc = 6.5f * d->inv_sr;
    d->rng = dsp_seed(0xDE1A7u);
    delay_set(d, 0.375f, 1.0f, 0.35f, 4000.0f, 0.2f, 0.2f, 0);
    delay_snap(d);
    delay_reset(d);
}

void delay_reset(delay_t *d)
{
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < d->len; i++)
            d->buf[c][i] = 0.0f;
    d->pos = 0;
    d->lp[0] = d->lp[1] = d->hp[0] = d->hp[1] = 0.0f;
    d->wow_phase = d->flutter_phase = 0.0f;
    d->drift = d->drift_target = 0.0f;
}

void delay_set(delay_t *d, float time_s, float ratio_r, float feedback, float tone_hz, float drive, float wow,
               int pingpong)
{
    const float margin = (WOW_MAX_S + FLUTTER_MAX_S) * d->sr + 4.0f;
    const float dmax = (float)d->len - margin;
    d->pingpong = pingpong ? 1 : 0;
    const float r = d->pingpong ? 1.0f : dsp_clampf(ratio_r, 0.25f, 2.0f);
    d->d_target[0] = dsp_clampf(time_s * d->sr, 4.0f + margin, dmax);
    d->d_target[1] = dsp_clampf(time_s * r * d->sr, 4.0f + margin, dmax);
    d->feedback = dsp_clampf(feedback, 0.0f, 0.98f);
    d->lp_coef = 1.0f - fm_exp(-FM_TWO_PI * dsp_clampf(tone_hz, 200.0f, 0.45f * d->sr) * d->inv_sr);
    d->drive = 1.0f + 4.0f * dsp_clampf(drive, 0.0f, 1.0f);
    d->inv_drive = 1.0f / d->drive;
    wow = dsp_clampf(wow, 0.0f, 1.0f);
    d->wow_depth = wow * WOW_MAX_S * d->sr;
    d->flutter_depth = wow * FLUTTER_MAX_S * d->sr;
}

void delay_snap(delay_t *d)
{
    d->d_cur[0] = d->d_target[0];
    d->d_cur[1] = d->d_target[1];
}

/* 4-point, 3rd-order Hermite interpolation reading `delay` samples back from the write position. */
static inline float read_hermite(const float *buf, int len, int pos, float delay)
{
    const int di = (int)delay;
    const float t = delay - (float)di;
    int i0 = pos - di; /* sample at integer delay di */
    if (i0 < 0)
        i0 += len;
    const int im1 = i0 + 1 >= len ? i0 + 1 - len : i0 + 1; /* newer */
    const int i1 = i0 - 1 < 0 ? i0 - 1 + len : i0 - 1;     /* older */
    const int i2 = i1 - 1 < 0 ? i1 - 1 + len : i1 - 1;
    const float xm1 = buf[im1], x0 = buf[i0], x1 = buf[i1], x2 = buf[i2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

DSP_FAST_CODE void delay_process(delay_t *d, const float *inL, const float *inR, float *outL, float *outR, int n)
{
    const float g = d->glide, fb = d->feedback, lpc = d->lp_coef, hpc = d->hp_coef;
    const float drv = d->drive, idrv = d->inv_drive;
    /* random drift of the wow: a new target about 15 times a second, followed with a ~10 ms lag. Both are set
     * in seconds (the same at any sample rate and block size); the drift glides over the block, a step in the
     * read position would click. */
    const float block_s = (float)n * d->inv_sr;
    if (dsp_rand_unipolar(&d->rng) < 15.0f * block_s)
        d->drift_target = dsp_rand_bipolar(&d->rng);
    float drift = d->drift;
    d->drift = dsp_clampf(d->drift + (d->drift_target - d->drift) * dsp_minf(1.0f, 96.0f * block_s), -1.0f, 1.0f);
    const float drift_step = (d->drift - drift) / (float)n;

    for (int i = 0; i < n; i++) {
        d->wow_phase += d->wow_inc;
        if (d->wow_phase >= 1.0f)
            d->wow_phase -= 1.0f;
        d->flutter_phase += d->flutter_inc;
        if (d->flutter_phase >= 1.0f)
            d->flutter_phase -= 1.0f;
        drift += drift_step;
        const float wow = d->wow_depth * (0.7f * fm_sin(d->wow_phase) + 0.3f * drift);
        const float flut = d->flutter_depth * fm_sin(d->flutter_phase);

        float y[2];
        for (int c = 0; c < 2; c++) {
            d->d_cur[c] += (d->d_target[c] - d->d_cur[c]) * g;
            /* channels get slightly different wow (stereo tape heads) */
            const float m = c ? 0.8f * wow - flut : wow + flut;
            y[c] = read_hermite(d->buf[c], d->len, d->pos, d->d_cur[c] + m);
        }

        float w[2];
        for (int c = 0; c < 2; c++) {
            /* feedback path: high-pass, low-pass (tone), normalized soft saturation */
            float v = y[d->pingpong ? 1 - c : c];
            d->hp[c] += (v - d->hp[c]) * hpc;
            v -= d->hp[c];
            d->lp[c] += (v - d->lp[c]) * lpc;
            v = fm_tanh(d->lp[c] * drv) * idrv;
            w[c] = v * fb;
        }
        /* ping-pong: mono sum enters the left line only, repeats alternate L, R, L... */
        const float xin = d->pingpong ? 0.5f * (inL[i] + inR[i]) : 0.0f;
        d->buf[0][d->pos] = (d->pingpong ? xin : inL[i]) + w[0];
        d->buf[1][d->pos] = (d->pingpong ? 0.0f : inR[i]) + w[1];
        if (++d->pos >= d->len)
            d->pos = 0;
        outL[i] = y[0];
        outR[i] = y[1];
    }
}
