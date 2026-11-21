/* test_drums.c — drum engine through the public API: every instrument of every kit is finite, bounded
 * and deterministic; kick settles on its pitch; the bass drum plays at its note and slides between held notes; decay scales length; the closed hat chokes the open hat;
 * non-drum notes are silent. */
#include <math.h>
#include <stdlib.h>

#include "engines/drums.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f

static void hit(int note, int vel)
{
    const uint8_t m[3] = {0x90, (uint8_t)note, (uint8_t)vel};
    synth_midi(synth_default(), m, 3, 0);
}

static void render(float *L, float *R, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), L + i, R + i, n - i < 256 ? n - i : 256);
}

static void kit(int k) /* 0 analog, 1 hybrid, 2 glitch, 3 trap */
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_DRUMS);
    synth_set_param(synth_default(), DRUMS_P_KIT, (float)k / 3.0f);
}

/* time (s) at which the 10 ms RMS first falls 40 dB below its maximum */
static float length40(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 0; i + 480 <= n; i += 480)
        m = fmaxf(m, rms(x + i, 480));
    for (int i = 0; i + 480 <= n; i += 480)
        if (rms(x + i, 480) < 0.01f * m && i > 960)
            return (float)i / SR;
    return (float)n / SR;
}

void suite_drums(void)
{
    const int n = 48000 * 3; /* the analog kick boom rings ~1.7 s (T60) */
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    for (int hy = 0; hy < 4; hy++) { /* analog, hybrid, glitch, trap */
        for (int i = 0; i < DR_COUNT; i++) {
            for (int run = 0; run < 2; run++) {
                kit(hy);
                hit(drums_instrument_notes[i], 127);
                render(run ? L2 : L, run ? R2 : R, n);
            }
            CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "%s kit %d: NaN or > 0 dBFS",
                  drums_instrument_names[i], hy);
            CHECK(max_abs(L, n) > 0.05f, "%s kit %d: too quiet (%.3f)", drums_instrument_names[i], hy,
                  (double)max_abs(L, n));
            CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "%s kit %d: not deterministic",
                  drums_instrument_names[i], hy);
            CHECK(rms(L + n - 4800, 4800) < 1e-4f, "%s kit %d: still sounding after 3 s", drums_instrument_names[i], hy);
        }
    }

    /* bass drum: at its note (A1 = 55 Hz); held F1 then A#1 slides from 43.7 Hz up to 58.3 Hz */
    kit(3);
    hit(33, 100);
    render(L, R, 48000);
    float fb = estimate_freq(L + 9600, 9600, SR);
    CHECK(fabsf(fb - 55.0f) < 1.5f, "bass drum A1: %.1f Hz", (double)fb);
    kit(3);
    synth_set_param(synth_default(), DRUMS_P_GLIDE, 0.6f); /* 0.3 s */
    hit(29, 100);
    render(L, R, 12000);
    hit(34, 100); /* F1 still held: a slide */
    render(L, R, 48000);
    const float f_start = estimate_freq(L + 480, 1920, SR), f_end = estimate_freq(L + 24000, 9600, SR);
    CHECK(f_start < 52.0f && fabsf(f_end - 58.27f) < 1.5f, "bass drum slide: %.1f Hz -> %.1f Hz", (double)f_start, (double)f_end);
    kit(3);
    hit(29, 100);
    render(L, R, 12000);
    { const uint8_t off[3] = {0x80, 29, 0}; synth_midi(synth_default(), off, 3, 0); }
    hit(34, 100); /* F1 released: no slide, A#1 at once */
    render(L, R, 48000);
    const float f_jump = estimate_freq(L + 2400, 4800, SR);
    CHECK(fabsf(f_jump - 58.27f) < 2.5f, "bass drum without slide: %.1f Hz", (double)f_jump);

    /* analog kick settles near 49 Hz */
    kit(0);
    hit(36, 100);
    render(L, R, n);
    const float f = estimate_freq(L + 9600, 9600, SR);
    CHECK(fabsf(f - 49.0f) < 2.0f, "analog kick settles at %.1f Hz instead of ~49", (double)f);

    /* decay x2 -> roughly twice as long */
    kit(0);
    hit(38, 100);
    render(L, R, n);
    const float t1 = length40(L, n);
    kit(0);
    synth_set_param(synth_default(), DRUMS_P(DR_SNARE, DRUMS_DECAY), param_to_norm(&drums_params[DRUMS_P(DR_SNARE, DRUMS_DECAY)], 2.0f));
    hit(38, 100);
    render(L, R, n);
    const float t2 = length40(L, n);
    CHECK(t2 > 1.6f * t1 && t2 < 2.4f * t1, "snare decay x2: %.3f s -> %.3f s", (double)t1, (double)t2);

    /* choke: open hat, then closed hat 100 ms later -> the open hat tail is cut */
    kit(0);
    hit(46, 110);
    render(L, R, n);
    const float open_tail = rms(L + 14400, 4800); /* 300-400 ms */
    kit(0);
    hit(46, 110);
    render(L, R, 4800);
    synth_set_param(synth_default(), DRUMS_P(DR_CHAT, DRUMS_LEVEL), 0.0f); /* silent closed hat: only its choke is heard */
    hit(42, 110);
    render(L + 4800, R + 4800, n - 4800);
    CHECK(rms(L + 14400, 4800) < 0.05f * open_tail, "closed hat does not choke the open hat");

    /* notes outside the drum map are silent */
    kit(1);
    hit(90, 127);
    render(L, R, n);
    CHECK(max_abs(L, n) == 0.0f, "non-drum note makes sound");

    free(L);
    free(R);
    free(L2);
    free(R2);
}
