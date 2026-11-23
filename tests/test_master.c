/* test_master.c — the master's saturation and limiter. Limiter alone: no peak past the ceiling (loud noise bursts,
 * a sine 18 dB over, at 48 and 96 kHz), transparent below it (the input delayed by the look-ahead, unchanged),
 * back to unity gain after a burst within a few release times, deterministic. Through the synth: the saturation
 * adds odd harmonics (soft) or even ones too (tube) while keeping the peak level at the master's nominal -12 dBFS, and
 * switches on and off without a click; the limiter holds the ceiling with
 * 18 dB of gain and switches on and off without a click; off, both leave the mix untouched. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engines/sine.h"
#include "fx/limiter.h"
#include "global_params.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 48000
#define NFFT 16384

static float L[N], R[N], L2[N], R2[N], X[N], db[NFFT / 2 + 1];
static limiter_t lim;

static void setg(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), v)); }
static void setpart(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(synth_part_param_info(id - SYNTH_PART_PARAM), v)); }
static void render(float *l, float *r, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, r + i, n - i < 256 ? n - i : 256);
}
static void sine_note(int note)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
    synth_set_param(synth_default(), SINE_P_SUSTAIN, 1.0f);
    const uint8_t m[3] = {0x90, (uint8_t)note, 127};
    synth_midi(synth_default(), m, 3, 0);
}
/* level in dB of the strongest bin within 3 of frequency f (spectrum in db) */
static float level_at(float f)
{
    const int k = (int)(f * NFFT / SR + 0.5f);
    float m = -400.0f;
    for (int d = -3; d <= 3; d++)
        m = fmaxf(m, db[k + d]);
    return m;
}
static void run_limiter(const float *inl, const float *inr, float *outl, float *outr, int n)
{
    for (int i = 0; i < n; i += 32) {
        memcpy(outl + i, inl + i, sizeof(float) * 32);
        memcpy(outr + i, inr + i, sizeof(float) * 32);
        limiter_process(&lim, outl + i, outr + i, 32);
    }
}

static void test_limiter(void)
{
    /* the ceiling holds: noise bursts up to +12 dBFS and a sine 18 dB over, 12 dB of gain, at two rates */
    static const float rates[2] = {48000.0f, 96000.0f};
    for (int k = 0; k < 2; k++) {
        uint32_t rng = dsp_seed(7u);
        for (int i = 0; i < N; i++) {
            const float burst = (i / 2400) % 3 == 0 ? 4.0f : 0.05f;
            X[i] = burst * dsp_rand_bipolar(&rng);
            L2[i] = 8.0f * sinf(6.2831853f * 60.0f * (float)i / rates[k]);
        }
        limiter_init(&lim, rates[k]);
        limiter_set(&lim, 12.0f, -1.0f, 0.1f);
        run_limiter(X, L2, L, R, N);
        const float peak = fmaxf(max_abs(L, N), max_abs(R, N)), ceil = powf(10.0f, -1.0f / 20.0f);
        CHECK(all_finite_bounded(L, N, 2.0f) && peak <= ceil * 1.0001f, "limiter at %.0f Hz: peak %.5f over the ceiling %.5f",
              (double)rates[k], (double)peak, (double)ceil);
    }

    /* transparent below the ceiling: the input, delayed by the look-ahead */
    limiter_init(&lim, SR);
    limiter_set(&lim, 0.0f, -1.0f, 0.1f);
    for (int i = 0; i < N; i++)
        X[i] = 0.3f * sinf(6.2831853f * 997.0f * (float)i / SR);
    run_limiter(X, X, L, R, N);
    const int lat = lim.len - 1;
    float err = 0.0f;
    for (int i = 0; i + lat < N; i++)
        err = fmaxf(err, fabsf(L[i + lat] - X[i]));
    CHECK(err < 1e-5f, "limiter below the ceiling: differs by %g (latency %d)", (double)err, lat);

    /* release: after a loud burst, back within 0.5 dB of unity in 5 release times (0.1 s) */
    limiter_init(&lim, SR);
    limiter_set(&lim, 0.0f, -1.0f, 0.1f);
    for (int i = 0; i < N; i++)
        X[i] = (i < 4800 ? 4.0f : 0.1f) * sinf(6.2831853f * 200.0f * (float)i / SR);
    run_limiter(X, X, L, R, N);
    const float back = rms(L + 4800 + 24000, 9600) / rms(X + 4800 + 24000 - lat, 9600);
    CHECK(20.0f * log10f(back) > -0.5f, "limiter release: %.2f dB still taken 0.5 s after the burst", (double)(20.0f * log10f(back)));

    /* deterministic */
    limiter_init(&lim, SR);
    limiter_set(&lim, 6.0f, -3.0f, 0.05f);
    run_limiter(X, X, L2, R2, N);
    limiter_init(&lim, SR);
    limiter_set(&lim, 6.0f, -3.0f, 0.05f);
    run_limiter(X, X, L, R, N);
    CHECK(memcmp(L, L2, sizeof L) == 0, "limiter not deterministic");
}

