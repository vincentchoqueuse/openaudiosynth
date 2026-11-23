/* test_fm4.c — the 4-operator FM engine on its matrix. A lone carrier: the note's pitch, fixed mode ignores the note,
 * the output column picks who is heard. One cell: the sidebands of carrier 4f modulated by f follow the Bessel
 * functions of the index the cell gives (J1/J0, J2/J0 within 1 dB). Feedback adds harmonics. A link running against
 * the operators' order (1 -> 2) works, one sample late. Every cell at full, the loudest notes: finite and bounded.
 * Deterministic. Reference: J. Chowning (JAES 1973) for the sideband amplitudes. */
#include <math.h>
#include <string.h>

#include "engines/fm4.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 32768

static float L[N], R[N], L2[N], db[N / 2 + 1];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&fm4_params[id], v)); }
static void start(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FM4);
    for (int k = 0; k < FM4_OPS; k++) { /* held, flat envelopes, no velocity */
        setp(FM4_OP(k, FM4_ATTACK), 0.001f);
        setp(FM4_OP(k, FM4_SUSTAIN), 1.0f);
        setp(FM4_OP(k, FM4_VEL_SENS), 0.0f);
    }
    setp(FM4_P_MOD(1, 0), 0.0f);
    setp(FM4_P_SPREAD, 0.0f);
}
static void play(int note, float *l, int n)
{
    const uint8_t m[3] = {0x90, (uint8_t)note, 127};
    synth_midi(synth_default(), m, 3, 0);
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, R + i, n - i < 256 ? n - i : 256);
}
/* level (dB) of the strongest bin within 3 of f, in the spectrum of the last N/2 samples */
static float level_at(float f)
{
    const int k = (int)(f * (N / 2) / SR + 0.5f);
    float m = -400.0f;
    for (int d = -3; d <= 3; d++)
        m = fmaxf(m, db[k + d]);
    return m;
}
static void spectrum(const float *x) { spectrum_db(x + N / 2, N / 2, db); }

/* Bessel function of the first kind, integer order, by its integral (test only) */
static double bessel_j(int n, double x)
{
    double s = 0.0;
    const int steps = 2000;
    for (int i = 0; i < steps; i++) {
        const double t = M_PI * (i + 0.5) / steps;
        s += cos(n * t - x * sin(t));
    }
    return s / steps;
}

static void test_carrier(void)
{
    start();
    play(69, L, N);
    const float f = estimate_freq(L + N / 2, N / 2, SR);
    CHECK(fabsf(f - 440.0f) < 0.5f, "fm4 lone carrier: %.2f Hz for A4", (double)f);
    CHECK(all_finite_bounded(L, N, 1.0f), "fm4 lone carrier not bounded");

    /* fixed mode: the same frequency whatever the note */
    start();
    setp(FM4_OP(0, FM4_MODE), 1.0f);
    setp(FM4_OP(0, FM4_FREQ), 300.0f);
    play(40, L, N);
    const float ff = estimate_freq(L + N / 2, N / 2, SR);
    CHECK(fabsf(ff - 300.0f) < 0.5f, "fm4 fixed mode: %.2f Hz instead of 300", (double)ff);

    /* the output column: op 1 silent, op 2 (ratio 2) heard */
    start();
    setp(FM4_P_OUT(0), 0.0f);
    setp(FM4_P_OUT(1), 1.0f);
    setp(FM4_OP(1, FM4_COARSE), 2.0f);
    play(69, L, N);
    const float f2 = estimate_freq(L + N / 2, N / 2, SR);
    CHECK(fabsf(f2 - 880.0f) < 1.0f, "fm4 output column: %.2f Hz instead of op 2 at 880", (double)f2);
}

