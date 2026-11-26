/* test_filter.c — TPT SVF: cutoff within +/-5 % (LP/HP, Q = 0.707), band-pass gain, stability under
 * fast modulation at high resonance, determinism; DC blocker; the ladder, the formant and screaming filters and
 * the common filter slot (vfilter); the morphing filter table (its frames' shapes and resonances, stable and bounded
 * under fast random morphing and feedback, deterministic). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engines/va.h"
#include "filter/dcblock.h"
#include "filter/formant.h"
#include "filter/scream.h"
#include "synth.h"
#include "filter/ladder.h"
#include "filter/morph.h"
#include "filter/svf_tpt.h"
#include "filter/vfilter.h"
#include "test.h"

#define SR 48000.0f

/* Steady-state gain (dB) for a sine of frequency f. */
static float svf_gain_db(svf_mode_t mode, float fc, float q, float f)
{
    const int n = 24000, skip = 12000;
    svf_t s;
    svf_init(&s, SR);
    svf_set_mode(&s, mode);
    svf_set(&s, fc, q);
    float *x = malloc(sizeof(float) * (size_t)n);
    double ph = 0.0;
    for (int i = 0; i < n; i++) {
        x[i] = (float)sin(2.0 * M_PI * ph);
        ph += (double)f / (double)SR;
    }
    const float rin = rms(x + skip, n - skip);
    svf_process(&s, x, x, n);
    const float rout = rms(x + skip, n - skip);
    free(x);
    return 20.0f * log10f(rout / rin);
}

/* Cutoff (20 Hz..20 kHz) and Q (0.5..25) drawn at random each block, mode changed regularly. */
static void run_modulated(const float *in, float *out, int n)
{
    svf_t s;
    svf_init(&s, SR);
    uint32_t r = 99u;
    for (int i = 0; i < n; i += 32) {
        svf_set_mode(&s, (svf_mode_t)((i / 4096) % SVF_MODE_COUNT));
        svf_set(&s, 20.0f * powf(1000.0f, dsp_rand_unipolar(&r)), 0.5f + 24.5f * dsp_rand_unipolar(&r));
        svf_process(&s, in + i, out + i, 32);
    }
}

/* Ladder: steady-state gain (dB) of a sine at f. */
static float ladder_gain_db(int os, float fc, float res, float f)
{
    const int n = 24000, skip = 12000;
    ladder_t l;
    ladder_init(&l, SR);
    ladder_set_oversampling(&l, os ? 2 : 1);
    ladder_set(&l, fc, res, 0.0f);
    float *x = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        x[i] = 0.1f * (float)sin(2.0 * M_PI * (double)f * i / (double)SR);
    const float rin = rms(x + skip, n - skip);
    for (int i = 0; i < n; i += 32)
        ladder_process(&l, x + i, x + i, 32);
    const float rout = rms(x + skip, n - skip);
    free(x);
    return 20.0f * log10f(rout / rin);
}