static void test_saturation(void)
{
    /* off: the mix untouched (a drive set, the mode off) */
    sine_note(57);
    render(L2, R2, N);
    sine_note(57);
    setg(SYNTH_PARAM_SATURATION_DRIVE, 18.0f);
    render(L, R, N);
    CHECK(memcmp(L, L2, sizeof L) == 0, "saturation off changes the mix");

    /* switched on and off while a sine plays: no step much larger than the sine's own */
    sine_note(45);
    setg(SYNTH_PARAM_SATURATION_DRIVE, 6.0f);
    render(L, R, 4800);
    float worst = 0.0f, prev = L[4799];
    const float amp = max_abs(L + 2400, 2400);
    for (int b = 0; b < 8; b++) {
        setg(SYNTH_PARAM_SATURATION, (float)((b + 1) & 1));
        render(L, R, 4800);
        for (int i = 0; i < 4800; i++)
            worst = fmaxf(worst, fabsf(L[i] - prev)), prev = L[i];
    }
    const float slope = 6.2831853f * 110.0f * amp / SR;
    CHECK(worst < 2.0f * slope, "saturation on/off: a step of %.5f (the sine's own %.5f)", (double)worst, (double)slope);
    /* the sine brought to the master's nominal peak, -12 dBFS */
    const float master_db = 20.0f * log10f(0.25f / max_abs(L2 + N / 2, N / 2)) - 6.0f; /* the part's level: +6 dB */
    sine_note(57);
    setpart(SYNTH_PART_LEVEL, 6.0f);
    setg(SYNTH_PARAM_MASTER_DB, master_db);
    render(L2, R2, N);
    spectrum_db(L2 + N - NFFT, NFFT, db);
    const float h3_clean = level_at(660.0f) - level_at(220.0f);

    /* soft: odd harmonics, no even ones; tube: even ones too; the level about the same */
    for (int mode = 1; mode <= 2; mode++) {
        sine_note(57);
        setpart(SYNTH_PART_LEVEL, 6.0f);
        setg(SYNTH_PARAM_MASTER_DB, master_db);
        setg(SYNTH_PARAM_SATURATION, (float)mode);
        setg(SYNTH_PARAM_SATURATION_DRIVE, 18.0f);
        render(L, R, N);
        spectrum_db(L + N - NFFT, NFFT, db);
        const float h1 = level_at(220.0f), h2 = level_at(440.0f) - h1, h3 = level_at(660.0f) - h1;
        const float lv = 20.0f * log10f(max_abs(L + N / 2, N / 2) / max_abs(L2 + N / 2, N / 2)); /* peak */
        printf("  saturation %s, 18 dB: H2 %.1f dB, H3 %.1f dB (clean %.1f), peak %+.1f dB\n", mode == 1 ? "soft" : "tube",
               (double)h2, (double)h3, (double)h3_clean, (double)lv);
        CHECK(all_finite_bounded(L, N, 1.0f) && h3 > -40.0f && h3 > h3_clean + 30.0f && fabsf(lv) < 2.0f,
              "saturation mode %d: H3 %.1f dB, peak %+.1f dB", mode, (double)h3, (double)lv);
        CHECK(mode == 1 ? h2 < -60.0f : h2 > -40.0f, "saturation mode %d: H2 %.1f dB", mode, (double)h2);
    }
}

static void test_limiter_synth(void)
{
    /* 18 dB of gain on a sine: held at the ceiling */
    sine_note(45);
    setg(SYNTH_PARAM_LIMITER, 1.0f);
    setg(SYNTH_PARAM_LIMITER_GAIN, 18.0f);
    setg(SYNTH_PARAM_LIMITER_CEILING, -3.0f);
    render(L, R, N);
    const float peak = max_abs(L + 2400, N - 2400), ceil = powf(10.0f, -3.0f / 20.0f);
    CHECK(peak <= ceil * 1.0001f && peak > 0.9f * ceil, "limiter through the synth: peak %.4f, ceiling %.4f", (double)peak,
          (double)ceil);

    /* switched on and off while a sine plays: no step larger than the sine's own (110 Hz, slope 2 pi f A / sr) */
    sine_note(45);
    render(L, R, 4800);
    float worst = 0.0f, amp = max_abs(L + 2400, 2400);
    float prev = L[4799];
    for (int b = 0; b < 8; b++) {
        setg(SYNTH_PARAM_LIMITER, (float)((b + 1) & 1));
        render(L, R, 4800);
        for (int i = 0; i < 4800; i++)
            worst = fmaxf(worst, fabsf(L[i] - prev)), prev = L[i];
    }
    const float slope = 6.2831853f * 110.0f * amp / SR;
    CHECK(worst < 2.0f * slope, "limiter on/off: a step of %.5f (the sine's own %.5f)", (double)worst, (double)slope);
}

void suite_master(void)
{
    test_limiter();
    test_saturation();
    test_limiter_synth();
}
