/* test_tempo.c — tempo module and MIDI clock: internal beat counting, a jittery 128 BPM clock is followed within
 * 0.5 BPM with the beat locked to the ticks, start resets the position, stop holds it, and the tempo-synced
 * delay echoes after one division (measured on the rendered audio). */
#include <math.h>
#include <stdlib.h>

#include "engines/micro.h"
#include "fx/rack.h"
#include "global_params.h"
#include "mod/tempo.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f

static void set_global(int id, float physical)
{
    synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), physical));
}

void suite_tempo(void)
{
    /* internal clock: 120 BPM = 2 beats per second */
    tempo_t t;
    tempo_init(&t, SR, 120.0f);
    for (int i = 0; i < 48000 / 32; i++)
        tempo_advance(&t, 32);
    CHECK(fabsf(t.beat - 2.0f) < 1e-3f, "internal tempo: %.5f beats after 1 s", (double)t.beat);

    /* and it keeps time late in a song: 600 s at 120 BPM = 1200 beats, within a hundredth of a beat (a single
     * float position rounds each block's increment and drifts by several beats) */
    tempo_init(&t, SR, 120.0f);
    for (int i = 0; i < 600 * 48000 / 32; i++)
        tempo_advance(&t, 32);
    CHECK(fabsf(t.beat - 1200.0f) < 0.01f, "internal tempo: %.4f beats after 600 s (1200 expected)", (double)t.beat);

    /* MIDI clock at 128 BPM with +/- 64 samples of jitter */
    tempo_init(&t, SR, 90.0f);
    tempo_set_external(&t, 1);
    tempo_start(&t);
    const double period = 60.0 * (double)SR / (128.0 * 24.0); /* 937.5 samples */
    uint32_t rng = dsp_seed(77u), now = 0;
    int next = 0;
    for (int tick = 0; tick < 24 * 16; tick++) { /* 16 beats */
        const uint32_t at = (uint32_t)(64.0 + tick * period + 64.0 * (double)dsp_rand_bipolar(&rng));
        while (now + 32 <= at) {
            tempo_advance(&t, 32);
            now += 32;
        }
        tempo_clock(&t, at);
        next = tick + 1;
    }
    CHECK(fabsf(t.bpm - 128.0f) < 0.5f, "midi clock: %.2f BPM (128 expected)", (double)t.bpm);
    CHECK(fabsf(t.beat - (float)next / 24.0f) < 1e-4f, "midi clock: beat %.4f, ticks say %.4f", (double)t.beat,
          (double)next / 24.0);
    /* between ticks the position moves on, but never past the next tick */
    const float b0 = t.beat;
    tempo_advance(&t, 400);
    CHECK(t.beat > b0 && t.beat <= b0 + 1.0f / 24.0f + 1e-6f, "midi clock: extrapolation %.5f -> %.5f", (double)b0,
          (double)t.beat);
    tempo_stop(&t);
    const float held = t.beat;
    tempo_advance(&t, 4800);
    CHECK(t.beat == held, "stop: position moved");
    tempo_start(&t);
    CHECK(t.beat == 0.0f, "start: position not reset");

    /* tempo-synced delay: at 100 BPM a 1/4 echo comes 0.6 s after the dry note */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MICRO);
    synth_set_param(synth_default(), MICRO_P_RELEASE, 0.0f);
    set_global(SYNTH_PARAM_TEMPO, 100.0f);
    set_global(SYNTH_PARAM_DELAY_SYNC, 9.0f); /* "1/4" */
    set_global(SYNTH_PARAM_DELAY_MIX, 1.0f);
    set_global(SYNTH_PARAM_DELAY_FEEDBACK, 0.0f);
    set_global(SYNTH_PARAM_DELAY_WOW, 0.0f);
    CHECK(!strcmp(tempo_div_labels[9], "1/4"), "division table: index 9 is %s", tempo_div_labels[9]);
    const int n = 48000;
    float *L = calloc((size_t)n, sizeof(float)), *R = calloc((size_t)n, sizeof(float));
    const uint8_t on[3] = {0x90, 69, 127}, off[3] = {0x80, 69, 0};
    synth_render(synth_default(), L, R, 256); /* let the parameters apply */
    synth_midi(synth_default(), on, 3, 0);
    synth_render(synth_default(), L, R, 960);
    synth_midi(synth_default(), off, 3, 0);
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), L + i, R + i, n - i < 256 ? n - i : 256);
    /* onset of the echo: first 10 ms window after 0.3 s whose level rises above half the dry note's */
    float dry = 0.0f, onset = -1.0f;
    dry = max_abs(L, 2400);
    for (int i = 9600; i + 480 <= n; i += 48)
        if (max_abs(L + i, 48) > 0.3f * dry) {
            onset = (float)i / SR;
            break;
        }
    /* the dry note started 0 s into L (after the 256 + 960 samples already rendered: shift by 960) */
    const float expect = 0.6f - 960.0f / SR;
    printf("  tempo-synced delay 1/4 at 100 BPM: echo at %.3f s (expected %.3f)\n", (double)onset, (double)expect);
    CHECK(fabsf(onset - expect) < 0.01f, "synced delay: echo at %.3f s, expected %.3f", (double)onset, (double)expect);
    free(L);
    free(R);
}
