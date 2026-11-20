/* reverb.c — Dattorro plate reverb (see reverb.h). Delay lengths and output taps are those of the
 * paper's Table 1 / Table 2, given at 29761 Hz and rescaled. Allpass: w = x - g*d, y = d + g*w. */
#include "reverb.h"
#include "../core/fastmath.h"

#define REF_SR 29761.0f

/* Paper delay lengths (samples at 29761 Hz) */
static const int in_ap_len[4] = {142, 107, 379, 277};
static const float in_ap_g[4] = {0.75f, 0.75f, 0.625f, 0.625f};
static const int tank_ap1_len[2] = {672, 908};   /* modulated, decay diffusion 1 */
static const int tank_d1_len[2] = {4453, 4217};
static const int tank_ap2_len[2] = {1800, 2656}; /* decay diffusion 2 */
static const int tank_d2_len[2] = {3720, 3163};
#define EXCURSION 16 /* peak modulation excursion of tank_ap1, samples at 29761 Hz */

static int scaled(int len, float k) { return (int)((float)len * k + 0.5f) + 1; }

size_t reverb_mem_floats(float sr, float max_size)
{
    const float k = sr / REF_SR, ks = k * dsp_clampf(max_size, 0.5f, REVERB_MAX_SIZE);
    size_t n = (size_t)(REVERB_MAX_PREDELAY_S * sr) + 2;
    for (int i = 0; i < 4; i++)
        n += (size_t)scaled(in_ap_len[i], k);
    for (int i = 0; i < 2; i++)
        n += (size_t)(scaled(tank_ap1_len[i] + EXCURSION + 2, ks) + scaled(tank_d1_len[i], ks) +
                      scaled(tank_ap2_len[i], ks) + scaled(tank_d2_len[i], ks));
    return n;
}

static float *line_init(reverb_line_t *l, float *mem, int len)
{
    l->buf = mem;
    l->len = len;
    l->pos = 0;
    return mem + len;
}

void reverb_init(reverb_t *r, float sr, float size, float *mem)
{
    const float k = sr / REF_SR, ks = k * dsp_clampf(size, 0.5f, REVERB_MAX_SIZE);
    r->sr = sr;
    mem = line_init(&r->pre, mem, (int)(REVERB_MAX_PREDELAY_S * sr) + 2);
    for (int i = 0; i < 4; i++)
        mem = line_init(&r->in_ap[i], mem, scaled(in_ap_len[i], k));
    for (int i = 0; i < 2; i++) {
        mem = line_init(&r->tank_ap1[i], mem, scaled(tank_ap1_len[i] + EXCURSION + 2, ks));
        mem = line_init(&r->tank_d1[i], mem, scaled(tank_d1_len[i], ks));
        mem = line_init(&r->tank_ap2[i], mem, scaled(tank_ap2_len[i], ks));
        mem = line_init(&r->tank_d2[i], mem, scaled(tank_d2_len[i], ks));
    }
    r->excursion = (float)EXCURSION * ks;
    r->lfo_inc = 0.8f / sr; /* slow, ~1 Hz as in the paper */
    r->bandwidth = 0.9995f;
    r->freeze = 0;
    r->glide = 1.0f - fm_exp2(-FM_LOG2E / (0.05f * sr));
    reverb_set(r, 0.5f, 6000.0f, 0.02f);
    r->cur_decay = r->decay;
    r->cur_damp = r->damping;
    r->cur_in = 1.0f;
    reverb_reset(r);
}

void reverb_reset(reverb_t *r)
{
    reverb_line_t *lines[] = {&r->pre, &r->in_ap[0], &r->in_ap[1], &r->in_ap[2], &r->in_ap[3],
                              &r->tank_ap1[0], &r->tank_ap1[1], &r->tank_d1[0], &r->tank_d1[1],
                              &r->tank_ap2[0], &r->tank_ap2[1], &r->tank_d2[0], &r->tank_d2[1]};
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        for (int j = 0; j < lines[i]->len; j++)
            lines[i]->buf[j] = 0.0f;
        lines[i]->pos = 0;
    }
    r->bw_state = r->damp_state[0] = r->damp_state[1] = r->fb[0] = r->fb[1] = 0.0f;
    r->lfo_phase = 0.0f;
}

void reverb_set(reverb_t *r, float decay, float damping_hz, float predelay_s)
{
    r->decay = dsp_clampf(decay, 0.0f, 0.99f);
    /* one-pole low-pass coefficient for the tank damping: y += (x - y)·c, c = 1 - exp(-2 pi·fc/SR) */
    r->damping = 1.0f - fm_exp(-FM_TWO_PI * dsp_clampf(damping_hz, 100.0f, 0.45f * r->sr) / r->sr);
    r->predelay = (int)(dsp_clampf(predelay_s, 0.0f, REVERB_MAX_PREDELAY_S) * r->sr);
}

void reverb_set_freeze(reverb_t *r, int on) { r->freeze = on ? 1 : 0; }

/* Value written d samples ago (d >= 1). */
static inline float tap(const reverb_line_t *l, int d)
{
    int i = l->pos - d;
    if (i < 0)
        i += l->len;
    return l->buf[i];
}

static inline void push(reverb_line_t *l, float x)
{
    l->buf[l->pos] = x;
    if (++l->pos >= l->len)
        l->pos = 0;
}

