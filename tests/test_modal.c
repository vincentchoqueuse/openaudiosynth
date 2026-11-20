/* test_modal.c — modal engine and effect bus through the public API: pitch, T60 decay at +/-10 %,
 * release damper, sustained bowing, bounded/deterministic under random parameters; reverb tail,
 * freeze, delay echo time and chorus width on the synth bus. */
#include <math.h>
#include <stdlib.h>

#include "engines/modal.h"
#include "global_params.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}

static void set_m(int id, float physical) { synth_set_param(synth_default(), id, param_to_norm(&modal_params[id], physical)); }
static void set_g(int id, float physical)
{
    synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), physical));
}

static void render(float *L, float *R, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), L + i, R + i, n - i < 256 ? n - i : 256);
}

/* RMS in 10 ms windows; returns the time (s) at which it first falls below `ratio` x its maximum. */
static float time_below(const float *x, int n, float ratio)
{
    const int w = 480;
    float m = 0.0f;
    for (int i = 0; i + w <= n; i += w)
        m = fmaxf(m, rms(x + i, w));
    for (int i = 0; i + w <= n; i += w)
        if (rms(x + i, w) < ratio * m && i > 4800)
            return (float)i / SR;
    return (float)n / SR;
}

static void single_mode(float decay)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MODAL);
    set_m(MODAL_P_MODEL, 0.0f); /* harmonic */
    set_m(MODAL_P_MODES, 1.0f);
    set_m(MODAL_P_VARIATION, 0.0f);
    set_m(MODAL_P_NOISE, 0.0f);
    set_m(MODAL_P_DECAY, decay);
    set_m(MODAL_P_RELEASE, 20.0f);
    set_m(MODAL_P_SPREAD, 0.0f);
}

