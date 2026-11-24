/* test_parts.c — multitimbral parts: routing by MIDI channel and key range, transposition (no stuck note when
 * it changes while a key is held), level, mute, balance, effect send, part off. Uses the micro engine (pure sines, known
 * pitches) so each part can be told apart in the spectrum. */
#include <math.h>
#include <stdlib.h>

#include "engines/micro.h"
#include "global_params.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 24000
#define NFFT 16384

static float L[N], R[N], L2[N], R2[N], db[NFFT / 2 + 1];

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}

static void render(float *l, float *r)
{
    for (int i = 0; i < N; i += 256)
        synth_render(synth_default(), l + i, r + i, N - i < 256 ? N - i : 256);
}

static void part_set(int part, int id, float physical)
{
    const param_desc_t *d = id >= SYNTH_PART_PARAM ? synth_part_param_info(id - SYNTH_PART_PARAM) : &micro_params[id];
    synth_part_param(synth_default(), part, id, param_to_norm(d, physical));
}

/* level (dB) of the strongest bin around f in the spectrum of L */
static float level_at(const float *x, float f)
{
    spectrum_db(x + 4000, NFFT, db);
    const int k = (int)(f * NFFT / SR + 0.5f);
    float m = -400.0f;
    for (int d = -3; d <= 3; d++)
        m = fmaxf(m, db[k + d]);
    return m;
}

/* two sine parts: part 0 on channel 1, part 1 on channel 2 */
static void two_parts(void)
{
    synth_init(synth_default(), SR);
    synth_part_engine(synth_default(), 0, SYNTH_ENGINE_MICRO);
    synth_part_engine(synth_default(), 1, SYNTH_ENGINE_MICRO);
    part_set(0, SYNTH_PART_CHANNEL, 1.0f);
    part_set(1, SYNTH_PART_CHANNEL, 2.0f);
    part_set(0, MICRO_P_SUSTAIN, 1.0f);
    part_set(1, MICRO_P_SUSTAIN, 1.0f);
}

