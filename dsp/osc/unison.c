/* unison.c — detuned, spread stack of polyBLEP oscillators (see unison.h).
 * Voice k of n sits at pos = -1 + 2k/(n-1); its offset is detune * sign(pos) |pos|^1.25 * (1 + 0.07 j_k), j_k a fixed
 * irregularity. Gains: the centre voice (the middle one, or the middle two) 1, the others `blend`, all scaled by
 * 1/sqrt(sum of the squares) so the incoherent sum keeps its loudness whatever the count and the blend. Pan: the
 * voices of a symmetric pair go to opposite sides and neighbours in pitch alternate, at |pos| x width. A single note's
 * level still moves by about 1 dB with the random start phases of its closest voices, as on any unison. */
#include "unison.h"
#include "../core/fastmath.h"

/* fixed irregularity of each voice's offset (no common beat period between the voices) */
static const float jitter[UNISON_MAX] = {0.31f, -0.74f, 0.12f, 0.93f, -0.45f, 0.58f, -0.17f, -0.86f,
                                         0.67f, -0.29f, 0.81f, -0.52f, 0.05f, 0.39f, -0.98f, 0.22f};

void unison_init(unison_t *u, float sample_rate, uint32_t seed)
{
    for (int k = 0; k < UNISON_MAX; k++) {
        osc_init(&u->osc[k], sample_rate);
        u->gl[k] = u->gr[k] = u->gl_prev[k] = u->gr_prev[k] = 0.0f;
    }
    u->rng = dsp_seed(seed);
    u->count = 1;
    unison_reset(u, 1);
}

void unison_reset(unison_t *u, int random)
{
    for (int k = 0; k < UNISON_MAX; k++) {
        osc_reset(&u->osc[k], random ? dsp_rand_unipolar(&u->rng) : 0.0f);
        u->lfo_phase[k] = dsp_rand_unipolar(&u->rng);
        u->lfo_factor[k] = 0.6f + 0.8f * dsp_rand_unipolar(&u->rng);
    }
    u->fresh = 1;
}

void unison_set(unison_t *u, float freq_hz, int wave, float pw, int voices, float detune_cents, float blend, float width,
                float motion_cents, float motion_hz, int block)
{
    const int n = dsp_clampi(voices, 1, UNISON_MAX);
    blend = dsp_clampf(blend, 0.0f, 1.0f);
    width = dsp_clampf(width, 0.0f, 1.0f);
    const float step = n > 1 ? 2.0f / (float)(n - 1) : 0.0f;
    float w[UNISON_MAX], sum = 0.0f;
    for (int k = 0; k < n; k++) {
        const float pos = -1.0f + step * (float)k, a = dsp_absf(pos);
        w[k] = n == 1 || a < 0.5f * step + 1e-6f ? 1.0f : blend; /* the middle one or two are the centre */
        sum += w[k] * w[k];
    }
    const float g = fm_exp2(-0.5f * fm_log2(sum)); /* 1 / sqrt(sum): constant loudness */
    for (int k = 0; k < n; k++) {
        const float pos = n > 1 ? -1.0f + step * (float)k : 0.0f, a = dsp_absf(pos);
        /* pitch: inner voices close, outer ones pushed out, each a little off the grid */
        float cents = detune_cents * (pos < 0.0f ? -1.0f : 1.0f) * a * fm_exp2(0.25f * fm_log2(a + 1e-9f)) *
                      (1.0f + 0.07f * jitter[k]);
        if (motion_cents > 0.0f) {
            u->lfo_phase[k] += motion_hz * u->lfo_factor[k] * (float)block * u->osc[k].inv_sr;
            if (u->lfo_phase[k] >= 1.0f)
                u->lfo_phase[k] -= (float)(int)u->lfo_phase[k];
            cents += motion_cents * fm_sin(u->lfo_phase[k]);
        }
        osc_set_wave(&u->osc[k], (osc_wave_t)dsp_clampi(wave, 0, OSC_WAVE_COUNT - 1));
        osc_set(&u->osc[k], freq_hz * fm_exp2(cents * (1.0f / 1200.0f)), pw);
        /* pan: a pair's two voices on opposite sides, neighbours alternating; constant power */
        const float pan = width * a * ((k & 1) ? -1.0f : 1.0f);
        const float ang = 0.125f * (pan + 1.0f); /* a quarter turn from hard left to hard right, in cycles */
        u->gl_prev[k] = u->gl[k], u->gr_prev[k] = u->gr[k];
        u->gl[k] = g * w[k] * 1.41421356f * fm_cos(ang); /* centred: the voice's full gain on each side */
        u->gr[k] = g * w[k] * 1.41421356f * fm_sin(ang);
    }
    for (int k = n; k < UNISON_MAX; k++) /* voices beyond the count fade out from where they were */
        u->gl_prev[k] = u->gl[k], u->gr_prev[k] = u->gr[k], u->gl[k] = u->gr[k] = 0.0f;
    if (u->fresh) {
        for (int k = 0; k < UNISON_MAX; k++)
            u->gl_prev[k] = u->gl[k], u->gr_prev[k] = u->gr[k];
        u->fresh = 0;
    }
    /* a voice dropped this block still has to fade: keep it counted until it is silent */
    int live = n;
    for (int k = n; k < UNISON_MAX; k++)
        if (u->gl_prev[k] != 0.0f || u->gr_prev[k] != 0.0f)
            live = k + 1;
    u->count = live;
}

DSP_FAST_CODE void unison_process(unison_t *u, float *restrict out_l, float *restrict out_r, int n)
{
    float t[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        out_l[i] = 0.0f;
    if (out_r)
        for (int i = 0; i < n; i++)
            out_r[i] = 0.0f;
    for (int k = 0; k < u->count; k++) {
        osc_process(&u->osc[k], t, n);
        if (out_r) {
            dsp_ramp_mac(out_l, t, u->gl_prev[k], u->gl[k], n);
            dsp_ramp_mac(out_r, t, u->gr_prev[k], u->gr[k], n);
        } else { /* mono: the voice at its centre gain (its left and right gains are equal at width 0) */
            dsp_ramp_mac(out_l, t, 0.5f * (u->gl_prev[k] + u->gr_prev[k]), 0.5f * (u->gl[k] + u->gr[k]), n);
        }
    }
}