void suite_modal(void)
{
    const int n = 48000 * 4;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    /* pitch of a single mode = note frequency */
    single_mode(3.0f);
    midi3(0x90, 69, 100);
    render(L, R, n);
    float f = estimate_freq(L + 2400, 48000, SR);
    CHECK(fabsf(f - 440.0f) < 0.05f, "modal A4: %.4f Hz", (double)f);

    /* T60 = decay: -60 dB after `decay` seconds (key held, struck instruments decay anyway) */
    const float t60 = time_below(L, n, 0.001f);
    printf("  modal decay 3 s -> measured T60 %.2f s\n", (double)t60);
    CHECK(fabsf(t60 - 3.0f) < 0.3f, "modal T60 %.2f s instead of 3", (double)t60);

    /* stiff string: partial 2 at 2 sqrt((1+4B)/(1+B)) f0 (B = 0.01 -> 2.0295, i.e. 446.5 Hz on A3) */
    {
        single_mode(3.0f);
        set_m(MODAL_P_MODEL, 1.0f); /* string */
        set_m(MODAL_P_MODES, 2.0f);
        set_m(MODAL_P_INHARMONICITY, 0.01f);
        set_m(MODAL_P_BRIGHTNESS, 1.0f);
        midi3(0x90, 57, 100);
        render(L, R, n);
        const int N = 65536;
        float *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        spectrum_db(L + 2400, N, db);
        int kmax = (int)(400.0f * N / SR);
        for (int k = kmax; k < (int)(500.0f * N / SR); k++)
            if (db[k] > db[kmax])
                kmax = k;
        const float a = db[kmax - 1], b = db[kmax], c = db[kmax + 1];
        const float fpk = ((float)kmax + 0.5f * (a - c) / (a - 2.0f * b + c)) * SR / (float)N;
        const float expect = 220.0f * 2.0f * sqrtf((1.0f + 0.04f) / 1.01f);
        CHECK(fabsf(fpk - expect) < 0.5f, "stiff string partial 2: %.2f Hz instead of %.2f", (double)fpk,
              (double)expect);
        free(db);
    }

    /* decay keytrack 1: one octave up rings half as long */
    {
        float t[2];
        for (int o = 0; o < 2; o++) {
            single_mode(2.0f);
            set_m(MODAL_P_DECAY_KEYTRACK, 1.0f);
            midi3(0x90, (uint8_t)(60 + 12 * o), 100);
            render(L, R, n);
            t[o] = time_below(L, n, 0.001f);
        }
        CHECK(fabsf(t[1] / t[0] - 0.5f) < 0.06f, "decay keytrack: T60 %.2f s -> %.2f s (ratio should be 0.5)",
              (double)t[0], (double)t[1]);
    }

    /* release: note-off after 0.2 s with release 0.3 s damps the 3 s ring */
    single_mode(3.0f);
    set_m(MODAL_P_RELEASE, 0.3f);
    midi3(0x90, 69, 100);
    render(L, R, 9600);
    midi3(0x80, 69, 0);
    render(L + 9600, R + 9600, n - 9600);
    const float t_rel = time_below(L, n, 0.001f);
    CHECK(t_rel < 0.2f + 0.3f + 0.15f, "release does not damp: -60 dB at %.2f s", (double)t_rel);

    /* bowing: sustained while held, decays after release */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MODAL);
    set_m(MODAL_P_MODEL, 6.0f); /* bowl */
    set_m(MODAL_P_BOW, 1.0f);
    set_m(MODAL_P_BOW_ATTACK, 0.2f);
    set_m(MODAL_P_RELEASE, 0.5f);
    midi3(0x90, 60, 100);
    render(L, R, n);
    const float held_early = rms(L + 24000, 9600), held_late = rms(L + n - 9600, 9600);
    CHECK(held_late > 0.5f * held_early && held_late > 1e-3f, "bowing does not sustain (%.4f -> %.4f)",
          (double)held_early, (double)held_late);
    midi3(0x80, 60, 0);
    render(L, R, n);
    CHECK(rms(L + n - 9600, 9600) < 1e-4f, "bowed note does not stop after release");

    /* random parameters, many notes, all models: bounded and deterministic */
    for (int run = 0; run < 2; run++) {
        uint32_t r = 4242u;
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_MODAL);
        for (int i = 0; i < n; i += 480) {
            for (int k = 0; k < 3; k++) {
                const uint32_t x = dsp_rand_u32(&r);
                const uint8_t note = (uint8_t)(24 + (x >> 8) % 90);
                if (x % 4 < 2)
                    midi3(0x90, note, (uint8_t)(1 + ((x >> 16) & 0x7E)));
                else if (x % 4 == 2)
                    midi3(0x80, note, 0);
                else
                    synth_set_param(synth_default(), (int)((x >> 4) % MODAL_P_COUNT), dsp_rand_unipolar(&r));
            }
            render((run ? L2 : L) + i, (run ? R2 : R) + i, n - i < 480 ? n - i : 480);
        }
    }
    CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "modal storm: NaN or |x| > 1");
    CHECK(rms(L, n) > 1e-3f, "modal storm: silent");
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "modal storm: not deterministic");

    /* ---- effect bus ---- */
    /* reverb: a short note gets a long tail; freeze holds it */
    single_mode(0.2f);
    set_g(SYNTH_PARAM_REVERB_MIX, 0.6f);
    set_g(SYNTH_PARAM_REVERB_DECAY, 0.9f);
    midi3(0x90, 69, 110);
    render(L, R, n);
    const float t_rev = time_below(L, n, 0.001f);
    printf("  bus reverb: -60 dB after %.2f s (dry note rings 0.2 s)\n", (double)t_rev);
    CHECK(t_rev > 2.0f, "bus reverb tail too short: %.2f s", (double)t_rev);

    single_mode(0.2f);
    set_g(SYNTH_PARAM_REVERB_MIX, 0.6f);
    set_g(SYNTH_PARAM_REVERB_DECAY, 0.5f);
    midi3(0x90, 69, 110);
    render(L, R, 12000);
    midi3(0xB0, 69, 127); /* freeze */
    render(L, R, n);
    CHECK(rms(L + n - 4800, 4800) > 1e-3f, "freeze (CC 69) does not hold the tail");

    /* delay: the echo arrives delay_time later */
    single_mode(0.05f);
    set_g(SYNTH_PARAM_DELAY_MIX, 0.8f);
    set_g(SYNTH_PARAM_DELAY_TIME, 0.5f);
    set_g(SYNTH_PARAM_DELAY_WOW, 0.0f);
    set_g(SYNTH_PARAM_DELAY_FEEDBACK, 0.0f);
    midi3(0x90, 81, 120);
    render(L, R, n);
    const float dry = rms(L, 4800), echo = rms(L + 24000, 4800), gap = rms(L + 12000, 4800);
    CHECK(echo > 0.3f * dry && gap < 0.05f * dry, "bus delay: dry %.4f, gap %.4f, echo at 0.5 s %.4f",
          (double)dry, (double)gap, (double)echo);

    /* chorus: a mono-panned note becomes wide */
    single_mode(3.0f);
    set_g(SYNTH_PARAM_CHORUS_MODE, 4.0f); /* ensemble */
    set_g(SYNTH_PARAM_CHORUS_MIX, 1.0f);
    midi3(0x90, 57, 100);
    render(L, R, n);
    double lr = 0, ll = 0, rr = 0;
    for (int i = 4800; i < n; i++) {
        lr += (double)L[i] * (double)R[i];
        ll += (double)L[i] * (double)L[i];
        rr += (double)R[i] * (double)R[i];
    }
    CHECK(lr / sqrt(ll * rr) < 0.95, "bus chorus: no width (corr %.3f)", lr / sqrt(ll * rr));

    free(L);
    free(R);
    free(L2);
    free(R2);

    /* string nonlinearity (tension): the energy of the vibration (mostly the low partials) raises the tension,
     * so every partial starts sharp and glides down with it: partial 8 glides like the fundamental; everything
     * stays bounded; tension 0 is the linear model */
    {
        const int n2 = 48000 * 3, nf = 16384;
        float *x = malloc(sizeof(float) * (size_t)n2), *rr = malloc(sizeof(float) * (size_t)n2);
        float *spec = malloc(sizeof(float) * (size_t)(nf / 2 + 1));
        float g1[2], g8[2];
        const float f8 = 261.626f * 8.0f * sqrtf(1.0f + 0.0004f * 64.0f);
        for (int t = 0; t < 2; t++) {
            synth_init(synth_default(), 48000.0f);
            synth_set_engine(synth_default(), SYNTH_ENGINE_MODAL);
            const int ids[] = {MODAL_P_MODEL, MODAL_P_MODES, MODAL_P_INHARMONICITY, MODAL_P_DECAY, MODAL_P_SPREAD,
                               MODAL_P_VARIATION, MODAL_P_UNISON, MODAL_P_TENSION};
            const float vals[] = {1.0f, 32.0f, 0.0004f, 8.0f, 0.0f, 0.0f, 0.0f, t ? 0.6f : 0.0f};
            for (int i = 0; i < 8; i++)
                synth_set_param(synth_default(), ids[i], param_to_norm(&modal_params[ids[i]], vals[i]));
            const uint8_t on[3] = {0x90, 60, 127};
            synth_midi(synth_default(), on, 3, 0);
            for (int i = 0; i < n2; i += 256)
                synth_render(synth_default(), x + i, rr + i, n2 - i < 256 ? n2 - i : 256);
            CHECK(all_finite_bounded(x, n2, 1.0f), "modal tension %d: NaN or > 0 dBFS", t);
            g1[t] = 1200.0f * log2f(estimate_freq(x + 2400, 9600, 48000.0f) / estimate_freq(x + n2 - 24000, 24000, 48000.0f));
            float fk[2];
            for (int w = 0; w < 2; w++) { /* partial 8: strongest bin near it, parabolic interpolation */
                spectrum_db(x + (w ? n2 - nf : 1200), nf, spec);
                const int k0 = (int)(f8 * 0.98f * (float)nf / 48000.0f), k1 = (int)(f8 * 1.02f * (float)nf / 48000.0f);
                int k = k0;
                for (int j = k0; j <= k1; j++)
                    if (spec[j] > spec[k])
                        k = j;
                const float a = spec[k - 1], b2 = spec[k], c = spec[k + 1];
                fk[w] = ((float)k + 0.5f * (a - c) / (a - 2.0f * b2 + c)) * 48000.0f / (float)nf;
            }
            g8[t] = 1200.0f * log2f(fk[0] / fk[1]);
        }
        printf("  modal tension: glide of the fundamental %.2f / %.2f cents, of partial 8 %.2f / %.2f cents (linear / 0.6)\n",
               (double)g1[0], (double)g1[1], (double)g8[0], (double)g8[1]);
        CHECK(fabsf(g1[0]) < 0.5f && fabsf(g8[0]) < 1.0f, "modal linear: unexpected glide %.2f / %.2f cents", (double)g1[0],
              (double)g8[0]);
        CHECK(g1[1] > 3.0f && g1[1] < 20.0f, "modal tension: fundamental glide %.2f cents", (double)g1[1]);
        CHECK(g8[1] > 2.0f, "modal tension: partial 8 does not follow (%.2f cents)", (double)g8[1]);
        free(x);
        free(rr);
        free(spec);
    }
}
