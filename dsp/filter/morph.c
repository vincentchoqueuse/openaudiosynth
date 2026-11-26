/* morph.c — the morphing filter table (see morph.h): frames of pole/zero sections, interpolated in the poles'
 * octaves and Q and in the zeros' place, turned into biquads once per call and ramped across it (two stable biquads
 * lie in the convex stability triangle of (a1, a2), so the ramp between them is stable at every sample).
 * References: R. Rossum, ICMC 1992 (pole-zero morphing); RBJ cookbook (section gains at the pole). */
#include "morph.h"
#include "../core/fastmath.h"

#define N MORPH_NYQ
#define OFF {0.0f, 0.0f, 0.0f, 0.0f, 0.0f} /* a section not used by the table */

/* {pole ratio, Q, zero ratio, depth, gain at the pole} */
static const morph_section_t TABLES[MORPH_TABLE_COUNT][MORPH_FRAMES][MORPH_SECTIONS] = {
    /* Classic: 24 dB low-pass, band-pass (high-pass 12 x low-pass 12), high-pass, high-pass an octave up. The
     * gains at the pole are the sections' Q: each section is then the cookbook one, unity in its pass band. */
    {
        {{1.0f, 0.54f, N, 1.0f, 0.54f}, {1.0f, 1.31f, N, 1.0f, 1.31f}, OFF, OFF},
        {{1.0f, 0.54f, 0.0f, 1.0f, 0.54f}, {1.0f, 1.31f, N, 1.0f, 1.31f}, OFF, OFF},
        {{1.0f, 0.54f, 0.0f, 1.0f, 0.54f}, {1.0f, 1.31f, 0.0f, 1.0f, 1.31f}, OFF, OFF},
        {{2.0f, 0.54f, 0.0f, 1.0f, 0.54f}, {2.0f, 1.31f, 0.0f, 1.0f, 1.31f}, OFF, OFF},
    },
    /* Growl: a soft low-pass high above, and three dark formant peaks moving like a mouth, oo, oh, ah, ee (a cutoff
     * near 500 Hz puts them where a low voice has them) */
    {
        {{6.0f, 0.7f, N, 1.0f, 0.7f}, {0.6f, 6.0f, 0, -1, 6.0f}, {1.6f, 8.0f, 0, -1, 4.0f}, {4.0f, 10.0f, 0, -1, 3.0f}},
        {{6.0f, 0.7f, N, 1.0f, 0.7f}, {0.9f, 6.0f, 0, -1, 6.0f}, {1.8f, 8.0f, 0, -1, 4.0f}, {4.6f, 10.0f, 0, -1, 3.0f}},
        {{6.0f, 0.7f, N, 1.0f, 0.7f}, {1.4f, 5.0f, 0, -1, 6.0f}, {2.2f, 7.0f, 0, -1, 4.0f}, {5.0f, 9.0f, 0, -1, 3.0f}},
        {{6.0f, 0.7f, N, 1.0f, 0.7f}, {0.5f, 6.0f, 0, -1, 5.0f}, {3.6f, 9.0f, 0, -1, 5.0f}, {5.6f, 10.0f, 0, -1, 3.0f}},
    },
};

static float lerp(float a, float b, float t) { return a + (b - a) * t; }
/* the zeros' place on 0..1 (0 = DC, 1 = Nyquist ratio), so a zero can travel from one end to the other */
static float zwarp(float r) { return r / (1.0f + r); }

void morph_frame(int table, float position, morph_section_t *out)
{
    table = dsp_clampi(table, 0, MORPH_TABLE_COUNT - 1);
    const float x = dsp_clampf(position, 0.0f, 1.0f) * (float)(MORPH_FRAMES - 1);
    const int f = dsp_clampi((int)x, 0, MORPH_FRAMES - 2);
    const float t = x - (float)f;
    for (int i = 0; i < MORPH_SECTIONS; i++) {
        const morph_section_t *a = &TABLES[table][f][i], *b = &TABLES[table][f + 1][i];
        morph_section_t *o = &out[i];
        if (a->pole <= 0.0f) {
            *o = *a;
            continue;
        }
        o->pole = fm_exp2(lerp(fm_log2(a->pole), fm_log2(b->pole), t));
        o->q = fm_exp2(lerp(fm_log2(a->q), fm_log2(b->q), t));
        const float ua = zwarp(a->zero), ub = zwarp(b->zero), u = lerp(ua, ub, t);
        o->zero = u / dsp_maxf(1.0f - u, 1e-6f);
        /* zeros crossing the spectrum (low-pass to high-pass) would cross the poles: they draw in toward the origin on
         * the way, all the way at mid-path for a full crossing, so a section is never a notch on its own resonance */
        const float du = ub > ua ? ub - ua : ua - ub;
        o->depth = a->depth < 0.0f ? -1.0f : lerp(a->depth, b->depth, t) * (1.0f - 4.0f * t * (1.0f - t) * du);
        o->gain = lerp(a->gain, b->gain, t);
    }
}

