/* test_fx.c — Dattorro plate reverb: bounded and finite, tail decays and gets longer with decay,
 * decorrelated stereo, deterministic, works at 44.1/48/96 kHz. Delay, chorus, freeze; the voice distortion:
 * bounded, deterministic, level held across drives, no comb filtering at a partial mix, aliasing below a threshold. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fx/chorus.h"
#include "fx/delay.h"
#include "fx/dist.h"
#include "fx/reverb.h"
#include "test.h"

/* Index of the largest |x| in [a, b). */
static int argmax_abs(const float *x, int a, int b)
{
    int k = a;
    for (int i = a; i < b; i++)
        if (fabsf(x[i]) > fabsf(x[k]))
            k = i;
    return k;
}

static void test_delay(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 4;
    float *mem = malloc(sizeof(float) * delay_mem_floats(sr, 2.0f));
    float *x = calloc((size_t)n, sizeof(float)), *L = malloc(sizeof(float) * (size_t)n),
          *R = malloc(sizeof(float) * (size_t)n), *L2 = malloc(sizeof(float) * (size_t)n);
    delay_t d;

    /* echo time exact (no wow), right channel at ratio 1.5, repeats decay */
    delay_init(&d, sr, 2.0f, mem);
    delay_set(&d, 0.25f, 1.5f, 0.5f, 20000.0f, 0.0f, 0.0f, 0);
    d.d_cur[0] = d.d_target[0];
    d.d_cur[1] = d.d_target[1];
    x[0] = 0.5f;
    for (int i = 0; i < n; i += 32)
        delay_process(&d, x + i, x + i, L + i, R + i, 32);
    const int e1 = argmax_abs(L, 0, 18000), e2 = argmax_abs(L, 18000, 30000);
    CHECK(abs(e1 - 12000) <= 1, "delay: first echo at %d instead of 12000", e1);
    CHECK(abs(e2 - 24000) <= 2, "delay: second echo at %d instead of 24000", e2);
    CHECK(fabsf(L[e2]) < fabsf(L[e1]), "delay: repeats do not decay");
    const int r1 = argmax_abs(R, 0, 30000);
    CHECK(abs(r1 - 18000) <= 1, "delay: right echo at %d instead of 18000", r1);

    /* ping-pong: first repeat left, second right */
    delay_init(&d, sr, 2.0f, mem);
    delay_set(&d, 0.25f, 1.0f, 0.6f, 20000.0f, 0.0f, 0.0f, 1);
    for (int i = 0; i < n; i += 32)
        delay_process(&d, x + i, x + i, L + i, R + i, 32);
    CHECK(fabsf(L[argmax_abs(L, 6000, 18000)]) > 10.0f * fabsf(R[argmax_abs(R, 6000, 18000)]),
          "ping-pong: first repeat not on the left");
    CHECK(fabsf(R[argmax_abs(R, 18000, 30000)]) > 10.0f * fabsf(L[argmax_abs(L, 18000, 30000)]),
          "ping-pong: second repeat not on the right");

    /* worst case: max feedback, max drive and wow, noise in, time sweeps: bounded and deterministic */
    for (int run = 0; run < 2; run++) {
        uint32_t r = 77u;
        delay_init(&d, sr, 2.0f, mem);
        for (int i = 0; i < n; i += 32) {
            float in[32];
            for (int j = 0; j < 32; j++)
                in[j] = i < n / 4 ? dsp_rand_bipolar(&r) : 0.0f;
            delay_set(&d, 0.05f + 0.5f * (float)(i % 48000) / 48000.0f, 1.3f, 0.98f, 3000.0f, 1.0f, 1.0f, i > n / 2);
            delay_process(&d, in, in, (run ? L2 : L) + i, R + i, 32);
        }
    }
    CHECK(all_finite_bounded(L, n, 4.0f), "delay worst case: NaN or |y| = %.2f", (double)max_abs(L, n));
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "delay not deterministic");
    free(mem);
    free(x);
    free(L);
    free(R);
    free(L2);
}

static void test_chorus(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 2;
    float *x = malloc(sizeof(float) * (size_t)n), *L = malloc(sizeof(float) * (size_t)n),
          *R = malloc(sizeof(float) * (size_t)n);
    uint32_t r = 3u;
    for (int i = 0; i < n; i++)
        x[i] = 0.5f * dsp_rand_bipolar(&r);
    chorus_t *c = malloc(sizeof(chorus_t));

    /* off: exact passthrough */
    chorus_init(c, sr);
    for (int i = 0; i < n; i += 32)
        chorus_process(c, x + i, x + i, L + i, R + i, 32);
    CHECK(memcmp(L, x, sizeof(float) * (size_t)n) == 0, "chorus off is not a passthrough");

    for (int m = CHORUS_I; m < CHORUS_MODE_COUNT; m++) {
        chorus_init(c, sr);
        chorus_set(c, (chorus_mode_t)m, 1.0f);
        for (int i = 0; i < n; i += 32)
            chorus_process(c, x + i, x + i, L + i, R + i, 32);
        CHECK(all_finite_bounded(L, n, 2.0f) && all_finite_bounded(R, n, 2.0f), "chorus mode %d: NaN or overshoot", m);
        double lr = 0, ll = 0, rr = 0;
        for (int i = 4800; i < n; i++) {
            lr += (double)L[i] * (double)R[i];
            ll += (double)L[i] * (double)L[i];
            rr += (double)R[i] * (double)R[i];
        }
        const double corr = lr / sqrt(ll * rr);
        CHECK(corr < 0.98, "chorus mode %d: no stereo width (corr %.3f)", m, corr);
    }
    free(c);
    free(x);
    free(L);
    free(R);
}

