/* ladder.c — ZDF ladder (see ladder.h).
 * One-pole TPT stage: y = G x + (1 - G) s, s <- 2y - s, with G = g/(1+g), g = tan(pi fc / fs).
 * Cascade: y4 = G^4 u + S, S = (1-G)(G^3 s1 + G^2 s2 + G s3 + s4); feedback u = x - k y4 solved
 * without delay: u = (x - k S) d, d = 1/(1 + k G^4). The input then goes through 2 tanh(u/2): unity
 * gain for small signals, bounded for large ones (this also bounds self-oscillation).
 * G, k and d are interpolated linearly per sample over the block (no division in the loop). */
#include "ladder.h"
#include "../core/fastmath.h"


void ladder_init(ladder_t *l, float sr)
{
    l->sr = sr;
    l->comp_amount = 0.35f;
    l->mode = LADDER_LP24;
    l->oversample = 1;
    ladder_reset(l);
    ladder_set(l, 1000.0f, 0.0f, 0.0f);
    l->snap = 1;
}

void ladder_reset(ladder_t *l)
{
    for (int i = 0; i < 4; i++)
        l->s[i] = 0.0f;
    hb4x_reset(&l->hb);
    l->snap = 1;
}

void ladder_set_mode(ladder_t *l, ladder_mode_t mode)
{
    l->mode = (unsigned)mode < (unsigned)LADDER_MODE_COUNT ? (int)mode : LADDER_LP24;
}

void ladder_set_oversampling(ladder_t *l, int factor)
{
    factor = hb_factor(factor);
    if (factor != l->oversample) {
        l->oversample = factor;
        hb4x_reset(&l->hb);
        l->snap = 1;
    }
}

void ladder_set(ladder_t *l, float cutoff_hz, float resonance, float drive)
{
    const float fs = (float)l->oversample * l->sr;
    const float fc = dsp_clampf(cutoff_hz, 10.0f, 0.45f * l->sr);
    const float g = fm_tan_pi(fc / fs);
    const float G = g / (1.0f + g);
    const float k = 4.2f * dsp_clampf(resonance, 0.0f, 1.0f);
    const float G2 = G * G;
    l->tG = G;
    l->tk = k;
    l->td = 1.0f / (1.0f + k * G2 * G2);
    l->drive = 1.0f + 6.0f * dsp_clampf(drive, 0.0f, 1.0f);
    l->inv_drive = 1.0f / l->drive;
    l->comp = 1.0f + l->comp_amount * k; /* partial passband compensation for the resonance */
    if (l->snap) {
        l->G = l->tG, l->k = l->tk, l->d = l->td;
        l->snap = 0;
    }
}

/* What a tick reads that does not change within a block, held in locals so the compiler keeps it in registers. */
typedef struct {
    float drive, inv_drive, comp;
    int mode;
} blk_t;

static inline blk_t blk_of(const ladder_t *l) { return (blk_t){l->drive, l->inv_drive, l->comp, l->mode}; }

/* One sample of the ladder at the internal rate; s: the four stage states (the caller's locals). */
static inline float tick(float *restrict s, const blk_t *c, float x, float G, float k, float d)
{
    const float b = 1.0f - G, G2 = G * G;
    const float S = b * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
    float u = (x * c->drive - k * S) * d;
    u = 2.0f * fm_tanh(0.5f * u); /* saturating input stage */
    float y[4], v = u;
    for (int i = 0; i < 4; i++) {
        y[i] = G * v + b * s[i];
        s[i] = 2.0f * y[i] - s[i];
        v = y[i];
    }
    const float out = c->mode == LADDER_LP24 ? y[3] : c->mode == LADDER_LP18 ? y[2] : y[1];
    return out * c->inv_drive * c->comp;
}