static void test_ladder(void)
{
    for (int os = 0; os < 2; os++) {
        static const float fcs[] = {100.0f, 1000.0f, 5000.0f};
        for (int i = 0; i < 3; i++) {
            const float g = ladder_gain_db(os, fcs[i], 0.0f, fcs[i]);
            CHECK(fabsf(g + 12.04f) < 1.0f, "ladder os %d: %.2f dB at fc %.0f (expected -12)", os, (double)g, (double)fcs[i]);
            const float pb = ladder_gain_db(os, fcs[i], 0.0f, fcs[i] * 0.1f);
            CHECK(fabsf(pb) < 0.5f, "ladder os %d: passband %.2f dB at fc/10", os, (double)pb);
        }
        /* self-oscillation: an impulse rings on at ~fc */
        ladder_t l;
        const int n = 48000;
        float *x = calloc((size_t)n, sizeof(float));
        x[0] = 0.5f;
        ladder_init(&l, SR);
        ladder_set_oversampling(&l, os ? 2 : 1);
        ladder_set(&l, 880.0f, 1.0f, 0.0f);
        for (int i = 0; i < n; i += 32)
            ladder_process(&l, x + i, x + i, 32);
        const float f = estimate_freq(x + n / 2, n / 2, SR);
        CHECK(rms(x + n - 4800, 4800) > 0.05f, "ladder os %d: no self-oscillation", os);
        CHECK(fabsf(f / 880.0f - 1.0f) < 0.05f, "ladder os %d: self-oscillates at %.1f Hz instead of ~880", os, (double)f);
        CHECK(all_finite_bounded(x, n, 4.0f), "ladder os %d: self-oscillation unbounded", os);
        free(x);
    }
    /* fast random modulation with drive, noise in: bounded and deterministic */
    {
        const int n = 48000 * 3;
        float *x = malloc(sizeof(float) * (size_t)n), *y1 = malloc(sizeof(float) * (size_t)n), *y2 = malloc(sizeof(float) * (size_t)n);
        for (int run = 0; run < 2; run++) {
            uint32_t r = 31u;
            ladder_t l;
            ladder_init(&l, SR);
            ladder_set_oversampling(&l, 2);
            for (int i = 0; i < n; i += 32) {
                for (int j = 0; j < 32; j++)
                    x[i + j] = dsp_rand_bipolar(&r);
                ladder_set_mode(&l, (ladder_mode_t)((i / 9600) % LADDER_MODE_COUNT));
                ladder_set(&l, 20.0f * powf(1000.0f, dsp_rand_unipolar(&r)), dsp_rand_unipolar(&r), dsp_rand_unipolar(&r));
                ladder_process(&l, x + i, (run ? y2 : y1) + i, 32);
            }
        }
        CHECK(all_finite_bounded(y1, n, 6.0f), "ladder modulated: NaN or |y| = %.2f", (double)max_abs(y1, n));
        CHECK(memcmp(y1, y2, sizeof(float) * (size_t)n) == 0, "ladder not deterministic");
        free(x);
        free(y1);
        free(y2);
    }
    /* oversampling reduces the aliasing of a hard-driven sine, step by step (1x, 2x, 4x), in the ladder and in the
     * scream filter; the level of the note itself does not depend on the factor */
    {
        const int N = 16384;
        static const int freqs[2] = {1707, 2731}; /* ~5 kHz and ~8 kHz on a bin */
        float *x = malloc(sizeof(float) * (size_t)N), *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        for (int which = 0; which < 2; which++)
            for (int fi = 0; fi < 2; fi++) {
                const int k0 = freqs[fi];
                float aliasdb[3], level[3];
                for (int o = 0; o < 3; o++) {
                    ladder_t l;
                    scream_t q;
                    ladder_init(&l, SR);
                    scream_init(&q, SR);
                    ladder_set_oversampling(&l, 1 << o);
                    scream_set_oversampling(&q, 1 << o);
                    for (int i = 0; i < N; i++)
                        x[i] = 0.9f * (float)sin(2.0 * M_PI * k0 * i / N);
                    for (int i = 0; i < N; i += 32) {
                        if (which == 0) {
                            ladder_set(&l, 20000.0f, 0.0f, 1.0f);
                            ladder_process(&l, x + i, x + i, 32);
                        } else {
                            scream_set(&q, 20000.0f, 0.3f, 1.0f);
                            scream_process(&q, x + i, x + i, 32);
                        }
                    }
                    spectrum_db(x, N, db);
                    float worst = -300.0f; /* everything except odd harmonics of k0 (true distortion) */
                    for (int k = 8; k <= N / 2; k++) {
                        int harm = 0;
                        for (int h = 1; h * k0 <= N / 2 + 4; h += 2)
                            harm |= abs(k - h * k0) <= 6;
                        if (!harm && db[k] > worst)
                            worst = db[k];
                    }
                    aliasdb[o] = worst - db[k0];
                    level[o] = db[k0];
                }
                const char *name = which ? "scream" : "ladder";
                const float hz = (float)k0 * SR / (float)N;
                printf("  %s drive aliasing at %.0f Hz: %.1f dB (1x) / %.1f dB (2x) / %.1f dB (4x)\n", name, (double)hz,
                       (double)aliasdb[0], (double)aliasdb[1], (double)aliasdb[2]);
                /* 5 kHz: the saturation's aliases are what 2x removes; what is left (about -50 dB) is the 5th
                 * harmonic (25 kHz) folding through the transition band of the half-band filter, the same at 2x
                 * and 4x. 8 kHz: the harmonics fold from far above, which 4x catches and 2x does not. */
                if (fi == 0) {
                    CHECK(aliasdb[1] < aliasdb[0] - 9.0f, "%s %.0f Hz: 2x does not reduce aliasing enough", name, (double)hz);
                    CHECK(aliasdb[2] < aliasdb[1] + 3.0f, "%s %.0f Hz: 4x is worse than 2x", name, (double)hz);
                    /* 1x: the cutoff (20 kHz) sits near Nyquist, where the filter's frequency warping bends the
                     * response a little; 2x and 4x agree closely */
                    CHECK(fabsf(level[2] - level[1]) < 0.5f && fabsf(level[1] - level[0]) < 1.0f,
                          "%s %.0f Hz: the note's level depends on the oversampling (%.2f / %.2f / %.2f dB)", name,
                          (double)hz, (double)level[0], (double)level[1], (double)level[2]);
                } else {
                    CHECK(aliasdb[2] < aliasdb[1] - 6.0f && aliasdb[2] < aliasdb[0] - 9.0f,
                          "%s %.0f Hz: 4x does not reduce aliasing below 1x and 2x", name, (double)hz);
                }
            }
        free(x);
        free(db);
    }
}