void morph_reset(morph_t *m)
{
    for (int i = 0; i < MORPH_SECTIONS; i++)
        m->s1[i] = m->s2[i] = 0.0f;
    m->env = m->last = 0.0f;
    m->snap = 1;
}

void morph_init(morph_t *m, float sample_rate)
{
    m->sr = sample_rate;
    m->table = MORPH_CLASSIC;
    m->cutoff = 1000.0f;
    m->position = m->reso = m->drive = m->feedback = m->follow = 0.0f;
    m->env_up = 1.0f - fm_exp(-1.0f / (0.002f * sample_rate));   /* 2 ms up */
    m->env_down = 1.0f - fm_exp(-1.0f / (0.080f * sample_rate)); /* 80 ms down */
    for (int i = 0; i < MORPH_SECTIONS; i++) {
        m->b0[i] = m->t0[i] = 1.0f;
        m->b1[i] = m->b2[i] = m->a1[i] = m->a2[i] = 0.0f;
        m->t1[i] = m->t2[i] = m->u1[i] = m->u2[i] = 0.0f;
        m->lim[i] = 1.0f;
    }
    m->active = 0;
    m->top = 1.0f;
    morph_reset(m);
}

void morph_set(morph_t *m, int table, float cutoff_hz, float position, float reso, float drive, float feedback,
               float follow)
{
    m->table = dsp_clampi(table, 0, MORPH_TABLE_COUNT - 1);
    m->cutoff = dsp_clampf(cutoff_hz, 10.0f, 0.45f * m->sr);
    m->position = dsp_clampf(position, 0.0f, 1.0f);
    m->reso = dsp_clampf(reso, 0.0f, 1.0f);
    m->drive = dsp_clampf(drive, 0.0f, 1.0f);
    m->feedback = dsp_clampf(feedback, -1.0f, 1.0f);
    m->follow = dsp_clampf(follow, 0.0f, 1.0f);
}

/* |1 + c1 e^-jw + c2 e^-2jw|^2 at w = 2 pi th */
static float mag2(float c1, float c2, float th)
{
    const float re = 1.0f + c1 * fm_cos(th) + c2 * fm_cos(2.0f * th);
    const float im = -(c1 * fm_sin(th) + c2 * fm_sin(2.0f * th));
    return re * re + im * im;
}

/* the target biquads for the position p */
static void targets(morph_t *m, float p)
{
    morph_section_t sec[MORPH_SECTIONS];
    morph_frame(m->table, p, sec);
    const float inv_sr = 1.0f / m->sr;
    const float qk = fm_exp2(3.0f * m->reso), gk = fm_exp2(m->reso); /* Q x8, the peak x2 (a third as much) */
    float th_at[MORPH_SECTIONS];
    m->active = 0;
    for (int i = 0; i < MORPH_SECTIONS; i++) {
        const morph_section_t *s = &sec[i];
        if (s->pole <= 0.0f) {
            m->t0[i] = 1.0f, m->t1[i] = m->t2[i] = m->u1[i] = m->u2[i] = 0.0f;
            continue;
        }
        m->active = i + 1;
        const float th = dsp_clampf(m->cutoff * s->pole, 10.0f, 0.45f * m->sr) * inv_sr; /* cycles per sample */
        const float q = s->q * qk;
        /* the cookbook's poles: alpha = sin(w) / 2Q, stable for every Q > 0 */
        const float al = fm_sin(th) / (2.0f * q), ia = 1.0f / (1.0f + al);
        const float a1 = -2.0f * fm_cos(th) * ia, a2 = (1.0f - al) * ia;
        const float r = fm_exp2(0.5f * fm_log2(a2)); /* the poles' radius */
        float c1, c2, g;
        if (s->depth < 0.0f) { /* peak: zeros on the pole's angle, radius d: away 1, on it (1 - d) / (1 - r) */
            const float d = dsp_clampf(1.0f - s->gain * gk * (1.0f - r), 0.0f, 0.9999f);
            c1 = -2.0f * d * fm_cos(th), c2 = d * d;
            g = 1.0f;
        } else {
            const float thz = dsp_clampf(s->zero * m->cutoff * inv_sr, 0.0f, 0.5f);
            c1 = -2.0f * s->depth * fm_cos(thz), c2 = s->depth * s->depth;
            const float ratio = mag2(a1, a2, th) / dsp_maxf(mag2(c1, c2, th), 1e-9f);
            g = s->gain * gk * fm_exp2(0.5f * fm_log2(ratio));
        }
        m->lim[i] = dsp_maxf(1.0f, s->gain * gk);
        th_at[i] = th;
        m->t0[i] = g, m->t1[i] = g * c1, m->t2[i] = g * c2, m->u1[i] = a1, m->u2[i] = a2;
    }
    /* the cascade's gain at each resonance (squared), the highest kept: what the feedback loop is scaled by */
    float top = 1.0f;
    for (int k = 0; k < m->active; k++) {
        if (sec[k].pole <= 0.0f)
            continue;
        float h = 1.0f;
        for (int i = 0; i < m->active; i++)
            if (sec[i].pole > 0.0f)
                h *= mag2(m->t1[i] / m->t0[i], m->t2[i] / m->t0[i], th_at[k]) * m->t0[i] * m->t0[i] /
                     dsp_maxf(mag2(m->u1[i], m->u2[i], th_at[k]), 1e-12f);
        top = dsp_maxf(top, h);
    }
    m->top = fm_exp2(0.5f * fm_log2(top));
}

