/* test_havoc.c — the noise machine with a wide palette. One generator alone: the feedback-free tone sits on the
 * note, the narrow band of noise sits at its pitch, every process sounds, stays bounded and repeats exactly; the
 * pulse life leaves silence between its hits; the frequency modulation by the generator before changes the sound.
 * The bleeps leave rests. The processors: a low-pass removes a high tone, the stutter repeats fragments, every type changes the signal and keeps it bounded. Then the
 * whole machine at its extremes, and the release to silence. */
#include <math.h>
#include <string.h>

#include "engines/havoc.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 65536

static float L[N], R[N], L2[N], db[N / 4 + 1];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&havoc_params[id], v)); }
/* generator 1 alone, at full level, following the note; no processor, instant attack */
static void solo(int type, float a, float b)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_HAVOC);
    setp(HAVOC_P_ATTACK, 0.001f);
    setp(HAVOC_P_VELOCITY, 0.0f);
    for (int g = 0; g < HAVOC_GEN; g++)
        setp(HAVOC_G_P(g, HAVOC_G_LEVEL), g == 0 ? 1.0f : 0.0f);
    setp(HAVOC_G_P(0, HAVOC_G_TYPE), (float)type);
    setp(HAVOC_G_P(0, HAVOC_G_PITCH), 0.0f);
    setp(HAVOC_G_P(0, HAVOC_G_TRACK), 1.0f);
    setp(HAVOC_G_P(0, HAVOC_G_A), a);
    setp(HAVOC_G_P(0, HAVOC_G_B), b);
}
static void play(int note, float *l, int n)
{
    const uint8_t m[3] = {0x90, (uint8_t)note, 127};
    synth_midi(synth_default(), m, 3, 0);
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, R + i, n - i < 256 ? n - i : 256);
}
static float peak_hz(const float *x, float lo, float hi)
{
    spectrum_db(x, N / 2, db);
    const float bin = SR / (float)(N / 2);
    int best = (int)(lo / bin);
    for (int k = best; k < (int)(hi / bin); k++)
        if (db[k] > db[best])
            best = k;
    return (float)best * bin;
}

static void test_generators(void)
{
    solo(HAVOC_T_TONE, 0.0f, 0.0f);
    play(69, L, N);
    const float f = estimate_freq(L + N / 2, N / 2, SR);
    CHECK(fabsf(f - 440.0f) < 2.2f, "havoc tone at A4: %.2f Hz", (double)f);

    /* a narrow band of noise (a = 0.9: Q about 40) at the pitch: A5 */
    solo(HAVOC_T_WIND, 0.9f, 0.0f);
    play(81, L, N);
    const float fw = peak_hz(L + N / 2, 200.0f, 4000.0f);
    CHECK(fabsf(fw - 880.0f) < 0.05f * 880.0f, "havoc wind band: %.0f Hz, expected 880", (double)fw);

    /* every process, in the middle of its controls and at their ends: it sounds, bounded, and repeats */
    for (int type = 0; type < HAVOC_T_COUNT; type++)
        for (int set = 0; set < 3; set++) {
            const float a = set == 0 ? 0.5f : set == 1 ? 0.0f : 1.0f, b = set == 2 ? 1.0f : set == 1 ? 0.0f : 0.5f;
            solo(type, a, b);
            play(60, L, N);
            solo(type, a, b);
            play(60, L2, N);
            CHECK(all_finite_bounded(L, N, 1.0f), "havoc %s a %g b %g: NaN or over 0 dBFS (max %g)",
                  havoc_params[HAVOC_G_P(0, HAVOC_G_TYPE)].labels[type], (double)a, (double)b, (double)max_abs(L, N));
            CHECK(memcmp(L, L2, sizeof L) == 0, "havoc %s a %g b %g: not deterministic",
                  havoc_params[HAVOC_G_P(0, HAVOC_G_TYPE)].labels[type], (double)a, (double)b);
            if (set == 0)
                CHECK(rms(L, N) > 0.003f, "havoc %s: silent (rms %g)", havoc_params[HAVOC_G_P(0, HAVOC_G_TYPE)].labels[type],
                      (double)rms(L, N));
        }
}

