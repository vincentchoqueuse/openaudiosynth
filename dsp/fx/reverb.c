/* reverb.c — the reverb's four types (see reverb.h). Tank delay lengths and output taps are those of Dattorro's
 * Table 1 / Table 2, given at 29761 Hz and rescaled by the type's size; the plate is the paper's. Allpass: w = x - g*d,
 * y = d + g*w. The spring's allpasses are first order: y = a x + z, z = x - a y. */
#include "reverb.h"
#include "../core/fastmath.h"

#define REF_SR 29761.0f

/* Paper delay lengths (samples at 29761 Hz) */
static const int in_ap_len[4] = {142, 107, 379, 277};
static const int tank_ap1_len[2] = {672, 908};   /* modulated, decay diffusion 1 */
static const int tank_d1_len[2] = {4453, 4217};
static const int tank_ap2_len[2] = {1800, 2656}; /* decay diffusion 2 */
static const int tank_d2_len[2] = {3720, 3163};
#define EXCURSION 16 /* peak modulation excursion of tank_ap1, samples at 29761 Hz */

static inline int imin(int a, int b) { return a < b ? a : b; }
static int scaled(int len, float k) { return (int)((float)len * k + 0.5f) + 1; }

/* the types: tank size (x the paper's), input diffusion, decay diffusion, input bandwidth, early reflections,
 * modulation depth (x the paper's) */
static const struct {
    float size, in_g01, in_g23, dd1, dd2, bw, er, exc;
} TYPES[REVERB_TYPES] = {
    [REVERB_PLATE] = {0.0f /* the size given at init */, 0.75f, 0.625f, 0.70f, 0.50f, 0.9995f, 0.0f, 1.0f},
    [REVERB_ROOM] = {0.5f, 0.75f, 0.70f, 0.55f, 0.45f, 0.65f, 0.55f, 0.4f},
    [REVERB_HALL] = {2.0f, 0.70f, 0.60f, 0.75f, 0.60f, 0.80f, 0.30f, 1.5f},
    [REVERB_SPRING] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f},
};
/* early reflections at the paper's plate size, seconds (left, right): scaled with the size */
static const float er_s[2][REVERB_ER] = {{0.0043f, 0.0137f, 0.0215f, 0.0298f, 0.0377f, 0.0461f},
                                        {0.0071f, 0.0119f, 0.0251f, 0.0323f, 0.0409f, 0.0488f}};
static const float spr_s[2] = {0.0371f, 0.0433f}; /* the two springs' loops at size x1 */
#define SPRING_A 0.72f                            /* allpass coefficient: the chirp's depth */
#define MIN_KS 0.25f
#define ER_MAX_S (0.0488f * REVERB_MAX_SIZE + 0.002f) /* the latest reflection at the largest size */

size_t reverb_mem_floats(float sr, float max_size)
{
    const float k = sr / REF_SR, ks = k * dsp_clampf(max_size, 0.5f, REVERB_MAX_SIZE);
    size_t n = (size_t)((REVERB_MAX_PREDELAY_S + ER_MAX_S) * sr) + 2;
    for (int i = 0; i < 4; i++)
        n += (size_t)scaled(in_ap_len[i], k);
    for (int i = 0; i < 2; i++)
        n += (size_t)(scaled(tank_ap1_len[i] + EXCURSION + 2, ks) + scaled(tank_d1_len[i], ks) +
                      scaled(tank_ap2_len[i], ks) + scaled(tank_d2_len[i], ks));
    n += 2 * ((size_t)(REVERB_SPRING_MAX_S * sr) + 2);
    return n;
}

static float *line_init(reverb_line_t *l, float *mem, int len)
{
    l->buf = mem;
    l->len = len;
    l->pos = 0;
    return mem + len;
}

/* the lines' lengths for the present scale r->ks (within what was allocated) */
static void lengths(reverb_t *r)
{
    const float ks = r->ks, rel = ks * REF_SR / r->sr; /* x the paper's plate */
    for (int i = 0; i < 2; i++) {
        r->len_ap1[i] = imin(scaled(tank_ap1_len[i] + EXCURSION + 2, ks), r->tank_ap1[i].len);
        r->len_d1[i] = imin(scaled(tank_d1_len[i], ks), r->tank_d1[i].len);
        r->len_ap2[i] = imin(scaled(tank_ap2_len[i], ks), r->tank_ap2[i].len);
        r->len_d2[i] = imin(scaled(tank_d2_len[i], ks), r->tank_d2[i].len);
        for (int k = 0; k < REVERB_ER; k++)
            r->er_d[i][k] = imin((int)(er_s[i][k] * rel * r->sr) + 1, r->pre.len - 1);
        r->spr_len[i] = dsp_clampi((int)(spr_s[i] * r->size_mult * r->sr), 2, r->spr[i].len - 1);
    }
    r->excursion = (float)EXCURSION * ks * TYPES[r->type].exc;
}