/* One input sample through the filter at the oversampling factor (the feedback path, which cannot run in passes). */
static inline float run(ladder_t *l, float *restrict s, const blk_t *c, float x, float G, float k, float d)
{
    if (l->oversample == 1)
        return tick(s, c, x, G, k, d);
    if (l->oversample == 2) {
        float a, b;
        hb2x_up(&l->hb.outer, x, &a, &b);
        const float ya = tick(s, c, a, G, k, d), yb = tick(s, c, b, G, k, d);
        return hb2x_down(&l->hb.outer, ya, yb);
    }
    float u[4], y[4];
    hb4x_up(&l->hb, x, u);
    for (int j = 0; j < 4; j++)
        y[j] = tick(s, c, u[j], G, k, d);
    return hb4x_down(&l->hb, y);
}

/* Without feedback, the block's state in locals; at 2x three passes over the block (all the upsampling, all the
 * ticks, all the downsampling: the half-band's input and output histories are separate, so this is the same
 * arithmetic in the same order as sample by sample, in tight loops). */
DSP_FAST_CODE void ladder_process(ladder_t *l, const float *in, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dG = (l->tG - l->G) * inv_n, dk = (l->tk - l->k) * inv_n, dd = (l->td - l->d) * inv_n;
    float G = l->G, k = l->k, d = l->d;
    float s[4] = {l->s[0], l->s[1], l->s[2], l->s[3]};
    const blk_t c = blk_of(l);
    const int os = l->oversample;
    for (int i0 = 0; i0 < n; i0 += SYNTH_BLOCK) {
        const int m = n - i0 < SYNTH_BLOCK ? n - i0 : SYNTH_BLOCK;
        const float *x = in + i0;
        float *y = out + i0;
        if (os == 1) {
            for (int i = 0; i < m; i++) {
                G += dG, k += dk, d += dd;
                y[i] = tick(s, &c, x[i], G, k, d);
            }
        } else if (os == 2) {
            float ua[SYNTH_BLOCK], ub[SYNTH_BLOCK];
            for (int i = 0; i < m; i++)
                hb2x_up(&l->hb.outer, x[i], &ua[i], &ub[i]);
            for (int i = 0; i < m; i++) {
                G += dG, k += dk, d += dd;
                ua[i] = tick(s, &c, ua[i], G, k, d);
                ub[i] = tick(s, &c, ub[i], G, k, d);
            }
            for (int i = 0; i < m; i++)
                y[i] = hb2x_down(&l->hb.outer, ua[i], ub[i]);
        } else { /* 4x: sample by sample (three passes measured slower here: 75 vs 68 ns per poly voice) */
            for (int i = 0; i < m; i++) {
                G += dG, k += dk, d += dd;
                float u[4];
                hb4x_up(&l->hb, x[i], u);
                for (int j = 0; j < 4; j++)
                    u[j] = tick(s, &c, u[j], G, k, d);
                y[i] = hb4x_down(&l->hb, u);
            }
        }
    }
    for (int i = 0; i < 4; i++)
        l->s[i] = s[i];
    l->G = l->tG, l->k = l->tk, l->d = l->td;
}

void ladder_set_compensation(ladder_t *l, float amount) { l->comp_amount = dsp_clampf(amount, 0.0f, 1.0f); }

DSP_FAST_CODE void ladder_process_fb(ladder_t *l, const float *in, const float *post, float fb, float hp_coef,
                                     float *state, float *out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dG = (l->tG - l->G) * inv_n, dk = (l->tk - l->k) * inv_n, dd = (l->td - l->d) * inv_n;
    float G = l->G, k = l->k, d = l->d;
    float last = state[0], hp = state[1];
    float s[4] = {l->s[0], l->s[1], l->s[2], l->s[3]};
    const blk_t c = blk_of(l);
    for (int i = 0; i < n; i++) {
        G += dG, k += dk, d += dd;
        hp += (last - hp) * hp_coef;
        const float x = in[i] + fb * (last - hp);
        const float y = run(l, s, &c, x, G, k, d);
        out[i] = y;
        last = y * post[i];
    }
    for (int i = 0; i < 4; i++)
        l->s[i] = s[i];
    state[0] = last, state[1] = hp;
    l->G = l->tG, l->k = l->tk, l->d = l->td;
}
