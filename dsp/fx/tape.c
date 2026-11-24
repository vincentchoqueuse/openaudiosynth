/* tape.c — worn tape machine (see tape.h). The delay is computed once per block and interpolated per sample: wow
 * is a sine near 0.5 Hz whose rate itself wanders, flutter a sine near 7 Hz; their depths are set so that full wow
 * is a pitch deviation of about 0.5 % and full flutter 0.15 %. */
#include "tape.h"
#include "../core/fastmath.h"

#define WOW_HZ 0.55f
#define FLUTTER_HZ 7.3f
#define WOW_PITCH 0.005f      /* peak pitch deviation at wow = 1 */
#define FLUTTER_PITCH 0.0015f /* at flutter = 1 */
#define TWO_PI 6.2831853f

void tape_reset(tape_t *t)
{
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < TAPE_LEN; i++)
            t->buf[c][i] = 0.0f;
        t->lp[c] = 0.0f;
    }
    t->w = 0;
    t->wow_ph = t->flutter_ph = t->drift = 0.0f;
    t->d_prev = -1.0f;
    t->rng = dsp_seed(0x7A9Eu);
}

void tape_init(tape_t *t, float sr, float *mem)
{
    t->buf[0] = mem;
    t->buf[1] = mem + TAPE_LEN;
    t->sr = sr;
    t->inv_sr = 1.0f / sr;
    t->mix = 0.0f;
    tape_set(t, 0, 0.3f, 0.2f, 0.3f, 0.15f, 9000.0f);
    t->mix_prev = 0.0f;
    tape_reset(t);
}

void tape_set(tape_t *t, int on, float wow, float flutter, float drive, float hiss, float tone_hz)
{
    t->mix = on ? 1.0f : 0.0f;
    /* depth of the delay modulation, in samples, for the wanted pitch deviation: A = dev * sr / (2 pi f) */
    t->wow = dsp_clampf(wow, 0.0f, 1.0f) * WOW_PITCH * t->sr * (1.0f / (TWO_PI * WOW_HZ));
    t->flutter = dsp_clampf(flutter, 0.0f, 1.0f) * FLUTTER_PITCH * t->sr * (1.0f / (TWO_PI * FLUTTER_HZ));
    /* drive: 0 .. 18 dB into the tanh, the output brought back to the level of the input */
    t->gain = fm_db2lin(18.0f * dsp_clampf(drive, 0.0f, 1.0f));
    t->norm = 0.25f / fm_tanh(0.25f * t->gain); /* a peak of 0.25 (a usual mix) comes out at 0.25 whatever the drive */
    t->hiss = 0.004f * dsp_clampf(hiss, 0.0f, 1.0f) * dsp_clampf(hiss, 0.0f, 1.0f); /* -48 dBFS at most */
    const float w = TWO_PI * dsp_clampf(tone_hz, 200.0f, 0.45f * t->sr) * t->inv_sr;
    t->lp_k = w / (1.0f + w);
}

DSP_FAST_CODE void tape_process(tape_t *t, float *L, float *R, int n)
{
    const float m0 = t->mix_prev, m1 = t->mix;
    if (m0 <= 0.0f && m1 <= 0.0f)
        return;
    if (m0 <= 0.0f) /* starting from off: an empty transport, nothing stale read during the fade in */
        tape_reset(t);
    t->mix_prev = m1;
    /* the transport: wow whose rate wanders by a quarter, flutter, around a delay that leaves room for both */
    const float dt = (float)n * t->inv_sr;
    t->drift += (dsp_rand_bipolar(&t->rng) - t->drift) * 0.02f;
    t->wow_ph = fm_frac(t->wow_ph + WOW_HZ * (1.0f + 0.25f * t->drift) * dt);
    t->flutter_ph = fm_frac(t->flutter_ph + FLUTTER_HZ * dt);
    const float base = 4.0f + t->wow + t->flutter;
    const float d1 = dsp_clampf(base + t->wow * fm_sin(t->wow_ph) + t->flutter * fm_sin(t->flutter_ph), 2.0f,
                                (float)(TAPE_LEN - 4));
    float d = t->d_prev < 0.0f ? d1 : t->d_prev;
    const float inv_n = 1.0f / (float)n, dd = (d1 - d) * inv_n, dm = (m1 - m0) * inv_n;
    t->d_prev = d1;
    float m = m0;
    const float g = t->gain, norm = t->norm, k = t->lp_k, hiss = t->hiss;
    float *ch[2] = {L, R};
    for (int i = 0; i < n; i++) {
        d += dd;
        m += dm;
        const int di = (int)d;
        const float fr = d - (float)di;
        const int r0 = (t->w - di) & (TAPE_LEN - 1), r1 = (r0 - 1) & (TAPE_LEN - 1);
        for (int c = 0; c < 2; c++) {
            const float x = ch[c][i];
            t->buf[c][t->w] = x;
            float y = t->buf[c][r0] + (t->buf[c][r1] - t->buf[c][r0]) * fr;
            y = fm_tanh(y * g) * norm;
            t->lp[c] += (y - t->lp[c]) * k;
            y = t->lp[c] + hiss * dsp_rand_bipolar(&t->rng);
            ch[c][i] = x + (y - x) * m;
        }
        t->w = (t->w + 1) & (TAPE_LEN - 1);
    }
}