void suite_parts(void)
{
    /* channels: each part hears only its own */
    two_parts();
    midi3(0x90, 69, 100); /* channel 1 -> part 0, 440 Hz */
    midi3(0x91, 81, 100); /* channel 2 -> part 1, 880 Hz */
    midi3(0x92, 57, 100); /* channel 3 -> nobody (220 Hz must stay silent) */
    render(L, R);
    const float a440 = level_at(L, 440.0f), a880 = level_at(L, 880.0f), a220 = level_at(L, 220.0f);
    CHECK(a440 > -60.0f && a880 > -60.0f, "parts: 440 Hz %.1f dB, 880 Hz %.1f dB", (double)a440, (double)a880);
    CHECK(a220 < a440 - 80.0f, "parts: channel 3 was played (%.1f dB)", (double)a220);

    /* key split + transpose: part 0 below C4, part 1 from C4 up and one octave higher */
    two_parts();
    for (int k = 0; k < 2; k++)
        part_set(k, SYNTH_PART_CHANNEL, 0.0f);
    part_set(0, SYNTH_PART_KEY_HI, 59.0f);
    part_set(1, SYNTH_PART_KEY_LO, 60.0f);
    part_set(1, SYNTH_PART_TRANSPOSE, 12.0f);
    midi3(0x90, 69, 100); /* A4: part 1 only, an octave up -> 880 Hz, no 440 Hz */
    render(L, R);
    CHECK(level_at(L, 880.0f) > level_at(L, 440.0f) + 80.0f, "split: 440 %.1f dB, 880 %.1f dB",
          (double)level_at(L, 440.0f), (double)level_at(L, 880.0f));

    /* transposition changed while a key is held: the note-off still stops the right note */
    synth_init(synth_default(), SR);
    synth_part_engine(synth_default(), 0, SYNTH_ENGINE_MICRO);
    part_set(0, MICRO_P_SUSTAIN, 1.0f);
    part_set(0, MICRO_P_RELEASE, 0.05f);
    midi3(0x90, 69, 100);
    render(L, R);
    part_set(0, SYNTH_PART_TRANSPOSE, 5.0f);
    midi3(0x80, 69, 0);
    render(L, R);
    render(L, R);
    CHECK(max_abs(L + N / 2, N / 2) < 1e-5f, "transpose while held: stuck note (%g)", (double)max_abs(L + N / 2, N / 2));

    /* level -6 dB halves the amplitude; mute and part off silence it */
    float ref = 0.0f;
    for (int k = 0; k < 4; k++) {
        synth_init(synth_default(), SR);
        synth_part_engine(synth_default(), 0, SYNTH_ENGINE_MICRO);
        part_set(0, MICRO_P_SUSTAIN, 1.0f);
        if (k == 1)
            part_set(0, SYNTH_PART_LEVEL, -6.0206f);
        if (k == 2)
            part_set(0, SYNTH_PART_MUTE, 1.0f);
        if (k == 3)
            synth_part_engine(synth_default(), 0, SYNTH_ENGINE_NONE);
        midi3(0x90, 69, 100);
        render(L, R);
        const float r = rms(L + N / 2, N / 2);
        if (k == 0)
            ref = r;
        else if (k == 1)
            CHECK(fabsf(r / ref - 0.5f) < 0.005f, "part level -6 dB: ratio %.4f", (double)(r / ref));
        else
            CHECK(r < 1e-6f, "part %s: not silent (%g)", k == 2 ? "mute" : "off", (double)r);
    }

    /* balance: full right leaves the left side silent, the right side untouched */
    synth_init(synth_default(), SR);
    synth_part_engine(synth_default(), 0, SYNTH_ENGINE_MICRO);
    part_set(0, MICRO_P_SUSTAIN, 1.0f);
    part_set(0, SYNTH_PART_PAN, 1.0f);
    midi3(0x90, 69, 100);
    render(L, R);
    CHECK(rms(L + N / 2, N / 2) < 1e-6f && rms(R + N / 2, N / 2) > 0.5f * ref, "pan right: L %g R %g",
          (double)rms(L + N / 2, N / 2), (double)rms(R + N / 2, N / 2));

    /* effect send 0: a part bypasses the reverb completely (same as no reverb) */
    for (int k = 0; k < 2; k++) {
        synth_init(synth_default(), SR);
        synth_part_engine(synth_default(), 0, SYNTH_ENGINE_MICRO);
        part_set(0, MICRO_P_SUSTAIN, 1.0f);
        if (k) {
            synth_set_param(synth_default(), SYNTH_PARAM_REVERB_MIX, 0.8f);
            part_set(0, SYNTH_PART_FX_SEND, 0.0f);
        }
        midi3(0x90, 69, 100);
        render(k ? L2 : L, k ? R2 : R);
    }
    float diff = 0.0f;
    for (int i = 0; i < N; i++)
        diff = fmaxf(diff, fabsf(L[i] - L2[i]));
    CHECK(diff < 1e-6f, "fx send 0: differs from no reverb by %g", (double)diff);

    /* eight parts at once: bounded, finite */
    synth_init(synth_default(), SR);
    for (int k = 0; k < SYNTH_PARTS; k++) {
        synth_part_engine(synth_default(), k, (synth_engine_t)(k % SYNTH_ENGINE_COUNT));
        part_set(k, SYNTH_PART_CHANNEL, 0.0f);
        part_set(k, SYNTH_PART_LEVEL, -12.0f);
    }
    for (int note = 36; note < 84; note += 7)
        midi3(0x90, (uint8_t)note, 120);
    render(L, R);
    CHECK(all_finite_bounded(L, N, 1.0f), "8 parts: NaN or > 0 dBFS");
    CHECK(rms(L, N) > 1e-3f, "8 parts: silent");
}
