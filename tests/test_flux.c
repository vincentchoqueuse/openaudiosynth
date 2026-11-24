/* test_flux.c — the feedback noise machine. One channel alone: the bare oscillator sits where its bar and the note
 * put it, and the master pitch reshapes the bar (u^(1/8) at +1); clicks into a loop ring at 1 / delay time; the
 * regulator holds a weak loop up; the crush throws aliases above a clean tone, the xor changes it; the ruptures do
 * nothing at rate 0, and at a high rate open holes in the sound. Then the whole machine: every parameter at its extremes stays finite and under
 * 0 dBFS, the same input gives the same output, and a released note falls silent and frees its voice.
 * Reference: the resonances of a feedback comb at k / T (J. O. Smith, Physical Audio Signal Processing). */
#include <math.h>
#include <string.h>

#include "engines/flux.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 65536

static float L[N], R[N], L2[N], db[N / 4 + 1];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&flux_params[id], v)); }
/* a still machine with channel 1 alone: no drift, no cross, no regulator, instant attack */
static void solo(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FLUX);
    setp(FLUX_P_DEPTH, 0.0f);
    setp(FLUX_P_CROSS, 0.0f);
    setp(FLUX_P_NORMALIZE, 0.0f);
    setp(FLUX_P_ATTACK, 0.005f);
    setp(FLUX_P_VELOCITY, 0.0f);
    for (int k = 0; k < FLUX_CH; k++) {
        setp(FLUX_CH_P(FLUX_C_LEVEL, k), k == 0 ? 1.0f : 0.0f);
        setp(FLUX_CH_P(FLUX_C_FLOW, k), 0.0f);
        setp(FLUX_CH_P(FLUX_C_FEEDBACK, k), 0.0f);
    }
}
static void play(int note, float *l, int n)
{
    const uint8_t m[3] = {0x90, (uint8_t)note, 127};
    synth_midi(synth_default(), m, 3, 0);
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, R + i, n - i < 256 ? n - i : 256);
}

static void test_pitch(void)
{
    /* para mode, no feedback: a parabolic sine at note - 60 + 108 u semitones */
    solo();
    setp(FLUX_P_MODE, (float)FLUX_MODE_PARA);
    setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.5f);
    play(69, L, N);
    const float f = estimate_freq(L + N / 2, N / 2, SR), want = 440.0f * powf(2.0f, -6.0f / 12.0f);
    CHECK(fabsf(f - want) < 0.005f * want, "flux bar 0.5 at A4: %.2f Hz, expected %.2f", (double)f, (double)want);

    solo();
    setp(FLUX_P_MODE, (float)FLUX_MODE_PARA);
    setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.5f);
    setp(FLUX_P_MAP_PITCH, 1.0f);
    play(69, L, N);
    const float f1 = estimate_freq(L + N / 2, N / 2, SR);
    const float want1 = 440.0f * powf(2.0f, (-60.0f + 108.0f * powf(0.5f, 0.125f)) / 12.0f);
    CHECK(fabsf(f1 - want1) < 0.01f * want1, "flux master pitch at +1: %.1f Hz, expected %.1f", (double)f1, (double)want1);
}

static void test_loop(void)
{
    /* slow clicks into an open loop: it rings at sr / delay (bar 0.5: 61.44 samples, 781 Hz) */
    solo();
    setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.0f);
    setp(FLUX_CH_P(FLUX_C_PULSE, 0), 0.0f);
    setp(FLUX_CH_P(FLUX_C_CUTOFF, 0), 1.0f);
    setp(FLUX_CH_P(FLUX_C_TIME, 0), 0.5f);
    setp(FLUX_CH_P(FLUX_C_FEEDBACK, 0), 0.95f);
    play(60, L, N);
    spectrum_db(L + N / 2, N / 2, db);
    int best = 0;
    const float bin = SR / (float)(N / 2);
    for (int k = (int)(300.0f / bin); k < (int)(1200.0f / bin); k++)
        if (best == 0 || db[k] > db[best])
            best = k;
    const float fres = (float)best * bin, want = SR / 61.44f;
    CHECK(fabsf(fres - want) < 0.05f * want, "flux loop resonance: %.0f Hz, expected %.0f", (double)fres, (double)want);

    /* the regulator: a weak loop (feedback 0.5) keeps ringing between the clicks; the plain loop dies away */
    float level[2];
    for (int reg = 0; reg < 2; reg++) {
        solo();
        setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.0f);
        setp(FLUX_CH_P(FLUX_C_PULSE, 0), 0.0f);
        setp(FLUX_CH_P(FLUX_C_CUTOFF, 0), 1.0f);
        setp(FLUX_CH_P(FLUX_C_TIME, 0), 0.5f);
        setp(FLUX_CH_P(FLUX_C_FEEDBACK, 0), 0.5f);
        setp(FLUX_P_NORMALIZE, (float)reg);
        play(60, L, N);
        level[reg] = 1.0f; /* the quietest 12 ms of the second half: between two clicks */
        for (int i = N / 2; i + 600 <= N; i += 600)
            level[reg] = fminf(level[reg], rms(L + i, 600));
    }
    CHECK(level[1] > 0.005f && level[0] < 0.001f, "flux regulator: between the clicks, rms %g with it, %g without",
          (double)level[1], (double)level[0]);
}

