/* test_gull.c — seabird call engine: a syllable jumps up by `rise` then slides below the note; a call has `calls`
 * syllables (random 0) then falls silent while the key is still held; looping keeps calling; extreme settings
 * bounded and deterministic. Pitch measured by autocorrelation over short windows. */
#include <math.h>
#include <string.h>

#include "engines/gull.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 192000 /* 4 s */

static float L[N], R[N], L2[N];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&gull_params[id], v)); }

/* a plain bird: no randomness, no roughness, no jitter, no noise */
static void plain(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_GULL);
    setp(GULL_P_SPREAD, 0.0f);
    setp(GULL_P_RANDOM, 0.0f);
    setp(GULL_P_ROUGH, 0.0f);
    setp(GULL_P_JITTER, 0.0f);
    setp(GULL_P_NOISE, 0.0f);
}

static void play(int note, int len)
{
    const uint8_t on[3] = {0x90, (uint8_t)note, 127};
    synth_midi(synth_default(), on, 3, 0);
    for (int i = 0; i < len; i += 256)
        synth_render(synth_default(), L + i, R + i, len - i < 256 ? len - i : 256);
}

/* fundamental (Hz) of x[at .. at + 2048) by normalized autocorrelation, lags for 300 Hz .. 4 kHz */
static float f0_at(const float *x, int at)
{
    const int w = 2048, lo = (int)(SR / 4000.0f), hi = (int)(SR / 300.0f);
    float best = -1.0f;
    int lag = lo;
    for (int k = lo; k <= hi; k++) {
        float c = 0.0f, e0 = 0.0f, e1 = 0.0f;
        for (int i = 0; i < w; i++) {
            c += x[at + i] * x[at + i + k];
            e0 += x[at + i] * x[at + i];
            e1 += x[at + i + k] * x[at + i + k];
        }
        const float r = c / sqrtf(e0 * e1 + 1e-20f);
        if (r > best * 1.02f) { /* the first strong peak wins against its multiples */
            best = r;
            lag = k;
        }
    }
    return SR / (float)lag;
}

/* syllables: bursts of sound separated by silence (50 ms windows) */
static int bursts(const float *x, int n)
{
    int count = 0, on = 0;
    for (int i = 0; i + 2400 <= n; i += 2400) {
        const int loud = rms(x + i, 2400) > 1e-3f;
        if (loud && !on)
            count++;
        on = loud;
    }
    return count;
}

void suite_gull(void)
{
    /* one syllable of 0.5 s, peak at 0.25: up 6 semitones, then 12 below the note at its end */
    plain();
    setp(GULL_P_CALLS, 1.0f);
    setp(GULL_P_SYLLABLE, 0.5f);
    setp(GULL_P_PEAK, 0.25f);
    setp(GULL_P_RISE, 6.0f / 1.3f); /* velocity 127 scales the rise by 1.3 */
    setp(GULL_P_FALL, 12.0f);
    play(81, N); /* A5, 880 Hz */
    const float f_peak = f0_at(L, (int)(0.125f * SR) - 1024), f_late = f0_at(L, (int)(0.4f * SR) - 1024);
    const float st_peak = 12.0f * log2f(f_peak / 880.0f), st_late = 12.0f * log2f(f_late / 880.0f);
    printf("  gull syllable: %+.1f st at the peak, %+.1f st at 80 %%\n", (double)st_peak, (double)st_late);
    CHECK(fabsf(st_peak - 6.0f) < 1.0f, "gull peak at %+.1f st (+6 expected)", (double)st_peak);
    /* at u = 0.8: rise - (rise + fall) w (0.4 + 0.6 w), w = 0.733: 6 - 18 * 0.733 * 0.84 = -5.1 st */
    CHECK(fabsf(st_late + 5.1f) < 1.0f, "gull slide at %+.1f st (-5.1 expected)", (double)st_late);

    /* a call of 4 syllables, then silence while the key is still held */
    plain();
    setp(GULL_P_CALLS, 4.0f);
    setp(GULL_P_SYLLABLE, 0.2f);
    setp(GULL_P_GAP, 0.15f);
    setp(GULL_P_ACCEL, 0.0f);
    play(84, N);
    const int b = bursts(L, N);
    CHECK(b == 4, "gull call: %d syllables (4 expected)", b);
    CHECK(rms(L + (int)(2.5f * SR), (int)(1.5f * SR)) < 1e-5f, "gull call: still sounding after the call");

    /* loop: still calling after 3 s */
    plain();
    setp(GULL_P_CALLS, 2.0f);
    setp(GULL_P_LOOP, 1.0f);
    setp(GULL_P_SYLLABLE, 0.2f);
    play(84, N);
    CHECK(bursts(L + (int)(2.5f * SR), (int)(1.5f * SR)) >= 1, "gull loop: silent after 2.5 s");

    /* a colony at extreme settings: finite, bounded, deterministic */
    for (int c = 0; c < 3; c++) {
        for (int pass = 0; pass < 2; pass++) {
            synth_init(synth_default(), SR);
            synth_set_engine(synth_default(), SYNTH_ENGINE_GULL);
            setp(GULL_P_LOOP, 1.0f);
            setp(GULL_P_RANDOM, 1.0f);
            setp(GULL_P_ROUGH, c == 0 ? 1.0f : 0.5f);
            setp(GULL_P_NOISE, c == 1 ? 1.0f : 0.2f);
            setp(GULL_P_RISE, 12.0f);
            setp(GULL_P_FALL, c == 2 ? 24.0f : 9.0f);
            setp(GULL_P_SYLLABLE, c == 2 ? 0.04f : 0.3f);
            setp(GULL_P_ACCEL, c == 2 ? 0.3f : -0.3f);
            setp(GULL_P_RESO, 1.0f);
            setp(GULL_P_FORMANT, c == 1 ? 4000.0f : 300.0f);
            for (int k = 0; k < 8; k++) {
                const uint8_t on[3] = {0x90, (uint8_t)(40 + 9 * k), 127};
                synth_midi(synth_default(), on, 3, 0);
            }
            float *out = pass ? L2 : L;
            for (int i = 0; i < N; i += 256)
                synth_render(synth_default(), out + i, R + i, 256);
        }
        CHECK(all_finite_bounded(L, N, 1.0f), "gull colony %d: NaN or > 0 dBFS", c);
        CHECK(rms(L + N / 2, N / 2) > 1e-4f, "gull colony %d: silent", c);
        CHECK(memcmp(L, L2, sizeof(float) * N) == 0, "gull colony %d: not deterministic", c);
    }
}
