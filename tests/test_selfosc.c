/* test_selfosc.c — self-oscillating engine: low drive = in-tune near-sine; high drive = rich relaxation wave,
 * still in tune (pitch compensation); a detuned oscillator is locked onto the note by entrain; the Duffing term
 * raises the pitch; aliasing of a high relaxation note stays low; the comb passes a note on one of its teeth and
 * hollows it between them; extreme settings bounded and deterministic. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engines/selfosc.h"
#include "osc/selfosc.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 96000
#define NFFT 65536

static float L[N], R[N], L2[N], db[NFFT / 2 + 1];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&selfosc_params[id], v)); }

static void run(float drive, float stiff, float entrain, float detune, int note, float *out)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SELFOSC);
    setp(SO_P_SPREAD, 0.0f);
    setp(SO_P_VEL_DRIVE, 0.0f);
    setp(SO_P_SUSTAIN, 1.0f);
    setp(SO_P_DRIVE, drive);
    setp(SO_P_STIFFNESS, stiff);
    setp(SO_P_ENTRAIN, entrain);
    setp(SO_P_DETUNE, detune);
    const uint8_t on[3] = {0x90, (uint8_t)note, 100};
    synth_midi(synth_default(), on, 3, 0);
    for (int i = 0; i < N; i += 256)
        synth_render(synth_default(), out + i, R + i, N - i < 256 ? N - i : 256);
    spectrum_db(out + N - NFFT, NFFT, db);
}

/* strongest peak within +/- 80 cents of target: frequency (parabolic) and level */
static float peak(float target, float *level)
{
    const float bin = SR / (float)NFFT;
    const int lo = (int)(target * 0.955f / bin), hi = (int)(target * 1.047f / bin) + 1;
    int k = lo;
    for (int i = lo; i <= hi; i++)
        if (db[i] > db[k])
            k = i;
    const float a = db[k - 1], b = db[k], c = db[k + 1];
    if (level)
        *level = b;
    return ((float)k + 0.5f * (a - c) / (a - 2.0f * b + c)) * bin;
}

static float cents(float f, float ref) { return 1200.0f * log2f(f / ref); }

