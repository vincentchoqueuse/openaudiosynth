/* test_sampler.c — the sample player (osc/splay.h): pitch at several speeds, passband flat, no aliasing when
 * a high sample is played faster, reverse, start/end points, loop and its release, determinism; the kit format
 * (core/kit.h); the sampler engine through the public API: a note plays its sample and another note is
 * silent, a pad's tune moves its pitch, a choke group cuts, a retrigger does not click, removing the kit
 * silences, and every output is finite and bounded. */
#include <math.h>
#include <stdlib.h>

#include "core/kit.h"
#include "engines/sampler.h"
#include "osc/splay.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 48000

static int16_t sine_pcm[N], ramp_pcm[N], noise_pcm[N];

static void make_sine(int16_t *x, int n, float f, float amp)
{
    for (int i = 0; i < n; i++)
        x[i] = (int16_t)(amp * 32767.0f * (float)sin(2.0 * M_PI * (double)f * i / (double)SR));
}

/* plays a whole sample at a constant speed into out (n samples) */
static int play(const int16_t *x, int frames, float rate, float start, float end, int reverse, int loop, float *out,
                int n)
{
    splay_t p;
    splay_start(&p, x, frames, start, end, reverse, loop);
    int last = 0;
    for (int i = 0; i < n; i += 32) {
        if (splay_process(&p, out + i, 32, rate, rate))
            last = i + 32;
    }
    return last; /* samples while active (to the block) */
}

static float db(float x) { return 20.0f * log10f(x + 1e-12f); }

static void suite_player(void)
{
    static float out[2 * N];
    make_sine(sine_pcm, N, 1000.0f, 0.5f);

    /* pitch at speeds 0.5, 1, 2 and a non-integer one */
    const float rates[4] = {0.5f, 1.0f, 2.0f, 1.0594631f};
    for (int k = 0; k < 4; k++) {
        play(sine_pcm, N, rates[k], 0, 1, 0, 0, out, 16384);
        const float f = estimate_freq(out + 2048, 8192, SR), want = 1000.0f * rates[k];
        CHECK(fabsf(f - want) < 0.002f * want, "speed %.3f: %.2f Hz instead of %.2f", (double)rates[k], (double)f, (double)want);
    }
    /* passband: 1 kHz and 15 kHz at speed 1 keep their level (within 0.3 dB) */
    play(sine_pcm, N, 1.0f, 0, 1, 0, 0, out, 16384);
    const float ref = rms(out + 2048, 8192);
    CHECK(fabsf(db(ref) - db(0.5f / sqrtf(2.0f))) < 0.3f, "1 kHz level %.2f dB", (double)(db(ref) - db(0.5f / sqrtf(2.0f))));
    static int16_t high[N];
    make_sine(high, N, 15000.0f, 0.5f);
    play(high, N, 1.0f, 0, 1, 0, 0, out, 16384);
    CHECK(fabsf(db(rms(out + 2048, 8192)) - db(ref)) < 0.3f, "15 kHz at speed 1: %.2f dB",
          (double)(db(rms(out + 2048, 8192)) - db(ref)));
    /* no aliasing: 20 kHz played 1.5x faster would fold to 18 kHz; it must be filtered out */
    make_sine(high, N, 20000.0f, 0.5f);
    play(high, N, 1.5f, 0, 1, 0, 0, out, 16384);
    CHECK(db(rms(out + 2048, 8192)) - db(ref) < -50.0f, "20 kHz at speed 1.5 aliases at %.1f dB",
          (double)(db(rms(out + 2048, 8192)) - db(ref)));
    /* 8 kHz at speed 2 (16 kHz) stays in the band */
    make_sine(high, N, 8000.0f, 0.5f);
    play(high, N, 2.0f, 0, 1, 0, 0, out, 8192);
    CHECK(fabsf(db(rms(out + 1024, 4096)) - db(ref)) < 1.0f, "8 kHz at speed 2: %.2f dB",
          (double)(db(rms(out + 1024, 4096)) - db(ref)));

    /* reverse: a rising ramp comes out falling */
    for (int i = 0; i < N; i++)
        ramp_pcm[i] = (int16_t)(i * 30000 / N);
    const int len = play(ramp_pcm, N, 1.0f, 0, 1, 1, 0, out, N + 64);
    CHECK(out[100] > out[1000] && out[1000] > out[20000] && out[20000] > out[40000], "reverse ramp not falling");
    CHECK(len >= N - 32 && len <= N + 32, "reverse length %d", len);

    /* start and end points: plays (end - start) of the sample, scaled by the speed, and fades at the end */
    const int n1 = play(ramp_pcm, N, 2.0f, 0.25f, 0.75f, 0, 0, out, N);
    CHECK(abs(n1 - N / 4) <= 32, "start/end at speed 2: %d samples instead of %d", n1, N / 4);
    CHECK(fabsf(out[64] - 0.25f * 30000.0f / 32768.0f) < 0.01f, "start point value %f", (double)out[64]);
    int lastnz = n1 + 32; /* the fade is in the block that reports the end */
    while (lastnz > 0 && out[lastnz - 1] == 0.0f)
        lastnz--;
    /* the last output sample is faded (the ramp is at 0.69 there), 40 samples before it is not */
    CHECK(out[lastnz - 1] < 0.05f && out[lastnz - 40] > 0.6f, "no fade before the end point: %f, %f",
          (double)out[lastnz - 1], (double)out[lastnz - 40]);

    /* loop: still playing long after the sample's length; once released, plays to the end and stops */
    {
        splay_t p;
        splay_start(&p, sine_pcm, 4800, 0.0f, 1.0f, 0, 1);
        int on = 1;
        for (int i = 0; i < 4 * N; i += 32)
            on &= splay_process(&p, out, 32, 1.0f, 1.0f);
        CHECK(on, "loop stopped");
        splay_set_loop(&p, 0);
        int blocks = 0;
        while (splay_process(&p, out, 32, 1.0f, 1.0f) && blocks < 1000)
            blocks++;
        CHECK(blocks <= 4800 / 32 + 1, "released loop did not end (%d blocks)", blocks);
    }

    /* determinism, finite and bounded output with a ramping speed */
    static float a[N], b[N];
    for (int i = 0; i < N; i++)
        noise_pcm[i] = (int16_t)((rand() % 65535) - 32767);
    for (int pass = 0; pass < 2; pass++) {
        float *o = pass ? b : a;
        splay_t p;
        splay_start(&p, noise_pcm, N, 0.1f, 0.9f, 0, 1);
        for (int i = 0; i < N; i += 32)
            splay_process(&p, o + i, 32, 0.3f + 3.0f * (float)i / N, 0.3f + 3.0f * (float)(i + 32) / N);
    }
    CHECK(!memcmp(a, b, sizeof a), "not deterministic");
    CHECK(all_finite_bounded(a, N, 2.0f), "unbounded output");
}

