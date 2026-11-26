/* test_classic.c — mono and va (with what poly had: sync, unison) engines through the public API: pitch, mono glide (always, or tied notes only), hard
 * sync (pitch locks to osc B), unison, the mono's panel (emphasis thinning the bass, note priority, output fed
 * back, pink noise, LFO through the wheel, lo range), and bounded/deterministic output under random parameters. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engines/mono.h"
#include "engines/va.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}

static void render(float *L, float *R, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), L + i, R + i, n - i < 256 ? n - i : 256);
}

static void setp(const param_desc_t *t, int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&t[id], v)); }

static void storm(int engine, int count, float *L, float *R, int n)
{
    uint32_t r = 777u + (uint32_t)engine;
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), (synth_engine_t)engine);
    for (int i = 0; i < n; i += 480) {
        for (int k = 0; k < 3; k++) {
            const uint32_t x = dsp_rand_u32(&r);
            const uint8_t note = (uint8_t)(28 + (x >> 8) % 60);
            if (x % 5 < 2)
                midi3(0x90, note, (uint8_t)(1 + ((x >> 16) & 0x7E)));
            else if (x % 5 == 2)
                midi3(0x80, note, 0);
            else if (x % 5 == 3)
                synth_set_param(synth_default(), (int)((x >> 4) % (uint32_t)count), dsp_rand_unipolar(&r));
            else
                midi3(0xE0, (uint8_t)((x >> 12) & 0x7F), (uint8_t)((x >> 20) & 0x7F));
        }
        render(L + i, R + i, n - i < 480 ? n - i : 480);
    }
}

void suite_classic(void)
{
    const int n = 48000 * 2;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    /* random parameters and notes: bounded and deterministic for each engine */
    static const struct { int engine, count; const char *name; } eng[] = {
        {SYNTH_ENGINE_MONO, MONO_P_COUNT, "mono"}, {SYNTH_ENGINE_VA, VA_P_COUNT, "va"}};
    for (int k = 0; k < 2; k++) {
        storm(eng[k].engine, eng[k].count, L, R, n);
        storm(eng[k].engine, eng[k].count, L2, R2, n);
        CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "%s storm: NaN or > 0 dBFS", eng[k].name);
        CHECK(rms(L, n) > 1e-3f, "%s storm: silent", eng[k].name);
        CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "%s storm: not deterministic", eng[k].name);
    }

    /* mono: pitch of osc 1 alone, open filter */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MONO);
    setp(mono_params, MONO_P_OSC2_LEVEL, 0.0f);
    setp(mono_params, MONO_P_OSC1_WAVE, 0.0f); /* tri */
    setp(mono_params, MONO_P_CUTOFF, 18000.0f);
    setp(mono_params, MONO_P_EMPHASIS, 0.0f);
    setp(mono_params, MONO_P_DRIFT, 0.0f);
    midi3(0x90, 57, 100); /* A3 */
    render(L, R, n);
    float f = estimate_freq(L + 9600, n - 9600, SR);
    CHECK(fabsf(f - 220.0f) < 0.2f, "mono A3: %.2f Hz", (double)f);

    /* mono glide: from A2 to A3 in 0.1 s, legato. Halfway it is between the two; once the time is over, it has
     * arrived (within 1 % of the octave), not still trailing behind; the knob's bottom is no glide at all */
    {
        const int g = 48000 / 4;
        setp(mono_params, MONO_P_GLIDE, 0.1f);
        midi3(0x80, 57, 0);
        midi3(0x90, 45, 100); /* A2 */
        render(L, R, g);
        midi3(0x90, 57, 100); /* A3, A2 still held: legato */
        render(L, R, g);
        const float mid = estimate_freq(L + 960, 1440, SR);  /* 20..50 ms */
        const float end = estimate_freq(L + 5760, 4800, SR); /* 120..220 ms */
        CHECK(mid > 120.0f && mid < 200.0f, "mono glide halfway: %.1f Hz (expected between A2 and A3)", (double)mid);
        CHECK(fabsf(end - 220.0f) < 1.5f, "mono glide has not arrived after its time: %.2f Hz", (double)end);
        setp(mono_params, MONO_P_GLIDE, 0.0f); /* clamped to the knob's bottom: off */
        midi3(0x80, 57, 0);
        midi3(0x80, 45, 0);
        midi3(0x90, 45, 100);
        render(L, R, g);
        midi3(0x90, 57, 100);
        render(L, R, g);
        f = estimate_freq(L + 480, 2400, SR); /* 10..60 ms */
        CHECK(fabsf(f - 220.0f) < 1.0f, "mono glide off still glides: %.2f Hz", (double)f);
        midi3(0x80, 57, 0);
        midi3(0x80, 45, 0);
        /* glide_mode legato: a note played after the last was released starts on pitch; a tied one still glides */
        setp(mono_params, MONO_P_GLIDE, 0.1f);
        setp(mono_params, MONO_P_GLIDE_MODE, 1.0f);
        midi3(0x90, 45, 100);
        render(L, R, g);
        midi3(0x80, 45, 0);
        midi3(0x90, 57, 100); /* detached */
        render(L, R, g);
        f = estimate_freq(L + 480, 2400, SR);
        CHECK(fabsf(f - 220.0f) < 1.0f, "mono glide_mode legato glides a detached note: %.2f Hz", (double)f);
        midi3(0x90, 45, 100); /* A2, A3 still held: tied */
        render(L, R, g);
        const float tied = estimate_freq(L + 960, 1440, SR);
        CHECK(tied > 120.0f && tied < 200.0f, "mono glide_mode legato does not glide a tied note: %.1f Hz", (double)tied);
        setp(mono_params, MONO_P_GLIDE_MODE, 0.0f);
        setp(mono_params, MONO_P_GLIDE, 0.0f);
        midi3(0x80, 57, 0);
        midi3(0x80, 45, 0);
    }

    /* mono, the panel of the original instrument */
    #define MONO_ONE_OSC()                                                                                  \
        do {                                                                                                \
            synth_init(synth_default(), SR);                                                                \
            synth_set_engine(synth_default(), SYNTH_ENGINE_MONO);                                           \
            setp(mono_params, MONO_P_OSC2_LEVEL, 0.0f);                                                     \
            setp(mono_params, MONO_P_DRIFT, 0.0f);                                                          \
            setp(mono_params, MONO_P_CONTOUR, 0.0f);                                                        \
            setp(mono_params, MONO_P_KEYTRACK, 0.0f);                                                       \
            setp(mono_params, MONO_P_AMP_S, 1.0f);                                                          \
        } while (0)
    {   /* emphasis thins the bass: a transistor ladder has no passband compensation */
        float lv[2];
        for (int k = 0; k < 2; k++) {
            MONO_ONE_OSC();
            setp(mono_params, MONO_P_OSC1_WAVE, 0.0f); /* triangle: almost only its fundamental, far below cutoff */
            setp(mono_params, MONO_P_CUTOFF, 4000.0f);
            setp(mono_params, MONO_P_MIX_DRIVE, 0.0f);
            setp(mono_params, MONO_P_EMPHASIS, k ? 0.8f : 0.0f);
            midi3(0x90, 45, 100); /* A2, 110 Hz */
            render(L, R, n);
            lv[k] = 20.0f * log10f(rms(L + n / 2, n / 2));
        }
        CHECK(lv[0] - lv[1] > 8.0f && lv[0] - lv[1] < 16.0f, "mono emphasis 0.8: bass %.1f dB down (8..16 expected)",
              (double)(lv[0] - lv[1]));
    }
    {   /* note priority: low keeps the lowest held key, high the highest, last the last one */
        static const float want[3] = {329.63f, 261.63f, 392.0f}; /* after C4, G4, E4: last = E4, low = C4, high = G4 */
        for (int prio = 0; prio < 3; prio++) {
            MONO_ONE_OSC();
            setp(mono_params, MONO_P_PRIORITY, (float)prio);
            setp(mono_params, MONO_P_CUTOFF, 18000.0f);
            midi3(0x90, 60, 100), midi3(0x90, 67, 100), midi3(0x90, 64, 100);
            render(L, R, n);
            f = estimate_freq(L + 9600, n - 9600, SR);
            CHECK(fabsf(f - want[prio]) < 1.0f, "mono priority %d: %.2f Hz (%.2f expected)", prio, (double)f, (double)want[prio]);
        }
        /* low priority: releasing the sounding key moves to the next lowest, still sounding */
        midi3(0x80, 67, 0); /* priority is "high" here (last loop turn): G4 off -> E4 */
        render(L, R, n);
        f = estimate_freq(L + 9600, n - 9600, SR);
        CHECK(fabsf(f - 329.63f) < 1.0f, "mono priority high, top key released: %.2f Hz (E4 expected)", (double)f);
    }
    {   /* the output fed back into the mixer: bounded, deterministic, and it adds level and harmonics */
        float lv[2];
        for (int k = 0; k < 3; k++) {
            MONO_ONE_OSC();
            setp(mono_params, MONO_P_CUTOFF, 1200.0f);
            setp(mono_params, MONO_P_FEEDBACK, k ? 1.0f : 0.0f);
            setp(mono_params, MONO_P_VOLUME, 6.0f);
            midi3(0x90, 33, 127); /* A1 */
            render(k == 2 ? L2 : L, k == 2 ? R2 : R, n);
            if (k < 2)
                lv[k] = 20.0f * log10f(rms(L + n / 2, n / 2));
            CHECK(all_finite_bounded(k == 2 ? L2 : L, n, 1.0f), "mono feedback %d: NaN or > 0 dBFS", k);
        }
        CHECK(lv[1] - lv[0] > 1.0f, "mono feedback: %.1f dB louder (more than 1 expected)", (double)(lv[1] - lv[0]));
        CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "mono feedback: not deterministic");
    }
    {   /* pink noise: more energy below 500 Hz than white, relative to the top */
        const int N = 32768;
        float *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        float tilt[2];
        for (int k = 0; k < 2; k++) {
            MONO_ONE_OSC();
            setp(mono_params, MONO_P_OSC1_LEVEL, 0.0f);
            setp(mono_params, MONO_P_NOISE_LEVEL, 0.8f);
            setp(mono_params, MONO_P_NOISE_COLOR, (float)k);
            setp(mono_params, MONO_P_CUTOFF, 18000.0f);
            setp(mono_params, MONO_P_MIX_DRIVE, 0.0f);
            midi3(0x90, 60, 100);
            render(L, R, n);
            spectrum_db(L + 9600, N, db);
            double lo = 0.0, hi = 0.0;
            for (int i = (int)(100.0f * N / SR); i < (int)(500.0f * N / SR); i++)
                lo += pow(10.0, (double)db[i] / 10.0);
            for (int i = (int)(4000.0f * N / SR); i < (int)(8000.0f * N / SR); i++)
                hi += pow(10.0, (double)db[i] / 10.0);
            tilt[k] = (float)(10.0 * log10(lo / hi)); /* white: -10 dB (ten times fewer bins); pink: ~0 dB */
        }
        CHECK(tilt[0] < -7.0f && tilt[1] - tilt[0] > 7.0f, "mono noise: white %.1f dB, pink %.1f dB (low band vs high band)",
              (double)tilt[0], (double)tilt[1]);
        free(db);
    }
    {   /* the LFO through the wheel bends the pitch; with the wheel down, nothing moves */
        float span[2];
        for (int k = 0; k < 2; k++) {
            MONO_ONE_OSC();
            setp(mono_params, MONO_P_OSC1_WAVE, 0.0f);
            setp(mono_params, MONO_P_CUTOFF, 18000.0f);
            setp(mono_params, MONO_P_MOD_B, 1.0f);    /* LFO */
            setp(mono_params, MONO_P_MOD_MIX, 1.0f);  /* only source B */
            setp(mono_params, MONO_P_LFO_WAVE, 1.0f); /* square: two steady pitches */
            setp(mono_params, MONO_P_LFO_RATE, 1.0f);
            setp(mono_params, MONO_P_MOD_PITCH, 2.0f);
            midi3(0xB0, 1, k ? 127 : 0);
            midi3(0x90, 57, 100);
            render(L, R, n);
            float lo = 1e9f, hi = 0.0f;
            for (int i = 4800; i + 9600 <= n; i += 9600) { /* 0.2 s windows inside the 0.5 s half periods */
                const float fw = estimate_freq(L + i, 9600, SR);
                lo = fminf(lo, fw), hi = fmaxf(hi, fw);
            }
            span[k] = 1200.0f * log2f(hi / lo);
        }
        CHECK(span[0] < 5.0f && span[1] > 300.0f && span[1] < 420.0f,
              "mono LFO: pitch span %.0f cents with the wheel down, %.0f with it up (400 expected)", (double)span[0], (double)span[1]);
    }
    {   /* osc 3 in its lo range runs at sub-audio rates (the footage of a modulator) */
        MONO_ONE_OSC();
        setp(mono_params, MONO_P_OSC1_LEVEL, 0.0f);
        setp(mono_params, MONO_P_OSC3_LEVEL, 1.0f);
        setp(mono_params, MONO_P_OSC3_RANGE, 5.0f);
        setp(mono_params, MONO_P_OSC3_DETUNE, 0.0f);
        setp(mono_params, MONO_P_OSC3_WAVE, 3.0f); /* square */
        setp(mono_params, MONO_P_CUTOFF, 18000.0f);
        midi3(0x90, 69, 100); /* A4: 440 Hz at 8', 110 at 32', 110 / 32 = 3.44 Hz at lo */
        render(L, R, n);
        /* rising edges, with hysteresis: after each edge the master DC blocker lets the level sag back to zero */
        float pk = 0.0f;
        for (int i = 9600; i < n; i++)
            pk = fmaxf(pk, fabsf(L[i]));
        int cross = 0, low = 1;
        for (int i = 9600; i < n; i++) {
            if (low && L[i] > 0.3f * pk)
                cross++, low = 0;
            else if (L[i] < -0.3f * pk)
                low = 1;
        }
        const float hz = (float)cross / ((float)(n - 9600) / SR);
        CHECK(hz > 2.5f && hz < 4.5f, "mono osc 3 lo range: %.2f Hz (3.44 expected)", (double)hz);
    }

    /* va's sync (what poly had): pitch, then hard sync locks the period to osc 2 even with osc 1 an octave + fifth up */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
    setp(va_params, VA_P_OSC2_LEVEL, 0.0f);
    setp(va_params, VA_P_DRIFT, 0.0f);
    setp(va_params, VA_P_TOLERANCE, 0.0f);
    setp(va_params, VA_P_JITTER, 0.0f);
    setp(va_params, VA_P_OSC2_DETUNE, 0.0f);
    setp(va_params, VA_P_CUTOFF, 18000.0f);
    setp(va_params, VA_P_FENV_AMT, 0.0f);
    setp(va_params, VA_P_AMP_S, 1.0f);
    midi3(0x90, 57, 100);
    render(L, R, n);
    f = estimate_freq(L + 9600, n - 9600, SR);
    CHECK(fabsf(f - 220.0f) < 0.3f, "va A3: %.2f Hz", (double)f);
    midi3(0x80, 57, 0);
    setp(va_params, VA_P_OSC1_SYNC, 1.0f);
    setp(va_params, VA_P_OSC1_OCT, 1.0f);
    setp(va_params, VA_P_OSC1_SEMI, 7.0f);
    render(L, R, n);
    midi3(0x90, 57, 100);
    render(L, R, n);
    {
        const int N = 32768;
        float *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        spectrum_db(L + 9600, N, db);
        /* synced: harmonics of 220 Hz only; the 19-semitone A frequency (660 x 1.0) is a harmonic too,
         * so check there is no energy at a non-multiple of 220 Hz like 330 Hz */
        const float k220 = 220.0f * N / SR, k330 = 330.0f * N / SR;
        float e220 = -300.0f, e330 = -300.0f;
        for (int k = (int)k220 - 3; k <= (int)k220 + 3; k++)
            e220 = fmaxf(e220, db[k]);
        for (int k = (int)k330 - 3; k <= (int)k330 + 3; k++)
            e330 = fmaxf(e330, db[k]);
        CHECK(e220 - e330 > 30.0f, "va sync: 330 Hz only %.1f dB below the 220 Hz fundamental", (double)(e220 - e330));
        CHECK(all_finite_bounded(L, n, 1.0f), "va sync: NaN or > 0 dBFS");
        free(db);
    }

    /* va's unison with a width (what poly's unison gave): one note, wider than a single voice */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
    setp(va_params, VA_P_OSC1_UNISON, 7.0f);
    setp(va_params, VA_P_OSC1_UNI_WIDTH, 1.0f);
    setp(va_params, VA_P_AMP_S, 1.0f);
    midi3(0x90, 45, 100);
    render(L, R, n);
    double lr = 0, ll = 0, rr = 0;
    for (int i = 9600; i < n; i++)
        lr += (double)L[i] * (double)R[i], ll += (double)L[i] * (double)L[i], rr += (double)R[i] * (double)R[i];
    CHECK(lr / sqrt(ll * rr) < 0.95, "va unison: not wide (corr %.3f)", lr / sqrt(ll * rr));
    CHECK(rms(L + 9600, n - 9600) > 0.01f, "va unison: silent");

    free(L);
    free(R);
    free(L2);
    free(R2);
}