void suite_selfosc(void)
{
    /* the precomputed pitch table is the one its generator computes */
    {
        selfosc_table_t t;
        selfosc_table_init(&t);
        CHECK(memcmp(&t, &selfosc_table, sizeof t) == 0, "selfosc_table differs from selfosc_table_init");
    }
    float l1, l3;
    /* low drive: a near-sine in tune */
    run(0.05f, 0.0f, 0.0f, 0.0f, 69, L);
    float f = peak(440.0f, &l1);
    peak(1320.0f, &l3);
    CHECK(fabsf(cents(f, 440.0f)) < 1.0f, "selfosc low drive: %.2f Hz", (double)f);
    CHECK(l1 - l3 > 30.0f, "selfosc low drive: 3rd harmonic only %.1f dB down", (double)(l1 - l3));

    /* high drive: a relaxation wave, rich, still in tune */
    run(4.0f, 0.0f, 0.0f, 0.0f, 69, L);
    f = peak(440.0f, &l1);
    peak(1320.0f, &l3);
    printf("  selfosc drive 4: %.2f Hz (%.2f cents), 3rd harmonic %.1f dB below the fundamental\n", (double)f,
           (double)cents(f, 440.0f), (double)(l1 - l3));
    /* free (no entrain) the table compensation is within a few cents; entrain locks it exactly (below) */
    CHECK(fabsf(cents(f, 440.0f)) < 5.0f, "selfosc high drive out of tune: %.2f Hz", (double)f);
    CHECK(l1 - l3 < 15.0f, "selfosc high drive: 3rd harmonic %.1f dB down (relaxation expected)", (double)(l1 - l3));

    /* entrain: free, a 40-cent detune sounds detuned; forced, it locks onto the note */
    run(1.0f, 0.0f, 0.0f, 40.0f, 69, L);
    const float free_f = peak(440.0f, NULL);
    run(1.0f, 0.0f, 1.5f, 40.0f, 69, L);
    const float locked = peak(440.0f, NULL);
    printf("  selfosc detune 40 cents: free %.2f cents, entrained %.2f cents\n", (double)cents(free_f, 440.0f),
           (double)cents(locked, 440.0f));
    CHECK(fabsf(cents(free_f, 440.0f) - 40.0f) < 3.0f, "selfosc free: %.2f cents (40 expected)", (double)cents(free_f, 440.0f));
    CHECK(fabsf(cents(locked, 440.0f)) < 1.0f, "selfosc entrain: not locked (%.2f cents)", (double)cents(locked, 440.0f));

    /* the Duffing term raises the pitch (free oscillator) */
    run(1.0f, 1.0f, 0.0f, 0.0f, 69, L);
    const float stiff = peak(470.0f, NULL);
    printf("  selfosc stiffness 1: %+.1f cents\n", (double)cents(stiff, 440.0f));
    CHECK(cents(stiff, 440.0f) > 10.0f, "selfosc stiffness: pitch only %+.1f cents", (double)cents(stiff, 440.0f));

    /* aliasing of a high relaxation note: what is not a harmonic stays far below the fundamental */
    run(5.0f, 0.0f, 1.0f, 0.0f, 93, L); /* A6, 1760 Hz */
    float top = -400.0f, worst = -400.0f;
    for (int k = 20; k < (int)(20000.0f * NFFT / SR); k++) {
        top = fmaxf(top, db[k]);
        const float h = (float)k * SR / (float)NFFT / 1760.0f;
        if (fabsf(h - floorf(h + 0.5f)) * 1760.0f > 30.0f)
            worst = fmaxf(worst, db[k]);
    }
    printf("  selfosc aliasing at 1760 Hz, drive 5: %.1f dB\n", (double)(worst - top));
    CHECK(worst - top < -45.0f, "selfosc aliasing %.1f dB", (double)(worst - top));

    /* comb tuned on 440 Hz (keytrack 0): comb has a tooth there, comb_neg a hole (its teeth at 220, 660 Hz) */
    float lc[2];
    for (int k = 0; k < 2; k++) {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_SELFOSC);
        setp(SO_P_SPREAD, 0.0f);
        setp(SO_P_VEL_DRIVE, 0.0f);
        setp(SO_P_SUSTAIN, 1.0f);
        setp(SO_P_DRIVE, 0.05f);
        setp(SO_P_ENTRAIN, 1.0f);
        setp(SO_P_FILTER, 7.0f + (float)k);
        setp(SO_P_CUTOFF, 440.0f);
        setp(SO_P_KEYTRACK, 0.0f);
        setp(SO_P_RESO, 0.9f);
        setp(SO_P_CHARACTER, 0.0f);
        const uint8_t on[3] = {0x90, 69, 100};
        synth_midi(synth_default(), on, 3, 0);
        for (int i = 0; i < N; i += 256)
            synth_render(synth_default(), L + i, R + i, 256);
        CHECK(all_finite_bounded(L, N, 1.0f), "selfosc comb %d: NaN or > 0 dBFS", k);
        lc[k] = 20.0f * log10f(rms(L + N / 2, N / 2) + 1e-9f);
    }
    CHECK(lc[0] - lc[1] > 12.0f, "selfosc comb: tooth %.1f dB vs hole %.1f dB (12 dB apart expected)", (double)lc[0],
          (double)lc[1]);

    /* extreme settings: finite, bounded, deterministic */
    static const float cfg[6][4] = {{5.0f, 2.0f, 2.0f, 100.0f}, {5.0f, 0.0f, 0.0f, -100.0f}, {0.0f, 2.0f, 2.0f, 0.0f},
                                    {3.0f, 1.0f, 0.2f, 70.0f}, {5.0f, 2.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 0.05f, -30.0f}};
    for (int c = 0; c < 6; c++) {
        for (int pass = 0; pass < 2; pass++) {
            synth_init(synth_default(), SR);
            synth_set_engine(synth_default(), SYNTH_ENGINE_SELFOSC);
            setp(SO_P_DRIVE, cfg[c][0]);
            setp(SO_P_STIFFNESS, cfg[c][1]);
            setp(SO_P_ENTRAIN, cfg[c][2]);
            setp(SO_P_DETUNE, cfg[c][3]);
            setp(SO_P_FILTER, (float)(c + 3)); /* hp, notch, formant, scream, comb, comb_neg */
            setp(SO_P_RESO, 1.0f);
            for (int k = 0; k < 6; k++) {
                const uint8_t on[3] = {0x90, (uint8_t)(24 + 17 * k), 127};
                synth_midi(synth_default(), on, 3, 0);
            }
            float *out = pass ? L2 : L;
            for (int i = 0; i < 48000; i += 256)
                synth_render(synth_default(), out + i, R + i, 256);
        }
        CHECK(all_finite_bounded(L, 48000, 1.0f), "selfosc config %d: NaN or > 0 dBFS", c);
        CHECK(rms(L + 24000, 24000) > 1e-4f, "selfosc config %d: silent", c);
        CHECK(memcmp(L, L2, sizeof(float) * 48000) == 0, "selfosc config %d: not deterministic", c);
    }
}
