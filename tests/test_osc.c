/* test_osc.c — polyBLEP oscillators: bounds, determinism, tuning, aliasing.
 * Aliasing: saw at f0 aligned on an FFT bin; the strongest non-harmonic line (alias) is compared
 * to the fundamental, for the polyBLEP saw and for a naive saw. */
#include <math.h>
#include <stdlib.h>

#include "osc/noise.h"
#include "osc/polyblep.h"
#include "osc/unison.h"

#include "test.h"

#define SR 48000.0f

static void render_osc(int wave, float freq, float pw, float *out, int n)
{
    osc_t o;
    osc_init(&o, SR);
    osc_set_wave(&o, (osc_wave_t)wave);
    osc_set(&o, freq, pw);
    for (int i = 0; i < n; i += 32)
        osc_process(&o, out + i, 32);
}

/* Worst alias (dB relative to the fundamental) among bins below kmax, outside the neighborhood
 * (+/-4 bins) of harmonics and DC. */
static float worst_alias_db(const float *db, int n, int k0, int kmax)
{
    const int half = n / 2;
    char *harm = calloc((size_t)half + 1, 1);
    for (int k = 0; k <= half; k += k0)
        for (int d = -4; d <= 4; d++)
            if (k + d >= 0 && k + d <= half)
                harm[k + d] = 1;
    float worst = -300.0f;
    for (int k = 0; k <= half && k < kmax; k++)
        if (!harm[k] && db[k] > worst)
            worst = db[k];
    free(harm);
    return worst - db[k0];
}