void reverb_set_type(reverb_t *r, int type, float size)
{
    r->type = dsp_clampi(type, 0, REVERB_TYPES - 1);
    r->size_mult = fm_exp2(2.0f * (dsp_clampf(size, 0.0f, 1.0f) - 0.5f)); /* x0.5 .. x2 */
    const float k = r->sr / REF_SR;
    const float base = r->type == REVERB_PLATE ? r->plate_ks : k * TYPES[r->type].size;
    r->ks_target = dsp_clampf(base * r->size_mult, k * MIN_KS, r->max_ks);
    r->in_g[0] = r->in_g[1] = TYPES[r->type].in_g01;
    r->in_g[2] = r->in_g[3] = TYPES[r->type].in_g23;
    r->dd1 = TYPES[r->type].dd1, r->dd2 = TYPES[r->type].dd2;
    r->bandwidth = TYPES[r->type].bw;
    r->er_gain = TYPES[r->type].er;
    if (r->type == REVERB_SPRING)
        lengths(r); /* the springs' lengths follow the size at once (no tank to glide) */
}

void reverb_set_tone(reverb_t *r, float lowcut_hz, float width)
{
    r->lowcut = lowcut_hz > 20.5f;
    r->lc_coef = 1.0f - fm_exp(-FM_TWO_PI * dsp_clampf(lowcut_hz, 20.0f, 1000.0f) / r->sr);
    r->width = dsp_clampf(width, 0.0f, 1.0f);
}

void reverb_init(reverb_t *r, float sr, float size, float *mem) { reverb_init_max(r, sr, size, size, mem); }

void reverb_init_max(reverb_t *r, float sr, float size, float max_size, float *mem)
{
    const float k = sr / REF_SR, ks = k * dsp_clampf(max_size, 0.5f, REVERB_MAX_SIZE);
    r->sr = sr;
    mem = line_init(&r->pre, mem, (int)((REVERB_MAX_PREDELAY_S + ER_MAX_S) * sr) + 2); /* the reflections read past the predelay */
    for (int i = 0; i < 4; i++)
        mem = line_init(&r->in_ap[i], mem, scaled(in_ap_len[i], k));
    for (int i = 0; i < 2; i++) {
        mem = line_init(&r->tank_ap1[i], mem, scaled(tank_ap1_len[i] + EXCURSION + 2, ks));
        mem = line_init(&r->tank_d1[i], mem, scaled(tank_d1_len[i], ks));
        mem = line_init(&r->tank_ap2[i], mem, scaled(tank_ap2_len[i], ks));
        mem = line_init(&r->tank_d2[i], mem, scaled(tank_d2_len[i], ks));
    }
    for (int i = 0; i < 2; i++)
        mem = line_init(&r->spr[i], mem, (int)(REVERB_SPRING_MAX_S * sr) + 2);
    r->max_ks = ks;
    r->plate_ks = dsp_minf(k * dsp_clampf(size, 0.5f, REVERB_MAX_SIZE), ks);
    r->width = 1.0f;
    r->lowcut = 0;
    r->lc_coef = 0.0f;
    reverb_set_type(r, REVERB_PLATE, 0.5f);
    r->ks = r->ks_target;
    lengths(r);
    r->lfo_inc = 0.8f / sr; /* slow, ~1 Hz as in the paper */
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
                              &r->tank_ap2[0], &r->tank_ap2[1], &r->tank_d2[0], &r->tank_d2[1], &r->spr[0], &r->spr[1]};
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        for (int j = 0; j < lines[i]->len; j++)
            lines[i]->buf[j] = 0.0f;
        lines[i]->pos = 0;
    }
    r->bw_state = r->damp_state[0] = r->damp_state[1] = r->fb[0] = r->fb[1] = 0.0f;
    r->lfo_phase = 0.0f;
    r->lc_state = 0.0f;
    for (int h = 0; h < 2; h++) {
        r->spr_lp[h] = r->spr_fb[h] = 0.0f;
        for (int i = 0; i < REVERB_SPRING_STAGES; i++)
            r->spr_z[h][i] = 0.0f;
    }
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
static inline int otap_delay(int len, int ref_pos, float k)
{
    int d = (int)((float)ref_pos * k) + 1;
    if (d >= len)
        d = len - 1;
    return d;
}

/* the spring: each side a loop of its line and a cascade of first-order allpasses (dispersion: every pass chirps),
 * damped and fed back; the input goes in after the low cut, the bandwidth filter and the predelay */