/* noise through a block processor, spectrum of the result (dB), for formant and comb-like filters */
#define FN 65536
static float fx[FN], fdb[FN / 2 + 1];
static float band_db(float hz)
{
    const int k = (int)(hz * 32768.0f / 48000.0f + 0.5f);
    double acc = 0.0;
    for (int d = -20; d <= 20; d++)
        acc += pow(10.0, (double)fdb[k + d] / 10.0);
    return (float)(10.0 * log10(acc / 41.0));
}
static void formant_noise(float vowel, float shift)
{
    formant_t f;
    formant_init(&f, 48000.0f);
    formant_set(&f, vowel, shift, 8.0f);
    uint32_t rng = dsp_seed(11u);
    for (int i = 0; i < FN; i += 32) {
        for (int j = 0; j < 32; j++)
            fx[i + j] = dsp_rand_bipolar(&rng);
        formant_process(&f, fx + i, fx + i, 32);
    }
    spectrum_db(fx + 16384, 32768, fdb);
}

/* gain (dB) of the morph filter for a sine of f, steady state, processed in blocks of 32 */
static float morph_gain_db(int table, float fc, float pos, float reso, float f)
{
    const int n = 24000, skip = 12000;
    morph_t m;
    morph_init(&m, SR);
    morph_set(&m, table, fc, pos, reso, 0.0f, 0.0f, 0.0f);
    float *x = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        x[i] = 0.1f * (float)sin(2.0 * M_PI * (double)f * i / (double)SR);
    const float rin = rms(x + skip, n - skip);
    for (int i = 0; i < n; i += 32)
        morph_process(&m, x + i, x + i, 32);
    const float rout = rms(x + skip, n - skip);
    free(x);
    return 20.0f * log10f(rout / rin);
}

/* the frequency (searched on a 1/48-octave grid from fc / 8 to fc x 8) where the response is highest */
static float morph_peak(int table, float fc, float pos, float reso, float lo, float hi)
{
    float best = -1e9f, at = lo;
    for (float f = lo; f <= hi; f *= 1.0145f) {
        const float g = morph_gain_db(table, fc, pos, reso, f);
        if (g > best)
            best = g, at = f;
    }
    return at;
}