static void test_life_and_ring(void)
{
    /* a pulse 4 times a second, short: the quietest 12 ms are far below the loudest */
    solo(HAVOC_T_TONE, 0.0f, 0.0f);
    setp(HAVOC_G_P(0, HAVOC_G_LIFE), (float)HAVOC_LIFE_PULSE);
    setp(HAVOC_G_P(0, HAVOC_G_LIFE_RATE), 4.0f);
    setp(HAVOC_G_P(0, HAVOC_G_LIFE_SHAPE), 0.2f);
    play(69, L, N);
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i + 600 <= N; i += 600) {
        lo = fminf(lo, rms(L + i, 600));
        hi = fmaxf(hi, rms(L + i, 600));
    }
    CHECK(hi > 0.02f && lo < 0.01f * hi, "havoc pulse life: rms %g at the hit, %g between", (double)hi, (double)lo);

    /* the bleeps: notes and rests (silence between), eight a second */
    solo(HAVOC_T_BLEEP, 0.5f, 0.6f);
    play(69, L, N);
    lo = 1.0f, hi = 0.0f;
    for (int i = 0; i + 600 <= N; i += 600) {
        lo = fminf(lo, rms(L + i, 600));
        hi = fmaxf(hi, rms(L + i, 600));
    }
    CHECK(hi > 0.02f && lo < 0.01f * hi, "havoc bleeps: rms %g in a note, %g in a rest", (double)hi, (double)lo);

    /* generator 4, silent itself, modulates the frequency of generator 1 */
    solo(HAVOC_T_TONE, 0.0f, 0.0f);
    play(69, L2, N);
    solo(HAVOC_T_TONE, 0.0f, 0.0f);
    setp(HAVOC_G_P(3, HAVOC_G_TYPE), (float)HAVOC_T_LOGISTIC);
    setp(HAVOC_G_P(0, HAVOC_G_FM), 0.5f);
    play(69, L, N);
    float diff = 0.0f;
    for (int i = N / 2; i < N; i++)
        diff = fmaxf(diff, fabsf(L[i] - L2[i]));
    CHECK(diff > 0.01f, "havoc fm from the generator before: changes the signal by %g at most", (double)diff);
}

static void test_processors(void)
{
    /* a 3520 Hz tone through a low-pass at 30 * 2^(9.2 * 0.36) = 298 Hz: 3.5 octaves above, at least 30 dB down */
    float level[2];
    for (int on = 0; on < 2; on++) {
        solo(HAVOC_T_TONE, 0.0f, 0.0f);
        setp(HAVOC_X_P(0, HAVOC_X_TYPE), on ? (float)HAVOC_X_LP : (float)HAVOC_X_OFF);
        setp(HAVOC_X_P(0, HAVOC_X_P1), 0.36f);
        setp(HAVOC_X_P(0, HAVOC_X_P2), 0.0f);
        play(105, L, N);
        level[on] = rms(L + N / 2, N / 2);
    }
    CHECK(level[0] > 0.02f && level[1] < 0.03f * level[0], "havoc low-pass: rms %g before, %g after", (double)level[0],
          (double)level[1]);

    /* every processor changes the signal and keeps it bounded */
    solo(HAVOC_T_TONE, 0.5f, 0.0f);
    play(57, L2, N);
    for (int type = 1; type < HAVOC_X_COUNT; type++) {
        solo(HAVOC_T_TONE, 0.5f, 0.0f);
        setp(HAVOC_X_P(1, HAVOC_X_TYPE), (float)type);
        setp(HAVOC_X_P(1, HAVOC_X_P1), 0.4f);
        setp(HAVOC_X_P(1, HAVOC_X_P2), 0.7f);
        play(57, L, N);
        float diff = 0.0f;
        for (int i = N / 2; i < N; i++)
            diff = fmaxf(diff, fabsf(L[i] - L2[i]));
        CHECK(diff > 0.005f && all_finite_bounded(L, N, 1.0f), "havoc processor %s: changes the signal by %g, max %g",
              havoc_params[HAVOC_X_P(0, HAVOC_X_TYPE)].labels[type], (double)diff, (double)max_abs(L, N));
    }
}