static void test_freeze(void)
{
    const float sr = 48000.0f;
    reverb_t r;
    float *mem = malloc(sizeof(float) * reverb_mem_floats(sr, 1.0f));
    reverb_init(&r, sr, 1.0f, mem);
    reverb_set(&r, 0.5f, 6000.0f, 0.0f);
    float in[32], oL[32], oR[32];
    uint32_t s = 9u;
    float e_before = 0.0f, e_frozen = 0.0f, e_after = 0.0f;
    for (int b = 0; b < 1500 * 16; b++) { /* 16 s */
        for (int j = 0; j < 32; j++)
            in[j] = b < 1500 ? 0.3f * dsp_rand_bipolar(&s) : (b < 1500 * 13 ? 0.3f * dsp_rand_bipolar(&s) : 0.0f);
        if (b == 1500)
            reverb_set_freeze(&r, 1); /* input keeps playing but must not enter */
        if (b == 1500 * 13)
            reverb_set_freeze(&r, 0);
        reverb_process(&r, in, oL, oR, 32);
        const float e = rms(oL, 32);
        if (b == 1499)
            e_before = e;
        if (b > 1500 * 12 && b < 1500 * 13)
            e_frozen = fmaxf(e_frozen, e);
        if (b == 1500 * 16 - 1)
            e_after = e;
    }
    printf("  freeze: level before %.4f, after 11 s frozen %.4f, 3 s after release %.6f\n", (double)e_before,
           (double)e_frozen, (double)e_after);
    CHECK(e_frozen > 0.2f * e_before, "freeze does not hold the tail");
    CHECK(e_frozen < 3.0f * e_before, "freeze lets the input in or grows");
    CHECK(e_after < 0.01f * e_before, "tail does not decay after unfreeze");
    free(mem);
}

/* Impulse response; returns the time (s) for the smoothed energy to fall 60 dB below its maximum. */
static float impulse(float sr, float decay, float *L, float *R, int n)
{
    reverb_t r;
    float *mem = malloc(sizeof(float) * reverb_mem_floats(sr, 1.0f));
    reverb_init(&r, sr, 1.0f, mem);
    reverb_set(&r, decay, 8000.0f, 0.0f);
    float *in = calloc((size_t)n, sizeof(float));
    in[0] = 1.0f;
    for (int i = 0; i < n; i += 32)
        reverb_process(&r, in + i, L + i, R + i, 32);
    free(in);
    free(mem);
    /* energy in 10 ms windows */
    const int w = (int)(0.01f * sr);
    float emax = 0.0f, t60 = -1.0f;
    for (int i = 0; i + w <= n; i += w)
        emax = fmaxf(emax, rms(L + i, w));
    for (int i = 0; i + w <= n; i += w)
        if (rms(L + i, w) > emax * 0.001f)
            t60 = (float)(i + w) / sr;
    return t60;
}


/* Worst line (dB re the fundamental at bin k0) outside +/-4 bins of the harmonics, below bin kmax. */
static float dist_alias_db(const float *db, int k0, int kmax)
{
    float worst = -300.0f;
    for (int k = 5; k < kmax; k++) {
        const int r = k % k0;
        if (r <= 4 || r >= k0 - 4)
            continue;
        if (db[k] > worst)
            worst = db[k];
    }
    return worst - db[k0];
}

