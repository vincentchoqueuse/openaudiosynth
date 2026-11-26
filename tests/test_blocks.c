/* test_blocks.c — the shared building blocks that had no direct test: the half-band oversampler (passband flat,
 * images and aliases at least 50 dB down), the resonator (frequency within 1 %, -60 dB at its T60, two modes in a
 * pair exactly as one by one) and the parameter smoother (63 % of a step after one time constant). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/smooth.h"
#include "filter/halfband.h"
#include "filter/resonator.h"
#include "test.h"

#define SR 48000.0f

/* gain (dB) of the 2x round trip (up then down) for a sine of f Hz at SR */
static float hb_roundtrip_db(float f)
{
    hb2x_t h;
    hb2x_reset(&h);
    double e_in = 0.0, e_out = 0.0;
    const int n = 24000, skip = 2000;
    for (int i = 0; i < n; i++) {
        const float x = (float)sin(2.0 * M_PI * (double)f * i / (double)SR);
        float a, b;
        hb2x_up(&h, x, &a, &b);
        const float y = hb2x_down(&h, a, b);
        if (i >= skip + 24) /* the round trip's delay is 23 samples: compare energies, not samples */
            e_in += (double)x * (double)x, e_out += (double)y * (double)y;
    }
    return (float)(10.0 * log10(e_out / e_in));
}

/* level (dB) of what a 2x-rate sine of f2 Hz (at 2 SR) leaves at SR after hb2x_down: above SR / 2 it must go */
static float hb_down_db(float f2)
{
    hb2x_t h;
    hb2x_reset(&h);
    double e = 0.0;
    const int n = 24000, skip = 2000;
    for (int i = 0; i < n; i++) {
        const float ya = (float)sin(2.0 * M_PI * (double)f2 * (2 * i) / (2.0 * (double)SR));
        const float yb = (float)sin(2.0 * M_PI * (double)f2 * (2 * i + 1) / (2.0 * (double)SR));
        const float y = hb2x_down(&h, ya, yb);
        if (i >= skip)
            e += (double)y * (double)y;
    }
    return (float)(10.0 * log10(e / ((n - skip) * 0.5)));
}

static void halfband(void)
{
    for (float f = 100.0f; f <= 16000.0f; f *= 2.0f) {
        const float g = hb_roundtrip_db(f);
        CHECK(fabsf(g) < 0.1f, "half-band round trip at %.0f Hz: %.3f dB (passband must stay flat)", (double)f, (double)g);
    }
    for (float f2 = 30000.0f; f2 <= 46000.0f; f2 += 4000.0f) {
        const float g = hb_down_db(f2);
        CHECK(g < -50.0f, "half-band down: %.0f Hz at 2x leaves %.1f dB (stopband)", (double)f2, (double)g);
    }
}

static void resonator(void)
{
    const float f = 440.0f, t60 = 0.5f;
    reso_t r;
    reso_clear(&r);
    reso_tune(&r, f, t60, SR);
    reso_strike(&r, 1.0f);
    const int n = (int)(t60 * SR);
    float *y = malloc(sizeof(float) * (size_t)n);
    memset(y, 0, sizeof(float) * (size_t)n);
    reso_ring(&r, y, n);
    int up = 0, first = -1, last = -1;
    for (int i = 1; i < n; i++)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f) {
            if (first < 0)
                first = i;
            last = i, up++;
        }
    const float hz = (float)(up - 1) * SR / (float)(last - first);
    CHECK(fabsf(hz / f - 1.0f) < 0.01f, "resonator frequency %.2f Hz for %.0f Hz", (double)hz, (double)f);
    float peak = 0.0f; /* the last cycle's peak against the first's: -60 dB at T60 */
    for (int i = n - (int)(SR / f) - 2; i < n; i++)
        peak = fmaxf(peak, fabsf(y[i]));
    const float db = 20.0f * log10f(peak);
    CHECK(fabsf(db + 60.0f) < 1.0f, "resonator at its T60: %.2f dB (expected -60)", (double)db);
    free(y);

    /* a pair is exactly two modes one after the other, driven or ringing */
    float x[64], o1[64], o2[64];
    for (int i = 0; i < 64; i++)
        x[i] = (float)((i * 37) % 11) / 11.0f - 0.5f;
    reso_t a1, b1, a2, b2;
    reso_clear(&a1), reso_tune(&a1, 300.0f, 1.0f, SR), reso_strike(&a1, 0.3f);
    reso_clear(&b1), reso_tune(&b1, 1234.0f, 0.2f, SR), reso_strike(&b1, -0.2f);
    a2 = a1, b2 = b1;
    memset(o1, 0, sizeof o1), memset(o2, 0, sizeof o2);
    reso_drive2(&a1, x, 0.5f, x + 32, 0.25f, o1, 32), reso_drive2(&b1, x, 0.1f, x + 32, 0.7f, o1, 32);
    reso_drive(&a1, x, 0.5f, o1 + 32, 32), reso_drive(&b1, x, 0.1f, o1 + 32, 32);
    reso_pair(&a2, &b2, x, 0.5f, 0.1f, x + 32, 0.25f, 0.7f, o2, 32);
    reso_pair(&a2, &b2, x, 0.5f, 0.1f, NULL, 0.0f, 0.0f, o2 + 32, 32);
    CHECK(memcmp(o1, o2, sizeof o1) == 0 && a1.zr == a2.zr && b1.zi == b2.zi, "reso_pair differs from two modes one by one");
}

static void smoother(void)
{
    const float tau = 0.005f, rate = SR / 32.0f;
    smooth_t s;
    smooth_init(&s, tau, rate, 0.0f);
    const int steps = (int)lroundf(tau * rate);
    float y = 0.0f;
    for (int i = 0; i < steps; i++)
        y = smooth_next(&s, 1.0f);
    const float expect = 1.0f - expf(-(float)steps / (tau * rate));
    CHECK(fabsf(y - expect) < 0.01f, "smoother after one time constant: %.4f (expected %.4f)", (double)y, (double)expect);
    for (int i = 0; i < 2000; i++)
        y = smooth_next(&s, 1.0f);
    CHECK(fabsf(y - 1.0f) < 1e-5f, "smoother does not settle on its target: %.6f", (double)y);
}

void suite_blocks(void)
{
    halfband();
    resonator();
    smoother();
}