static void test_morph(void)
{
    /* Classic: low-pass at 0 (24 dB/oct), high-pass at 2/3, band-pass between; unity pass band */
    const float fc = 1000.0f;
    float g = morph_gain_db(MORPH_CLASSIC, fc, 0.0f, 0.0f, 100.0f);
    CHECK(fabsf(g) < 0.5f, "morph classic LP pass band: %.2f dB", (double)g);
    g = morph_gain_db(MORPH_CLASSIC, fc, 0.0f, 0.0f, 4000.0f);
    CHECK(g < -40.0f, "morph classic LP two octaves up: %.1f dB (24 dB/oct expected)", (double)g);
    g = morph_gain_db(MORPH_CLASSIC, fc, 0.0f, 0.0f, fc);
    CHECK(fabsf(g + 3.0f) < 0.6f, "morph classic LP at the cutoff: %.2f dB (-3 expected)", (double)g);
    g = morph_gain_db(MORPH_CLASSIC, fc, 2.0f / 3.0f, 0.0f, 250.0f);
    CHECK(g < -40.0f, "morph classic HP two octaves down: %.1f dB", (double)g);
    g = morph_gain_db(MORPH_CLASSIC, fc, 2.0f / 3.0f, 0.0f, 10000.0f);
    CHECK(fabsf(g) < 0.5f, "morph classic HP pass band: %.2f dB", (double)g);
    const float bp_lo = morph_gain_db(MORPH_CLASSIC, fc, 1.0f / 3.0f, 0.0f, 125.0f);
    const float bp_hi = morph_gain_db(MORPH_CLASSIC, fc, 1.0f / 3.0f, 0.0f, 8000.0f);
    CHECK(bp_lo < -30.0f && bp_hi < -30.0f, "morph classic BP: %.1f dB 3 octaves down, %.1f up", (double)bp_lo,
          (double)bp_hi);
    /* resonance: a peak at the cutoff, the level held */
    const float pk = morph_peak(MORPH_CLASSIC, fc, 0.0f, 1.0f, 500.0f, 2000.0f);
    CHECK(fabsf(pk / fc - 1.0f) < 0.05f, "morph classic resonance at %.0f Hz (1000 expected)", (double)pk);
    g = morph_gain_db(MORPH_CLASSIC, fc, 0.0f, 1.0f, pk);
    CHECK(g > 3.0f && g < 12.0f, "morph classic resonant peak %.1f dB", (double)g);
    /* Growl: the frame's peaks where the table puts them */
    morph_section_t sec[MORPH_SECTIONS];
    for (int fr = 0; fr < MORPH_FRAMES; fr++) {
        const float pos = (float)fr / (float)(MORPH_FRAMES - 1);
        morph_frame(MORPH_GROWL, pos, sec);
        const float f1 = 500.0f * sec[1].pole;
        const float p = morph_peak(MORPH_GROWL, 500.0f, pos, 0.0f, f1 * 0.8f, f1 * 1.25f);
        CHECK(fabsf(p / f1 - 1.0f) < 0.05f, "morph growl frame %d: first formant at %.0f Hz (%.0f expected)", fr,
              (double)p, (double)f1);
    }
    /* fast random morphing, resonance, drive and feedback, on a loud saw: finite, bounded, deterministic */
    const int n = 48000;
    float *x = malloc(sizeof(float) * (size_t)n), *y = malloc(sizeof(float) * (size_t)n),
          *z = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        x[i] = 0.5f * (2.0f * (float)fmod(55.0 * i / (double)SR, 1.0) - 1.0f);
    for (int pass = 0; pass < 2; pass++) {
        morph_t m;
        morph_init(&m, SR);
        uint32_t r = 7u;
        float *o = pass ? z : y;
        for (int i = 0; i < n; i += 32) {
            const int t = (i / 4800) % MORPH_TABLE_COUNT;
            morph_set(&m, t, 40.0f * powf(400.0f, dsp_rand_unipolar(&r)), dsp_rand_unipolar(&r),
                      dsp_rand_unipolar(&r), dsp_rand_unipolar(&r), dsp_rand_bipolar(&r), dsp_rand_unipolar(&r));
            morph_process(&m, x + i, o + i, 32);
        }
    }
    CHECK(all_finite_bounded(y, n, 200.0f), "morph under random morphing: NaN, Inf or runaway");
    CHECK(memcmp(y, z, sizeof(float) * (size_t)n) == 0, "morph not deterministic");
    /* feedback at its top rings by itself after the input stops, bounded */
    morph_t m;
    morph_init(&m, SR);
    morph_set(&m, MORPH_GROWL, 300.0f, 0.5f, 0.6f, 0.3f, 1.0f, 0.0f);
    for (int i = 0; i < n; i += 32)
        morph_process(&m, i < 480 ? x + i : z, y + i, 32), memset(z, 0, sizeof(float) * 32);
    CHECK(all_finite_bounded(y, n, 200.0f), "morph feedback: NaN, Inf or runaway");
    const float ring = rms(y + n / 2, n / 2);
    CHECK(ring > 0.01f, "morph feedback 1: does not ring by itself after the input stops (rms %.4f)", (double)ring);
    morph_set(&m, MORPH_GROWL, 300.0f, 0.5f, 0.6f, 0.3f, 0.3f, 0.0f);
    for (int i = 0; i < n; i += 32)
        morph_process(&m, z, y + i, 32);
    const float dies = rms(y + n / 2, n / 2);
    CHECK(dies < 1e-4f, "morph feedback 0.3: still rings without input (rms %.5f)", (double)dies);
    free(x);
    free(y);
    free(z);
}