void suite_osc(void)
{
    const int n = 48000;
    float *a = malloc(sizeof(float) * (size_t)n), *b = malloc(sizeof(float) * (size_t)n);
    static const char *names[] = {"sine", "saw", "pulse", "tri"};
    static const float freqs[] = {27.5f, 440.0f, 3520.0f, 12000.0f, 21000.0f};

    for (int w = 0; w < OSC_WAVE_COUNT; w++) {
        for (size_t f = 0; f < sizeof freqs / sizeof freqs[0]; f++) {
            render_osc(w, freqs[f], 0.3f, a, n);
            CHECK(all_finite_bounded(a, n, 1.3f), "%s %.0f Hz: NaN or |x| = %.3f", names[w], (double)freqs[f],
                  (double)max_abs(a, n));
        }
        render_osc(w, 440.0f, 0.5f, a, n);
        render_osc(w, 440.0f, 0.5f, b, n);
        CHECK(memcmp(a, b, sizeof(float) * (size_t)n) == 0, "%s: not deterministic", names[w]);
        const float fe = estimate_freq(a, n, SR);
        CHECK(fabsf(fe - 440.0f) < 0.05f, "%s: frequency %.4f Hz instead of 440", names[w], (double)fe);
    }

    /* fast PWM: no NaN, bounded */
    {
        osc_t o;
        osc_init(&o, SR);
        osc_set_wave(&o, OSC_PULSE);
        for (int i = 0; i < n; i += 32) {
            osc_set(&o, 1000.0f + 900.0f * sinf((float)i * 0.001f), 0.5f + 0.45f * sinf((float)i * 0.01f));
            osc_process(&o, a + i, 32);
        }
        CHECK(all_finite_bounded(a, n, 1.3f), "PWM: NaN or overshoot (%.3f)", (double)max_abs(a, n));
    }

    /* Aliasing, saw and pulse, polyBLEP vs naive. f0 is bin-aligned so harmonics fall on bins.
     * Criteria (see docs/decisions.md): aliases below 10 kHz under -50 dB and >= 20 dB better than
     * naive; over the whole band >= 6 dB better (2-point polyBLEP gains little right below Nyquist). */
    {
        const int N = 16384, k10k = N * 10000 / 48000;
        static const int k0s[] = {853, 1365}; /* ~2499 Hz and ~3999 Hz */
        float *x = malloc(sizeof(float) * (size_t)N), *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        for (int w = OSC_SAW; w <= OSC_PULSE; w++) {
            for (int q = 0; q < 2; q++) {
                const int k0 = k0s[q];
                const float f0 = (float)k0 * SR / (float)N;
                render_osc(w, f0, 0.5f, x, N);
                spectrum_db(x, N, db);
                const float b_all = worst_alias_db(db, N, k0, N), b_low = worst_alias_db(db, N, k0, k10k);
                double ph = 0.0; /* naive reference */
                for (int i = 0; i < N; i++) {
                    x[i] = w == OSC_SAW ? (float)(2.0 * ph - 1.0) : (ph < 0.5 ? 1.0f : -1.0f);
                    ph += (double)f0 / (double)SR;
                    ph -= floor(ph);
                }
                spectrum_db(x, N, db);
                const float n_all = worst_alias_db(db, N, k0, N), n_low = worst_alias_db(db, N, k0, k10k);
                printf("  %-5s %4.0f Hz: worst alias <10k %.1f dB (naive %.1f), full band %.1f dB (naive %.1f)\n",
                       names[w], (double)f0, (double)b_low, (double)n_low, (double)b_all, (double)n_all);
                CHECK(b_low < -50.0f, "%s %.0f Hz: alias below 10 kHz at %.1f dB", names[w], (double)f0,
                      (double)b_low);
                CHECK(b_low < n_low - 20.0f, "%s %.0f Hz: not 20 dB better than naive below 10 kHz", names[w],
                      (double)f0);
                CHECK(b_all < n_all - 6.0f, "%s %.0f Hz: not 6 dB better than naive over the full band", names[w],
                      (double)f0);
            }
        }
        free(x);
        free(db);
    }

    /* Unison: one voice = exact pitch; loudness kept across the count and the blend; width 0 = the same on both
     * sides, width 1 = two different sides; bounded; deterministic */
    {
        unison_t u;
        float *c = malloc(sizeof(float) * (size_t)n), *r = malloc(sizeof(float) * (size_t)n);
        unison_init(&u, SR, 1);
        for (int i = 0; i < n; i += 32) {
            unison_set(&u, 440.0f, OSC_SAW, 0.5f, 1, 30.0f, 1.0f, 0.0f, 0.0f, 0.2f, 32);
            unison_process(&u, a + i, NULL, 32);
        }
        const float fe = estimate_freq(a, n, SR);
        CHECK(fabsf(fe - 440.0f) < 0.05f, "unison x1: %.4f Hz", (double)fe);

        /* the level, averaged over four notes (each starts its voices at random phases, and the closest voices
         * beat slowly: one note alone moves by about 1 dB, as on any unison) */
        float lo = 1e9f, hi = -1e9f;
        for (int voices = 2; voices <= UNISON_MAX; voices += 7) {
            for (int m = 0; m < 2; m++) {
                double e = 0.0;
                for (int seed = 0; seed < 4; seed++) {
                    for (int run = 0; run < 2; run++) {
                        unison_init(&u, SR, 7u + 101u * (uint32_t)seed);
                        for (int i = 0; i < n; i += 32) {
                            unison_set(&u, 220.0f, OSC_SAW, 0.5f, voices, 30.0f, (float)m, 0.0f, 8.0f, 0.5f, 32);
                            unison_process(&u, (run ? c : b) + i, NULL, 32);
                        }
                    }
                    CHECK(memcmp(b, c, sizeof(float) * (size_t)n) == 0, "unison not deterministic");
                    CHECK(all_finite_bounded(b, n, 3.0f), "unison: NaN or |x| = %.3f", (double)max_abs(b, n));
                    const float x = rms(b + 4800, n - 4800);
                    e += (double)x * (double)x;
                }
                const float db = (float)(10.0 * log10(e / 4.0));
                lo = fminf(lo, db), hi = fmaxf(hi, db);
            }
        }
        printf("  unison 2..16 voices, blend 0..1: rms %.1f .. %.1f dB (4 notes each)\n", (double)lo, (double)hi);
        CHECK(hi - lo < 1.5f, "unison loudness changes with the count or the blend: %.1f dB", (double)(hi - lo));

        for (int wide = 0; wide < 2; wide++) {
            unison_init(&u, SR, 3);
            for (int i = 0; i < n; i += 32) {
                unison_set(&u, 220.0f, OSC_SAW, 0.5f, 7, 30.0f, 0.8f, (float)wide, 0.0f, 0.2f, 32);
                unison_process(&u, c + i, r + i, 32);
            }
            float diff = 0.0f;
            for (int i = 4800; i < n; i++)
                diff = fmaxf(diff, fabsf(c[i] - r[i]));
            if (wide)
                CHECK(diff > 0.1f, "unison width 1: left and right are the same (max diff %.4f)", (double)diff);
            else
                CHECK(diff < 1e-6f, "unison width 0: left and right differ (max diff %.6f)", (double)diff);
        }
        free(c);
        free(r);
    }

    /* Noise: mean ~0, values in [-1, 1), deterministic */
    {
        noise_t z1, z2;
        noise_init(&z1, 42);
        noise_init(&z2, 42);
        noise_process(&z1, a, n);
        noise_process(&z2, b, n);
        float m = 0.0f;
        for (int i = 0; i < n; i++)
            m += a[i];
        m /= (float)n;
        CHECK(all_finite_bounded(a, n, 1.0f), "noise out of bounds");
        CHECK(fabsf(m) < 0.02f, "noise: mean %.4f", (double)m);
        CHECK(fabsf(rms(a, n) - 0.57735f) < 0.01f, "noise: rms %.4f", (double)rms(a, n));
        CHECK(memcmp(a, b, sizeof(float) * (size_t)n) == 0, "noise not deterministic");
    }
    free(a);
    free(b);
}