static inline float allpass(reverb_line_t *l, float x, float g, int d)
{
    const float del = tap(l, d);
    const float w = x - g * del;
    push(l, w);
    return del + g * w;
}

/* Allpass with a fractional (modulated) delay, linear interpolation. */
static inline float allpass_frac(reverb_line_t *l, float x, float g, float d)
{
    const int di = (int)d;
    const float fr = d - (float)di;
    const float a = tap(l, di), b = tap(l, di + 1);
    const float del = a + (b - a) * fr;
    const float w = x - g * del;
    push(l, w);
    return del + g * w;
}

/* Delay of an output tap whose position is given in samples at 29761 Hz (constant within a call). */
static inline int otap_delay(const reverb_line_t *l, int ref_pos, float k)
{
    int d = (int)((float)ref_pos * k) + 1;
    if (d >= l->len)
        d = l->len - 1;
    return d;
}

DSP_FAST_CODE void reverb_process(reverb_t *r, const float *in, float *outL, float *outR, int n)
{
    const float k = (float)(r->tank_d1[0].len - 1) / (float)tank_d1_len[0]; /* actual scale */
    const float bw = r->bandwidth, gl = r->glide;
    /* frozen: tank gain ~1 (-60 dB after ~20 min), no damping, input closed */
    const float t_decay = r->freeze ? 0.9999f : r->decay;
    const float t_damp = r->freeze ? 1.0f : r->damping;
    const float t_in = r->freeze ? 0.0f : 1.0f;
    const float dd1 = 0.70f, dd2 = 0.50f;
    /* output taps (paper, Table 2); [0] = left half (24..39), [1] = right half (48..63): their delays do not
     * change within the call, computed once here rather than for every sample */
    const reverb_line_t *d1L = &r->tank_d1[0], *apL = &r->tank_ap2[0], *d2L = &r->tank_d2[0];
    const reverb_line_t *d1R = &r->tank_d1[1], *apR = &r->tank_ap2[1], *d2R = &r->tank_d2[1];
    const int l0 = otap_delay(d1R, 266, k), l1 = otap_delay(d1R, 2974, k), l2 = otap_delay(apR, 1913, k),
              l3 = otap_delay(d2R, 1996, k), l4 = otap_delay(d1L, 1990, k), l5 = otap_delay(apL, 187, k),
              l6 = otap_delay(d2L, 1066, k);
    const int r0 = otap_delay(d1L, 353, k), r1 = otap_delay(d1L, 3627, k), r2 = otap_delay(apL, 1228, k),
              r3 = otap_delay(d2L, 2673, k), r4 = otap_delay(d1R, 2111, k), r5 = otap_delay(apR, 335, k),
              r6 = otap_delay(d2R, 121, k);
    for (int i = 0; i < n; i++) {
        r->cur_decay += (t_decay - r->cur_decay) * gl;
        r->cur_damp += (t_damp - r->cur_damp) * gl;
        r->cur_in += (t_in - r->cur_in) * gl;
        const float decay = r->cur_decay, damp = r->cur_damp;
        /* predelay + bandwidth filter + input diffusion */
        push(&r->pre, in[i] * r->cur_in);
        float x = r->predelay > 0 ? tap(&r->pre, r->predelay) : in[i] * r->cur_in;
        r->bw_state += (x - r->bw_state) * bw;
        x = r->bw_state;
        for (int a = 0; a < 4; a++)
            x = allpass(&r->in_ap[a], x, in_ap_g[a], r->in_ap[a].len - 1);

        /* tank modulation: the two halves move in quadrature */
        r->lfo_phase += r->lfo_inc;
        if (r->lfo_phase >= 1.0f)
            r->lfo_phase -= 1.0f;
        const float m0 = r->excursion * (1.0f + fm_sin(r->lfo_phase)) * 0.5f;
        const float m1 = r->excursion * (1.0f + fm_cos(r->lfo_phase)) * 0.5f;
        const float mod[2] = {m0, m1};

        float out_half[2];
        for (int h = 0; h < 2; h++) {
            float t = x + r->fb[h];
            const float base = (float)(r->tank_ap1[h].len - 3) - r->excursion;
            /* decay diffusion 1 has the opposite sign in the paper */
            t = allpass_frac(&r->tank_ap1[h], t, -dd1, base + mod[h]);
            push(&r->tank_d1[h], t);
            t = tap(&r->tank_d1[h], r->tank_d1[h].len - 1);
            r->damp_state[h] += (t - r->damp_state[h]) * damp;
            t = r->damp_state[h] * decay;
            t = allpass(&r->tank_ap2[h], t, dd2, r->tank_ap2[h].len - 1);
            push(&r->tank_d2[h], t);
            out_half[h] = tap(&r->tank_d2[h], r->tank_d2[h].len - 1) * decay;
        }
        /* figure of eight: each half feeds the other */
        r->fb[1] = out_half[0];
        r->fb[0] = out_half[1];

        outL[i] = 0.6f * (tap(d1R, l0) + tap(d1R, l1) - tap(apR, l2) + tap(d2R, l3) - tap(d1L, l4) -
                          tap(apL, l5) - tap(d2L, l6));
        outR[i] = 0.6f * (tap(d1L, r0) + tap(d1L, r1) - tap(apL, r2) + tap(d2L, r3) - tap(d1R, r4) -
                          tap(apR, r5) - tap(d2R, r6));
    }
}