/* ---------------- the engine ---------------- */

static uint8_t kit_mem[KIT_HEADER + 4 * KIT_ENTRY + 4 * 2 * N] __attribute__((aligned(4)));
static size_t kit_size;

/* four samples: 1 kHz sine on 36, noise on 38 and 42 (choke group 1 together), nothing on 40 */
static void build_kit(void)
{
    kit_entry_t e[3] = {{"sine", 36, 0, 0, N}, {"open", 38, 0, 0, N}, {"closed", 42, 0, 0, N / 4}};
    uint32_t off = KIT_HEADER + 3 * KIT_ENTRY;
    for (int k = 0; k < 3; k++) {
        e[k].offset = off;
        off += 2 * e[k].frames;
    }
    kit_write_directory(kit_mem, e, 3);
    memcpy(kit_mem + e[0].offset, sine_pcm, 2 * N);
    memcpy(kit_mem + e[1].offset, noise_pcm, 2 * N);
    memcpy(kit_mem + e[2].offset, noise_pcm, N / 2);
    kit_size = off;
}

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

static void set(int id, float value) { synth_set_param(synth_default(), id, param_to_norm(&sampler_params[id], value)); }

static void start(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SAMPLER);
    synth_part_kit(synth_default(), 0, kit_mem, kit_size);
}

