/* test_drone.c — drone engine: right pitch on a lone partial (and at A4 = 432 Hz), a long attack that arrives at
 * its time, a release that ends, sparkles that come and go, glacial brighter than warm, and every macro at its
 * extreme bounded and deterministic. Pitch by autocorrelation, levels by RMS over windows. */
#include <math.h>
#include <string.h>

#include "engines/drone.h"
#include "global_params.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N (20 * 48000)

static float L[N], R[N], L2[N], db[16385];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&drone_params[id], v)); }
static void setg(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), v)); }
static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}
static void render(float *l, float *r, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, r + i, n - i < 256 ? n - i : 256);
}
static void fresh(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_DRONE);
}
/* one partial, still, nothing else */
static void lone(void)
{
    fresh();
    setp(DRN_P_DENSITY, 0.0f);
    setp(DRN_P_DEPTH, 0.0f);
    setp(DRN_P_MOTION, 0.0f);
    setp(DRN_P_SPARKLE, 0.0f);
    setp(DRN_P_BODY_LEVEL, -60.0f);
    setp(DRN_P_AIR_LEVEL, -60.0f);
    setp(DRN_P_ATTACK, 0.05f);
}

static float centroid(const float *x)
{
    spectrum_db(x, 32768, db);
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= 16384; k++) {
        const double pw = pow(10.0, (double)db[k] / 10.0);
        num += pw * k * ((double)SR / 32768.0);
        den += pw;
    }
    return (float)(num / den);
}

void suite_drone(void)
{
    /* pitch: A4 on a lone partial (its beating twin 0.08 Hz away, a random detune within 2 cents) */
    for (int a432 = 0; a432 < 2; a432++) {
        lone();
        if (a432)
            setg(SYNTH_PARAM_A4_HZ, 432.0f);
        midi3(0x90, 69, 100);
        render(L, R, 2 * 48000);
        const float f = estimate_freq(L + 24000, 48000, SR), want = a432 ? 432.0f : 440.0f;
        CHECK(fabsf(f - want) < 0.6f, "drone pitch: %.2f Hz, expected %.0f", (double)f, (double)want);
    }

    /* attack of 4 s: well below at 1 s, arrived at 4 s (within 1 dB); measured against the same drone with no
     * attack (same seeds: the same beating), so the twin's slow beat cancels out */
    lone();
    midi3(0x90, 69, 100);
    render(L2, R, 5 * 48000);
    lone();
    setp(DRN_P_ATTACK, 4.0f);
    midi3(0x90, 69, 100);
    render(L, R, 5 * 48000);
    const float a1 = rms(L + 48000 - 4800, 9600) / rms(L2 + 48000 - 4800, 9600);
    const float a4 = rms(L + 4 * 48000 - 4800, 9600) / rms(L2 + 4 * 48000 - 4800, 9600);
    CHECK(a1 < 0.5f && a4 > 0.89f && a4 < 1.01f, "drone attack 4 s: %.3f of full at 1 s, %.3f at 4 s", (double)a1,
          (double)a4);

    /* release of 1 s: silent and the voice free 2 s after the key */
    lone();
    setp(DRN_P_RELEASE, 1.0f);
    midi3(0x90, 69, 100);
    render(L, R, 48000);
    midi3(0x80, 69, 0);
    render(L, R, 3 * 48000);
    CHECK(rms(L + 2 * 48000, 48000) < 1e-6f, "drone release: still sounding 2 s after a 1 s release (%g)",
          (double)rms(L + 2 * 48000, 48000));

    /* sparkles alone: some ring out within 20 s at full rate; none at rate 0 */
    for (int on = 0; on < 2; on++) {
        lone();
        setp(DRN_P_PARTIALS_LEVEL, -60.0f);
        setp(DRN_P_SPARKLE, on ? 1.0f : 0.0f);
        midi3(0x90, 57, 100);
        render(L, R, N);
        int rings = 0;
        for (int w = 0; w + 4800 <= N; w += 4800)
            rings += max_abs(L + w, 4800) > 1e-3f;
        CHECK(on ? rings > 5 : rings == 0, "drone sparkle %s: %d windows ringing", on ? "on" : "off", rings);
    }

    /* glacial is brighter than warm (same note, same density) */
    float c[2];
    for (int warm = 0; warm < 2; warm++) {
        fresh();
        setp(DRN_P_TEMPERATURE, warm ? 1.0f : 0.0f);
        setp(DRN_P_SPARKLE, 0.0f);
        setp(DRN_P_ATTACK, 0.05f);
        midi3(0x90, 45, 100);
        render(L, R, 3 * 48000);
        c[warm] = centroid(L + 48000);
    }
    printf("  drone centroid: glacial %.0f Hz, warm %.0f Hz\n", (double)c[0], (double)c[1]);
    CHECK(c[0] > 1.5f * c[1], "drone temperature: glacial centroid %.0f Hz, warm %.0f Hz", (double)c[0], (double)c[1]);

    /* everything at its extreme, a 6-note cluster: finite, bounded, deterministic */
    for (int run = 0; run < 2; run++) {
        fresh();
        for (int id = DRN_P_TEMPERATURE; id <= DRN_P_WIDTH; id++)
            synth_set_param(synth_default(), id, 1.0f);
        for (int id = DRN_P_PARTIALS_LEVEL; id <= DRN_P_SPARKLE_LEVEL; id++)
            setp(id, 6.0f);
        setp(DRN_P_ATTACK, 0.05f);
        for (int k = 0; k < 6; k++)
            midi3(0x90, (uint8_t)(33 + 7 * k), 127);
        render(run ? L2 : L, R, 8 * 48000);
    }
    CHECK(all_finite_bounded(L, 8 * 48000, 1.0f), "drone extremes: NaN or > 0 dBFS (max %g)",
          (double)max_abs(L, 8 * 48000));
    CHECK(memcmp(L, L2, sizeof(float) * 8 * 48000) == 0, "drone extremes: not deterministic");
}