static DSP_FAST_CODE void spring_process(reverb_t *r, const float *in, float *outL, float *outR, int n)
{
    const float bw = r->bandwidth, gl = r->glide, a = SPRING_A;
    const float t_decay = r->freeze ? 0.9999f : r->decay, t_damp = r->freeze ? 1.0f : r->damping;
    const float t_in = r->freeze ? 0.0f : 1.0f;
    float cur_decay = r->cur_decay, cur_damp = r->cur_damp, cur_in = r->cur_in, bw_state = r->bw_state, lc = r->lc_state;
    const int predelay = r->predelay, lowcut = r->lowcut;
    const float lcc = r->lc_coef;
    for (int i = 0; i < n; i++) {
        cur_decay += (t_decay - cur_decay) * gl;
        cur_damp += (t_damp - cur_damp) * gl;
        cur_in += (t_in - cur_in) * gl;
        push(&r->pre, in[i] * cur_in);
        float x = predelay > 0 ? tap(&r->pre, predelay) : in[i] * cur_in;
        if (lowcut) {
            lc += (x - lc) * lcc;
            x -= lc;
        }
        bw_state += (x - bw_state) * bw;
        x = bw_state;
        const float g = 0.35f + 0.6f * cur_decay; /* loop gain: the tail's length */
        float out[2];
        for (int h = 0; h < 2; h++) {
            float y = tap(&r->spr[h], r->spr_len[h]);
            float *z = r->spr_z[h];
            for (int s = 0; s < REVERB_SPRING_STAGES; s++) {
                const float o = a * y + z[s];
                z[s] = y - a * o;
                y = o;
            }
            r->spr_lp[h] += (y - r->spr_lp[h]) * cur_damp;
            out[h] = y;
            push(&r->spr[h], x + r->spr_lp[h] * g);
        }
        outL[i] = out[0];
        outR[i] = out[1];
    }
    r->cur_decay = cur_decay, r->cur_damp = cur_damp, r->cur_in = cur_in, r->bw_state = bw_state, r->lc_state = lc;
}

/* the tank's scale glides to the wanted one (about 0.15 s): the delays slide, as a tape's would */
static void glide_size(reverb_t *r, int n)
{
    if (r->ks == r->ks_target)
        return;
    const float d = r->ks_target - r->ks;
    r->ks = dsp_absf(d) < 1e-4f * r->ks_target ? r->ks_target : r->ks + d * dsp_minf(1.0f, (float)n / (0.15f * r->sr));
    lengths(r);
}