static void test_dist(void)
{
    const float sr = 48000.0f;
    const int n = 16384, k0 = 509; /* 1491 Hz on a bin (not a divisor of the rate: aliases fall between harmonics) */
    float *x = malloc(sizeof(float) * (size_t)n), *y = malloc(sizeof(float) * (size_t)n), *y2 = malloc(sizeof(float) * (size_t)n);
    float *db = malloc(sizeof(float) * (size_t)(n / 2 + 1));
    static const char *const names[] = {"off", "soft", "tube", "hard", "fold"};
    dist_t d;
    for (int i = 0; i < n; i++)
        x[i] = 0.7f * sinf(6.28318530717959f * (float)k0 * (float)i / (float)n);
    for (int mode = DIST_SOFT; mode < DIST_MODE_COUNT; mode++) {
        float lo = 1e9f, hi = 0.0f;
        for (int k = 0; k <= 4; k++) {
            const float drive = 9.0f * (float)k; /* 0, 9, 18, 27, 36 dB */
            dist_init(&d, sr);
            dist_set(&d, (dist_mode_t)mode, drive, 20000.0f, 1.0f);
            for (int i = 0; i < n; i += 32)
                dist_process(&d, x + i, y + i, 32);
            CHECK(all_finite_bounded(y, n, 2.0f), "dist %s %.0f dB: NaN or |y| > 2", names[mode], (double)drive);
            const float r = rms(y + 1024, n - 1024);
            lo = fminf(lo, r), hi = fmaxf(hi, r);
            /* determinism */
            dist_init(&d, sr);
            dist_set(&d, (dist_mode_t)mode, drive, 20000.0f, 1.0f);
            for (int i = 0; i < n; i += 32)
                dist_process(&d, x + i, y2 + i, 32);
            CHECK(memcmp(y, y2, sizeof(float) * (size_t)n) == 0, "dist %s: not deterministic", names[mode]);
            if (k == 2) { /* 18 dB: aliases below 10 kHz */
                spectrum_db(y, n, db);
                const float al = dist_alias_db(db, k0, (int)(10000.0f * (float)n / sr));
                printf("  dist %-4s 18 dB on 1491 Hz: worst alias below 10 kHz %.1f dB\n", names[mode], (double)al);
                CHECK(al < -50.0f, "dist %s: alias at %.1f dB", names[mode], (double)al);
            }
        }
        const float spread = 20.0f * log10f(hi / lo);
        printf("  dist %-4s level over 0..36 dB of drive: %.1f dB spread\n", names[mode], (double)spread);
        CHECK(spread < 8.0f, "dist %s: level moves %.1f dB with the drive", names[mode], (double)spread);
    }
    /* a partial mix at a low drive is the dry signal (no comb notch): a 9 kHz sine keeps its level */
    for (int i = 0; i < n; i++)
        x[i] = 0.05f * sinf(6.28318530717959f * 9000.0f * (float)i / sr);
    dist_init(&d, sr);
    dist_set(&d, DIST_SOFT, 0.0f, 20000.0f, 0.5f);
    for (int i = 0; i < n; i += 32)
        dist_process(&d, x + i, y + i, 32);
    const float ratio = 20.0f * log10f(rms(y + 1024, n - 1024) / rms(x + 1024, n - 1024));
    printf("  dist mix 0.5 at 9 kHz: %.2f dB\n", (double)ratio);
    CHECK(fabsf(ratio) < 1.5f, "dist: a partial mix notches 9 kHz (%.2f dB)", (double)ratio);
    free(x), free(y), free(y2), free(db);
}

void suite_fx(void)
{
    test_dist();
    test_delay();
    test_chorus();
    test_freeze();

    const float sr = 48000.0f;
    const int n = 48000 * 12;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    const float t_short = impulse(sr, 0.5f, L, R, n);
    CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "reverb: NaN or |y| > 1");
    const float t_long = impulse(sr, 0.9f, L, R, n);
    printf("  reverb decay 0.5 -> T60 %.2f s, decay 0.9 -> T60 %.2f s\n", (double)t_short, (double)t_long);
    CHECK(t_short > 0.3f && t_short < 4.0f, "decay 0.5: T60 %.2f s", (double)t_short);
    CHECK(t_long > 2.0f * t_short, "decay 0.9 not much longer (%.2f s)", (double)t_long);
    CHECK(t_long < 11.5f, "decay 0.9 never dies out");

    /* stereo: decorrelated outputs */
    {
        double sl = 0, sr2 = 0, slr = 0;
        for (int i = 4800; i < 4800 * 10; i++) {
            sl += (double)L[i] * (double)L[i];
            sr2 += (double)R[i] * (double)R[i];
            slr += (double)L[i] * (double)R[i];
        }
        const double c = slr / sqrt(sl * sr2 + 1e-30);
        CHECK(fabs(c) < 0.5, "reverb L/R correlation %.2f", c);
    }

    /* deterministic */
    impulse(sr, 0.9f, L2, R2, n);
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "reverb not deterministic");

    /* other rates, full-scale noise input with max decay: still bounded */
    static const float rates[] = {44100.0f, 96000.0f};
    for (int k = 0; k < 2; k++) {
        reverb_t r;
        float *mem = malloc(sizeof(float) * reverb_mem_floats(rates[k], 2.0f));
        reverb_init(&r, rates[k], 2.0f, mem);
        reverb_set(&r, 0.99f, 20000.0f, 0.25f);
        uint32_t s = 5u;
        int ok = 1;
        float in[32], oL[32], oR[32];
        for (int i = 0; i < (int)rates[k] * 10; i += 32) {
            for (int j = 0; j < 32; j++)
                in[j] = dsp_rand_bipolar(&s);
            reverb_process(&r, in, oL, oR, 32);
            ok &= all_finite_bounded(oL, 32, 20.0f) && all_finite_bounded(oR, 32, 20.0f);
        }
        CHECK(ok, "reverb at %.0f Hz, decay 0.99: NaN or runaway", (double)rates[k]);
        free(mem);
    }
    free(L);
    free(R);
    free(L2);
    free(R2);
}