static void suite_engine(void)
{
    static float L[N], R[N], L2[N], R2[N];
    build_kit();
    CHECK(kit_count(kit_mem, kit_size) == 3, "test kit invalid");

    /* a note with a sample plays it; one without stays silent */
    start();
    midi3(0x90, 36, 127);
    render(L, R, N / 2);
    CHECK(all_finite_bounded(L, N / 2, 1.0f) && rms(L + 4800, 9600) > 0.05f, "note 36 silent (%f)", (double)rms(L + 4800, 9600));
    CHECK(fabsf(estimate_freq(L + 4800, 8192, SR) - 1000.0f) < 2.0f, "note 36 not at 1 kHz");
    start();
    midi3(0x90, 40, 127);
    render(L, R, 4800);
    CHECK(max_abs(L, 4800) == 0.0f, "note without a sample sounds");

    /* a pad's tune changes its speed: +7 semitones */
    start();
    set(SAMPLER_P(0, SAMPLER_TUNE), 7.0f);
    midi3(0x90, 36, 127);
    render(L, R, N / 2);
    const float want = 1000.0f * powf(2.0f, 7.0f / 12.0f);
    CHECK(fabsf(estimate_freq(L + 4800, 8192, SR) - want) < 0.003f * want, "pad tune: %.1f Hz instead of %.1f",
          (double)estimate_freq(L + 4800, 8192, SR), (double)want);

    /* at another output rate the sample keeps its pitch (kits are 48 kHz) */
    {
        static const float rates[2] = {44100.0f, 96000.0f};
        for (int k = 0; k < 2; k++) {
            synth_init(synth_default(), rates[k]);
            synth_set_engine(synth_default(), SYNTH_ENGINE_SAMPLER);
            synth_part_kit(synth_default(), 0, kit_mem, kit_size);
            midi3(0x90, 36, 127);
            render(L, R, N / 2);
            const float f = estimate_freq(L + 4800, 8192, rates[k]);
            CHECK(fabsf(f - 1000.0f) < 2.0f, "at %.0f Hz the 1 kHz sample plays at %.1f Hz", (double)rates[k], (double)f);
        }
    }

    /* choke: the closed hat (42) stops the open one (38) within ~10 ms */
    start();
    set(SAMPLER_P(1, SAMPLER_CHOKE), 1.0f);
    set(SAMPLER_P(2, SAMPLER_CHOKE), 1.0f);
    midi3(0x90, 38, 100);
    render(L, R, 4800);
    midi3(0x90, 42, 100);
    render(L, R, N / 2);
    start();
    set(SAMPLER_P(1, SAMPLER_CHOKE), 1.0f);
    set(SAMPLER_P(2, SAMPLER_CHOKE), 1.0f);
    render(L2, R2, 4800);
    midi3(0x90, 42, 100);
    render(L2, R2, N / 2);
    float diff = 0.0f;
    for (int i = 9600; i < N / 2; i++) /* after 200 ms: the master's DC blocker has let go of the cut noise */
        diff = fmaxf(diff, fabsf(L[i] - L2[i]));
    CHECK(diff < 1e-4f, "open hat still sounding after the choke (%f)", (double)diff);
    CHECK(rms(L + 14400, 4800) < 1e-4f, "closed hat sample not finished");

    /* retrigger the sine pad 37 times at odd moments: no jump larger than the sine's own slope */
    start();
    float jump = 0.0f, prev = 0.0f;
    for (int k = 0; k < 37; k++) {
        midi3(0x90, 36, 127);
        render(L, R, 1013);
        for (int i = 0; i < 1013; i++) {
            jump = fmaxf(jump, fabsf(L[i] - prev));
            prev = L[i];
        }
    }
    const float slope = 2.0f * (float)M_PI * 1000.0f / SR * 0.5f * 0.5f * 0.7071f; /* sine slope at the output */
    CHECK(jump < 1.6f * slope, "retrigger clicks: jump %f, sine slope %f", (double)jump, (double)slope);

    /* reverse, start/end and loop through the engine: still finite and bounded */
    start();
    set(SAMPLER_P(1, SAMPLER_REVERSE), 1.0f);
    set(SAMPLER_P(1, SAMPLER_LOOP), 1.0f);
    set(SAMPLER_P(1, SAMPLER_START), 0.2f);
    set(SAMPLER_P(1, SAMPLER_END), 0.3f);
    set(SAMPLER_P(1, SAMPLER_CUTOFF), 800.0f);
    midi3(0x90, 38, 127);
    render(L, R, N);
    CHECK(all_finite_bounded(L, N, 1.0f) && rms(L + N - 4800, 4800) > 0.01f, "looping pad stopped or unbounded");
    midi3(0x80, 38, 0);
    render(L, R, N);
    CHECK(rms(L + N - 4800, 4800) < 1e-4f, "looping pad not released");

    /* removing the kit silences at once */
    start();
    midi3(0x90, 36, 127);
    render(L, R, 4800);
    synth_part_kit(synth_default(), 0, NULL, 0);
    render(L, R, 4800);
    /* only the master's DC blocker letting go of the cut sine remains (it decays in ~16 ms) */
    CHECK(max_abs(L + 2400, 2400) < 3e-4f, "sound after the kit was removed (%f)", (double)max_abs(L + 2400, 2400));
    /* a damaged kit is refused (no samples) */
    kit_mem[20 + KIT_HEADER] = 200; /* note 200 */
    CHECK(kit_count(kit_mem, kit_size) < 0, "damaged kit accepted");
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SAMPLER);
    synth_part_kit(synth_default(), 0, kit_mem, kit_size);
    midi3(0x90, 36, 127);
    render(L, R, 4800);
    CHECK(max_abs(L, 4800) == 0.0f, "damaged kit played");
    kit_mem[20 + KIT_HEADER] = 36;
}

void suite_sampler(void)
{
    suite_player();
    suite_engine();
}