void suite_filter(void)
{
    test_morph();
    test_ladder();
    static const float fcs[] = {50.0f, 200.0f, 1000.0f, 5000.0f, 15000.0f, 20000.0f};
    for (size_t i = 0; i < sizeof fcs / sizeof fcs[0]; i++) {
        const float fc = fcs[i];
        const float lo = svf_gain_db(SVF_LP, fc, 0.70710678f, 0.95f * fc);
        const float hi = svf_gain_db(SVF_LP, fc, 0.70710678f, 1.05f * fc);
        CHECK(lo > -3.0103f && hi < -3.0103f, "LP fc %.0f: %.2f dB at 0.95fc, %.2f dB at 1.05fc", (double)fc,
              (double)lo, (double)hi);
        const float hlo = svf_gain_db(SVF_HP, fc, 0.70710678f, 0.95f * fc);
        const float hhi = svf_gain_db(SVF_HP, fc, 0.70710678f, 1.05f * fc);
        CHECK(hlo < -3.0103f && hhi > -3.0103f, "HP fc %.0f: %.2f dB at 0.95fc, %.2f dB at 1.05fc", (double)fc,
              (double)hlo, (double)hhi);
        const float bp = svf_gain_db(SVF_BP, fc, 5.0f, fc);
        CHECK(fabsf(bp) < 0.2f, "BP fc %.0f: center gain %.2f dB", (double)fc, (double)bp);
    }

    /* Cutoff clamped to 0.45*SR: requesting 30 kHz must not break anything */
    {
        const float g = svf_gain_db(SVF_LP, 30000.0f, 0.707f, 1000.0f);
        CHECK(fabsf(g) < 0.1f, "LP with 30 kHz requested: %.2f dB at 1 kHz", (double)g);
    }

    /* Fast random modulation, Q up to 25, noise input: stable, bounded, deterministic */
    {
        const int n = 48000 * 5;
        float *x = malloc(sizeof(float) * (size_t)n), *y1 = malloc(sizeof(float) * (size_t)n),
              *y2 = malloc(sizeof(float) * (size_t)n);
        uint32_t r = 1234567u;
        for (int i = 0; i < n; i++)
            x[i] = dsp_rand_bipolar(&r);
        run_modulated(x, y1, n);
        run_modulated(x, y2, n);
        CHECK(all_finite_bounded(y1, n, 60.0f), "modulated SVF: NaN or |y| = %.2f", (double)max_abs(y1, n));
        CHECK(memcmp(y1, y2, sizeof(float) * (size_t)n) == 0, "SVF not deterministic");
        free(x);
        free(y1);
        free(y2);
    }

    /* DC blocker: a DC step vanishes (< -60 dB after 1 s), 1 kHz passes */
    {
        dcblock_t d;
        dcblock_init(&d, SR);
        float x[480];
        float last = 1.0f;
        for (int b = 0; b < 100; b++) {
            for (int i = 0; i < 480; i++)
                x[i] = 0.5f;
            dcblock_process(&d, x, x, 480);
            last = x[479];
        }
        CHECK(fabsf(last) < 0.0005f, "DC blocker: %.5f left after 1 s", (double)last);
    }

    /* formant: vowel a has its first formant at 730 Hz (well above 1.5 kHz), i at 270 Hz and 2290 Hz */
    formant_noise(0.0f, 0.0f);
    CHECK(band_db(730.0f) > band_db(1500.0f) + 10.0f, "formant a: 730 Hz %.1f dB, 1.5 kHz %.1f dB", (double)band_db(730.0f),
          (double)band_db(1500.0f));
    formant_noise(0.5f, 0.0f);
    CHECK(band_db(2290.0f) > band_db(1300.0f) + 8.0f && band_db(270.0f) > band_db(600.0f) + 8.0f,
          "formant i: 270 Hz %.1f, 600 Hz %.1f, 1300 Hz %.1f, 2290 Hz %.1f dB", (double)band_db(270.0f),
          (double)band_db(600.0f), (double)band_db(1300.0f), (double)band_db(2290.0f));
    formant_noise(0.0f, 1.0f); /* one octave up: F1 of a at 1460 Hz */
    CHECK(band_db(1460.0f) > band_db(730.0f) + 6.0f, "formant shift +1 oct: 1460 Hz %.1f, 730 Hz %.1f dB",
          (double)band_db(1460.0f), (double)band_db(730.0f));

    /* scream: 12 dB/oct low-pass, self-oscillation near the cutoff, bounded when driven hard */
    {
        static scream_t k;
        float x[480];
        float lvl[2];
        for (int t = 0; t < 2; t++) {
            scream_init(&k, 48000.0f);
            const float f = t ? 8000.0f : 100.0f;
            float m = 0.0f;
            for (int b = 0; b < 200; b++) {
                for (int i = 0; i < 480; i++)
                    x[i] = sinf(6.2831853f * f * (float)(b * 480 + i) / 48000.0f);
                scream_set(&k, 1000.0f, 0.0f, 0.0f);
                scream_process(&k, x, x, 480);
                if (b > 100)
                    m = fmaxf(m, max_abs(x, 480));
            }
            lvl[t] = m;
        }
        const float att = 20.0f * log10f(lvl[0] / lvl[1]);
        CHECK(att > 30.0f, "scream low-pass: 8 kHz only %.1f dB below 100 Hz (12 dB/oct expected)", (double)att);

        scream_init(&k, 48000.0f);
        scream_set_oversampling(&k, 2);
        float *y = malloc(sizeof(float) * 48000);
        for (int b = 0; b < 100; b++) {
            for (int i = 0; i < 480; i++)
                y[b * 480 + i] = (b == 0 && i == 0) ? 1.0f : 0.0f;
            scream_set(&k, 600.0f, 1.0f, 0.3f);
            scream_process(&k, y + b * 480, y + b * 480, 480);
        }
        const float fo = estimate_freq(y + 24000, 24000, 48000.0f);
        CHECK(all_finite_bounded(y, 48000, 4.0f) && rms(y + 24000, 24000) > 1e-3f, "scream self-oscillation: dead or unbounded");
        CHECK(fabsf(fo / 600.0f - 1.0f) < 0.15f, "scream self-oscillation at %.1f Hz (cutoff 600 Hz)", (double)fo);
        uint32_t rng = dsp_seed(9u);
        for (int b = 0; b < 100; b++) { /* loud noise, full drive, resonance and a moving cutoff */
            for (int i = 0; i < 480; i++)
                y[b * 480 + i] = 4.0f * dsp_rand_bipolar(&rng);
            scream_set(&k, 200.0f * powf(50.0f, 0.5f + 0.5f * sinf((float)b * 0.3f)), 1.0f, 1.0f);
            scream_process(&k, y + b * 480, y + b * 480, 480);
        }
        CHECK(all_finite_bounded(y, 48000, 8.0f), "scream driven: unbounded (max %g)", (double)max_abs(y, 48000));
        free(y);
    }

    /* va second filter: series and parallel are finite, bounded and change the sound */
    {
        float *L0 = malloc(sizeof(float) * 24000), *L1 = malloc(sizeof(float) * 24000), *R = malloc(sizeof(float) * 24000);
        for (int k = 0; k < 3; k++) {
            synth_init(synth_default(), 48000.0f);
            if (k) {
                synth_set_param(synth_default(), VA_P_F2_MODE, param_to_norm(&va_params[VA_P_F2_MODE], 5.0f)); /* formant */
                synth_set_param(synth_default(), VA_P_FILTER_ROUTING, param_to_norm(&va_params[VA_P_FILTER_ROUTING], (float)(k - 1)));
            }
            const uint8_t on[3] = {0x90, 48, 110};
            synth_midi(synth_default(), on, 3, 0);
            float *out = k == 0 ? L0 : L1;
            for (int i = 0; i < 24000; i += 240)
                synth_render(synth_default(), out + i, R + i, 240);
            if (k) {
                CHECK(all_finite_bounded(L1, 24000, 1.0f), "va filter2 %s: NaN or > 0 dBFS", k == 1 ? "series" : "parallel");
                float diff = 0.0f;
                for (int i = 0; i < 24000; i++)
                    diff = fmaxf(diff, fabsf(L1[i] - L0[i]));
                CHECK(diff > 0.01f, "va filter2 %s: no effect", k == 1 ? "series" : "parallel");
            }
        }
        free(L0);
        free(L1);
        free(R);
    }
}