DSP_FAST_CODE void reverb_process(reverb_t *r, const float *in, float *outL, float *outR, int n)
{
    if (r->type == REVERB_SPRING)
        spring_process(r, in, outL, outR, n);
    else {
        glide_size(r, n);
        const float k = (float)(r->len_d1[0] - 1) / (float)tank_d1_len[0]; /* actual scale */
        const float bw = r->bandwidth, gl = r->glide;
        /* frozen: tank gain ~1 (-60 dB after ~20 min), no damping, input closed */
        const float t_decay = r->freeze ? 0.9999f : r->decay;
        const float t_damp = r->freeze ? 1.0f : r->damping;
        const float t_in = r->freeze ? 0.0f : 1.0f;
        const float dd1 = r->dd1, dd2 = r->dd2;
        /* output taps (paper, Table 2); [0] = left half (24..39), [1] = right half (48..63): their delays do not
         * change within the call, computed once here rather than for every sample */
        const reverb_line_t *d1L = &r->tank_d1[0], *apL = &r->tank_ap2[0], *d2L = &r->tank_d2[0];
        const reverb_line_t *d1R = &r->tank_d1[1], *apR = &r->tank_ap2[1], *d2R = &r->tank_d2[1];
        const int l0 = otap_delay(r->len_d1[1], 266, k), l1 = otap_delay(r->len_d1[1], 2974, k),
                  l2 = otap_delay(r->len_ap2[1], 1913, k), l3 = otap_delay(r->len_d2[1], 1996, k),
                  l4 = otap_delay(r->len_d1[0], 1990, k), l5 = otap_delay(r->len_ap2[0], 187, k),
                  l6 = otap_delay(r->len_d2[0], 1066, k);
        const int r0 = otap_delay(r->len_d1[0], 353, k), r1 = otap_delay(r->len_d1[0], 3627, k),
                  r2 = otap_delay(r->len_ap2[0], 1228, k), r3 = otap_delay(r->len_d2[0], 2673, k),
                  r4 = otap_delay(r->len_d1[1], 2111, k), r5 = otap_delay(r->len_ap2[1], 335, k),
                  r6 = otap_delay(r->len_d2[1], 121, k);
        /* the state each sample reads and writes, in locals for the block (written back after it) */
        float cur_decay = r->cur_decay, cur_damp = r->cur_damp, cur_in = r->cur_in, bw_state = r->bw_state, lc = r->lc_state;
        float lfo_phase = r->lfo_phase, damp_state[2] = {r->damp_state[0], r->damp_state[1]}, fb[2] = {r->fb[0], r->fb[1]};
        const float excursion = r->excursion, lfo_inc = r->lfo_inc, er = r->er_gain, lcc = r->lc_coef;
        const int predelay = r->predelay, lowcut = r->lowcut;
        const int ap1_len[2] = {r->len_ap1[0], r->len_ap1[1]}, d1_len[2] = {r->len_d1[0], r->len_d1[1]};
        const int ap2_len[2] = {r->len_ap2[0], r->len_ap2[1]}, d2_len[2] = {r->len_d2[0], r->len_d2[1]};
        const float in_g[4] = {r->in_g[0], r->in_g[1], r->in_g[2], r->in_g[3]};
        int erd[2][REVERB_ER]; /* the reflections' delays after this block's predelay, inside the line */
        for (int h = 0; h < 2; h++)
            for (int e = 0; e < REVERB_ER; e++)
                erd[h][e] = imin(predelay + r->er_d[h][e], r->pre.len - 1);
        for (int i = 0; i < n; i++) {
            cur_decay += (t_decay - cur_decay) * gl;
            cur_damp += (t_damp - cur_damp) * gl;
            cur_in += (t_in - cur_in) * gl;
            const float decay = cur_decay, damp = cur_damp;
            /* predelay + low cut + bandwidth filter + input diffusion */
            push(&r->pre, in[i] * cur_in);
            float x = predelay > 0 ? tap(&r->pre, predelay) : in[i] * cur_in;
            if (lowcut) {
                lc += (x - lc) * lcc;
                x -= lc;
            }
            bw_state += (x - bw_state) * bw;
            x = bw_state;
            for (int a = 0; a < 4; a++)
                x = allpass(&r->in_ap[a], x, in_g[a], r->in_ap[a].len - 1);

            /* tank modulation: the two halves move in quadrature */
            lfo_phase += lfo_inc;
            if (lfo_phase >= 1.0f)
                lfo_phase -= 1.0f;
            const float m0 = excursion * (1.0f + fm_sin(lfo_phase)) * 0.5f;
            const float m1 = excursion * (1.0f + fm_cos(lfo_phase)) * 0.5f;
            const float mod[2] = {m0, m1};

            float out_half[2];
            for (int h = 0; h < 2; h++) {
                float t = x + fb[h];
                const float base = (float)(ap1_len[h] - 3) - excursion;
                /* decay diffusion 1 has the opposite sign in the paper */
                t = allpass_frac(&r->tank_ap1[h], t, -dd1, base + mod[h]);
                push(&r->tank_d1[h], t);
                t = tap(&r->tank_d1[h], d1_len[h] - 1);
                damp_state[h] += (t - damp_state[h]) * damp;
                t = damp_state[h] * decay;
                t = allpass(&r->tank_ap2[h], t, dd2, ap2_len[h] - 1);
                push(&r->tank_d2[h], t);
                out_half[h] = tap(&r->tank_d2[h], d2_len[h] - 1) * decay;
            }
            /* figure of eight: each half feeds the other */
            fb[1] = out_half[0];
            fb[0] = out_half[1];

            float yl = 0.6f * (tap(d1R, l0) + tap(d1R, l1) - tap(apR, l2) + tap(d2R, l3) - tap(d1L, l4) -
                               tap(apL, l5) - tap(d2L, l6));
            float yr = 0.6f * (tap(d1L, r0) + tap(d1L, r1) - tap(apL, r2) + tap(d2L, r3) - tap(d1R, r4) -
                               tap(apR, r5) - tap(d2R, r6));
            if (er > 0.0f) { /* early reflections: taps of the input after the predelay, fading, alternating */
                float el = 0.0f, eright = 0.0f, gk = er;
                for (int e = 0; e < REVERB_ER; e++, gk *= -0.82f) {
                    el += gk * tap(&r->pre, erd[0][e]);
                    eright += gk * tap(&r->pre, erd[1][e]);
                }
                yl += el, yr += eright;
            }
            outL[i] = yl;
            outR[i] = yr;
        }
        r->cur_decay = cur_decay, r->cur_damp = cur_damp, r->cur_in = cur_in, r->bw_state = bw_state, r->lfo_phase = lfo_phase;
        r->lc_state = lc;
        r->damp_state[0] = damp_state[0], r->damp_state[1] = damp_state[1], r->fb[0] = fb[0], r->fb[1] = fb[1];
    }
    if (r->width < 0.999f) /* narrower: the side shrinks */
        for (int i = 0; i < n; i++) {
            const float m = 0.5f * (outL[i] + outR[i]), s = 0.5f * (outL[i] - outR[i]) * r->width;
            outL[i] = m + s;
            outR[i] = m - s;
        }
}