static void test_machine(void)
{
    /* every parameter at its bottom, then its top, in each mode: finite, bounded, deterministic */
    for (int ext = 0; ext < 2; ext++)
        for (int mode = 0; mode < 3; mode++)
            for (int run = 0; run < 2; run++) {
                synth_init(synth_default(), SR);
                synth_set_engine(synth_default(), SYNTH_ENGINE_FLUX);
                for (int id = 0; id < FLUX_P_COUNT; id++)
                    if (id != FLUX_P_VOLUME && id != FLUX_P_ATTACK)
                        synth_set_param(synth_default(), id, ext ? 1.0f : 0.0f);
                setp(FLUX_P_MODE, (float)mode);
                play(ext ? 96 : 24, run ? L2 : L, N);
                if (run) {
                    CHECK(all_finite_bounded(L, N, 1.0f), "flux extremes %d mode %d: NaN or over 0 dBFS (max %g)", ext, mode,
                          (double)max_abs(L, N));
                    CHECK(memcmp(L, L2, sizeof L) == 0, "flux extremes %d mode %d: not deterministic", ext, mode);
                }
            }

    /* the default sound plays, a second note slides the same voice, a release ends in silence */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FLUX);
    setp(FLUX_P_ATTACK, 0.01f);
    setp(FLUX_P_RELEASE, 0.05f);
    play(48, L, N);
    CHECK(rms(L + N / 2, N / 2) > 0.01f && all_finite_bounded(L, N, 1.0f), "flux default sound: rms %g",
          (double)rms(L + N / 2, N / 2));
    const uint8_t off[3] = {0x80, 48, 0};
    synth_midi(synth_default(), off, 3, 0);
    for (int i = 0; i < N; i += 256)
        synth_render(synth_default(), L + i, R + i, N - i < 256 ? N - i : 256);
    CHECK(max_abs(L + N / 2, N / 2) < 1e-5f, "flux release: still %g after the note", (double)max_abs(L + N / 2, N / 2));
}

/* level of the strongest component above 3 kHz, relative to the strongest of all (dB) */
static float highs_db(const float *x)
{
    spectrum_db(x, N / 2, db);
    const int k0 = (int)(3000.0f / (SR / (float)(N / 2)));
    float top = -200.0f, high = -200.0f;
    for (int k = 1; k < N / 4; k++) {
        top = fmaxf(top, db[k]);
        if (k >= k0)
            high = fmaxf(high, db[k]);
    }
    return high - top;
}

static void test_dirt(void)
{
    /* a clean parabolic sine at 311 Hz has nothing above 3 kHz; resampled near 1 kHz and 4 bits, it does */
    float h[2];
    for (int dirty = 0; dirty < 2; dirty++) {
        solo();
        setp(FLUX_P_MODE, (float)FLUX_MODE_PARA);
        setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.5f);
        setp(FLUX_P_CRUSH, dirty ? 0.8f : 0.0f);
        play(69, L, N);
        h[dirty] = highs_db(L + N / 2);
    }
    CHECK(h[0] < -50.0f && h[1] > -30.0f, "flux crush: above 3 kHz %.0f dB clean, %.0f dB crushed", (double)h[0], (double)h[1]);

    /* the xor with the neighbour: another signal, still bounded */
    solo();
    setp(FLUX_P_MODE, (float)FLUX_MODE_PARA);
    setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.5f);
    play(69, L2, N);
    solo();
    setp(FLUX_P_MODE, (float)FLUX_MODE_PARA);
    setp(FLUX_CH_P(FLUX_C_PITCH, 0), 0.5f);
    setp(FLUX_P_XOR, 1.0f);
    play(69, L, N);
    float diff = 0.0f;
    for (int i = N / 2; i < N; i++)
        diff = fmaxf(diff, fabsf(L[i] - L2[i]));
    CHECK(diff > 0.01f && all_finite_bounded(L, N, 1.0f), "flux xor: changes the signal by %g at most", (double)diff);
}

static void default_machine(float rupture, float violence)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FLUX);
    setp(FLUX_P_ATTACK, 0.005f);
    setp(FLUX_P_RUPTURE, rupture);
    setp(FLUX_P_VIOLENCE, violence);
}
/* the quietest 12 ms of 16 buffers (22 s) of a held note, the first quarter second skipped */
static float quietest(void)
{
    float q = 1.0f;
    for (int pass = 0; pass < 16; pass++) {
        play(48, L, N);
        for (int i = pass ? 0 : 12000; i + 600 <= N; i += 600)
            q = fminf(q, rms(L + i, 600));
    }
    return q;
}

static void test_rupture(void)
{
    /* at rate 0 the size of the ruptures changes nothing */
    default_machine(0.0f, 0.0f);
    play(48, L2, N);
    default_machine(0.0f, 1.0f);
    play(48, L, N);
    CHECK(memcmp(L, L2, sizeof L) == 0, "flux: violence without ruptures changes the sound");

    /* the steady machine never falls silent; with violent ruptures it has holes */
    default_machine(0.0f, 1.0f);
    const float steady = quietest();
    default_machine(1.0f, 1.0f);
    const float broken = quietest();
    CHECK(steady > 0.003f && broken < 0.1f * steady, "flux ruptures: quietest 12 ms %g without, %g with", (double)steady,
          (double)broken);
}

void suite_flux(void)
{
    test_pitch();
    test_loop();
    test_dirt();
    test_rupture();
    test_machine();
}