static void test_stutter(void)
{
    /* never stuck (p2 = 0): the signal passes untouched; stuck most of the time: a fragment repeats exactly */
    solo(HAVOC_T_WIND, 0.2f, 0.0f);
    play(60, L2, N);
    solo(HAVOC_T_WIND, 0.2f, 0.0f);
    setp(HAVOC_X_P(0, HAVOC_X_TYPE), (float)HAVOC_X_STUTTER);
    setp(HAVOC_X_P(0, HAVOC_X_P2), 0.0f);
    play(60, L, N);
    CHECK(memcmp(L, L2, sizeof L) == 0, "havoc stutter at 0: changes the signal");

    solo(HAVOC_T_WIND, 0.2f, 0.0f);
    setp(HAVOC_X_P(0, HAVOC_X_TYPE), (float)HAVOC_X_STUTTER);
    setp(HAVOC_X_P(0, HAVOC_X_P1), 0.5f);
    setp(HAVOC_X_P(0, HAVOC_X_P2), 0.9f);
    play(60, L, N);
    /* noise never repeats; a stuck fragment does: the best match of a window with itself one fragment later */
    float best = 1.0f;
    for (int at = N / 4; at + 4096 < N; at += 1500)
        for (int lag = 200; lag < 900; lag++) {
            float d = 0.0f, e = 0.0f;
            for (int i = 0; i < 128; i++) {
                d += fabsf(L[at + i] - L[at + i + lag]);
                e += fabsf(L[at + i]);
            }
            if (e > 0.1f)
                best = fminf(best, d / e);
        }
    CHECK(best < 0.05f, "havoc stutter: no repeated fragment found (best mismatch %g)", (double)best);
}

static void test_machine(void)
{
    /* every parameter at its bottom, then its top, with every process in the four generators and every
     * processor in the two slots: finite, bounded, deterministic */
    for (int ext = 0; ext < 2; ext++)
        for (int type = 0; type < HAVOC_T_COUNT; type++)
            for (int run = 0; run < 2; run++) {
                synth_init(synth_default(), SR);
                synth_set_engine(synth_default(), SYNTH_ENGINE_HAVOC);
                for (int id = 0; id < HAVOC_P_COUNT; id++)
                    if (id != HAVOC_P_VOLUME && id != HAVOC_P_ATTACK)
                        synth_set_param(synth_default(), id, ext ? 1.0f : 0.0f);
                for (int g = 0; g < HAVOC_GEN; g++)
                    setp(HAVOC_G_P(g, HAVOC_G_TYPE), (float)((type + g) % HAVOC_T_COUNT));
                for (int x = 0; x < HAVOC_PROC; x++)
                    setp(HAVOC_X_P(x, HAVOC_X_TYPE), (float)((type + 4 * x) % HAVOC_X_COUNT));
                play(ext ? 96 : 24, run ? L2 : L, N / 2);
                if (run) {
                    CHECK(all_finite_bounded(L, N / 2, 1.0f), "havoc extremes %d from process %d: NaN or over 0 dBFS (max %g)",
                          ext, type, (double)max_abs(L, N / 2));
                    CHECK(memcmp(L, L2, sizeof(float) * (N / 2)) == 0, "havoc extremes %d from process %d: not deterministic",
                          ext, type);
                }
            }

    /* the default sound plays, and a release ends in silence */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_HAVOC);
    setp(HAVOC_P_RELEASE, 0.05f);
    play(48, L, N);
    CHECK(rms(L + N / 2, N / 2) > 0.01f && all_finite_bounded(L, N, 1.0f), "havoc default sound: rms %g",
          (double)rms(L + N / 2, N / 2));
    const uint8_t off[3] = {0x80, 48, 0};
    synth_midi(synth_default(), off, 3, 0);
    for (int i = 0; i < N; i += 256)
        synth_render(synth_default(), L + i, R + i, N - i < 256 ? N - i : 256);
    CHECK(max_abs(L + N / 2, N / 2) < 1e-5f, "havoc release: still %g after the note", (double)max_abs(L + N / 2, N / 2));
}

void suite_havoc(void)
{
    test_generators();
    test_life_and_ring();
    test_processors();
    test_stutter();
    test_machine();
}