static void test_sidebands(void)
{
    /* carrier ratio 4, modulator ratio 1, cell c: deviation 2 pi FM4_MOD_MAX c^2 radians */
    static const float cells[2] = {0.3f, 0.5f};
    for (int t = 0; t < 2; t++) {
        start();
        setp(FM4_OP(0, FM4_COARSE), 4.0f);
        setp(FM4_P_MOD(1, 0), cells[t]);
        play(45, L, N); /* 110 Hz: carrier 440 */
        spectrum(L);
        const double beta = 2.0 * M_PI * (double)(FM4_MOD_MAX * cells[t] * cells[t]);
        const float c0 = level_at(440.0f), c1 = level_at(330.0f) - c0, c2 = level_at(220.0f) - c0;
        const float t1 = (float)(20.0 * log10(fabs(bessel_j(1, beta) / bessel_j(0, beta))));
        const float t2 = (float)(20.0 * log10(fabs(bessel_j(2, beta) / bessel_j(0, beta))));
        printf("  fm4 cell %.1f (beta %.2f): J1/J0 %.2f dB (theory %.2f), J2/J0 %.2f dB (theory %.2f)\n", (double)cells[t],
               beta, (double)c1, (double)t1, (double)c2, (double)t2);
        CHECK(fabsf(c1 - t1) < 1.0f && fabsf(c2 - t2) < 1.0f, "fm4 sidebands, cell %.1f: J1/J0 %.2f (%.2f), J2/J0 %.2f (%.2f)",
              (double)cells[t], (double)c1, (double)t1, (double)c2, (double)t2);
    }
}

static void test_feedback_and_loops(void)
{
    /* feedback on the carrier: harmonics appear */
    start();
    play(45, L, N);
    spectrum(L);
    const float h2_clean = level_at(220.0f) - level_at(110.0f);
    start();
    setp(FM4_P_MOD(0, 0), 0.8f);
    play(45, L, N);
    spectrum(L);
    const float h2 = level_at(220.0f) - level_at(110.0f), h3 = level_at(330.0f) - level_at(110.0f);
    CHECK(h2 > -20.0f && h3 > -30.0f && h2 > h2_clean + 40.0f, "fm4 feedback: H2 %.1f dB (clean %.1f), H3 %.1f dB",
          (double)h2, (double)h2_clean, (double)h3);

    /* a link against the order (op 1 modulates op 2, op 2 heard): sidebands like the forward link */
    start();
    setp(FM4_P_OUT(0), 0.0f);
    setp(FM4_P_OUT(1), 1.0f);
    setp(FM4_OP(1, FM4_COARSE), 4.0f);
    setp(FM4_P_MOD(0, 1), 0.3f);
    play(45, L, N);
    spectrum(L);
    const float back = level_at(330.0f) - level_at(440.0f);
    start();
    setp(FM4_OP(0, FM4_COARSE), 4.0f);
    setp(FM4_P_MOD(1, 0), 0.3f);
    play(45, L2, N);
    spectrum(L2);
    const float fwd = level_at(330.0f) - level_at(440.0f);
    CHECK(fabsf(back - fwd) < 0.5f, "fm4 backward link: J1/J0 %.2f dB, forward %.2f dB", (double)back, (double)fwd);

    /* everything at full: every cell, every output, high notes, a chord; finite and bounded */
    start();
    for (int c = 0; c < FM4_OPS * FM4_OPS; c++)
        setp(FM4_P_MATRIX + c, 1.0f);
    for (int k = 0; k < FM4_OPS; k++) {
        setp(FM4_P_OUT(k), 1.0f);
        setp(FM4_OP(k, FM4_COARSE), (float)(3 * k + 1));
    }
    for (int note = 60; note <= 108; note += 12) {
        const uint8_t m[3] = {0x90, (uint8_t)note, 127};
        synth_midi(synth_default(), m, 3, 0);
    }
    play(36, L, N);
    CHECK(all_finite_bounded(L, N, 1.0f), "fm4 full matrix: not finite or over 1 (peak %.3f)", (double)max_abs(L, N));
}

static void test_deterministic(void)
{
    start();
    setp(FM4_P_MOD(2, 0), 0.6f);
    setp(FM4_P_MOD(0, 2), 0.4f);
    setp(FM4_P_MOD(2, 2), 0.5f);
    setp(FM4_P_FILTER_TYPE, 1.0f);
    setp(FM4_P_PITCH_AMOUNT, 12.0f);
    play(57, L, N);
    memcpy(L2, L, sizeof L);
    start();
    setp(FM4_P_MOD(2, 0), 0.6f);
    setp(FM4_P_MOD(0, 2), 0.4f);
    setp(FM4_P_MOD(2, 2), 0.5f);
    setp(FM4_P_FILTER_TYPE, 1.0f);
    setp(FM4_P_PITCH_AMOUNT, 12.0f);
    play(57, L, N);
    CHECK(memcmp(L, L2, sizeof L) == 0 && all_finite_bounded(L, N, 1.0f), "fm4 not deterministic or not bounded");
}

void suite_fm4(void)
{
    test_carrier();
    test_sidebands();
    test_feedback_and_loops();
    test_deterministic();
}