void morph_process(morph_t *m, const float *in, float *out, int n)
{
    if (n <= 0)
        return;
    targets(m, dsp_minf(1.0f, m->position + m->follow * dsp_minf(1.0f, 4.0f * m->env)));
    const int ns = m->active;
    const float inv_n = m->snap ? 0.0f : 1.0f / (float)n;
    if (m->snap) /* the first block after a reset starts on its filter */
        for (int i = 0; i < MORPH_SECTIONS; i++)
            m->b0[i] = m->t0[i], m->b1[i] = m->t1[i], m->b2[i] = m->t2[i], m->a1[i] = m->u1[i], m->a2[i] = m->u2[i];
    m->snap = 0;
    float d0[MORPH_SECTIONS], d1[MORPH_SECTIONS], d2[MORPH_SECTIONS], e1[MORPH_SECTIONS], e2[MORPH_SECTIONS];
    for (int i = 0; i < ns; i++) {
        d0[i] = (m->t0[i] - m->b0[i]) * inv_n, d1[i] = (m->t1[i] - m->b1[i]) * inv_n;
        d2[i] = (m->t2[i] - m->b2[i]) * inv_n, e1[i] = (m->u1[i] - m->a1[i]) * inv_n;
        e2[i] = (m->u2[i] - m->a2[i]) * inv_n;
    }
    const int sat = m->drive > 0.0f;
    /* each section's ceiling, from 1.5 x its gain (a full-scale input barely touched) down to 0.19 x (clipped); unity
     * gain below it */
    float ik[MORPH_SECTIONS], k[MORPH_SECTIONS];
    const float c = 1.5f * fm_exp2(-3.0f * m->drive);
    for (int i = 0; i < ns; i++)
        ik[i] = c * m->lim[i], k[i] = 1.0f / ik[i];
    /* what the clipping takes, about, given back; and what a positive feedback adds (1 / (1 - fb) at its peak, its
     * saturation holding it lower), about, taken */
    const float makeup = (sat ? fm_exp2(2.0f * m->drive) : 1.0f) / (1.0f + dsp_maxf(m->feedback, 0.0f));
    /* the loop's gain scaled by the cascade's highest gain, so that 1 is a little past the point where it rings by
     * itself (where the cascade's phase lets it) whatever the table and the resonance */
    const float fb = 1.3f * m->feedback / m->top;
    float env = m->env, last = m->last;
    for (int j = 0; j < n; j++) {
        const float x = in[j];
        const float ax = x < 0.0f ? -x : x;
        env += (ax > env ? m->env_up : m->env_down) * (ax - env);
        float y = x + fb * last;
        for (int i = 0; i < ns; i++) {
            m->b0[i] += d0[i], m->b1[i] += d1[i], m->b2[i] += d2[i], m->a1[i] += e1[i], m->a2[i] += e2[i];
            const float v = m->b0[i] * y + m->s1[i];
            m->s1[i] = m->b1[i] * y - m->a1[i] * v + m->s2[i];
            m->s2[i] = m->b2[i] * y - m->a2[i] * v;
            y = (sat && i + 1 < ns) ? ik[i] * fm_tanh(k[i] * v) : v;
        }
        last = fm_tanh(y);
        out[j] = makeup * y;
    }
    for (int i = 0; i < ns; i++) /* land exactly on the targets */
        m->b0[i] = m->t0[i], m->b1[i] = m->t1[i], m->b2[i] = m->t2[i], m->a1[i] = m->u1[i], m->a2[i] = m->u2[i];
    m->env = env, m->last = last;
}
