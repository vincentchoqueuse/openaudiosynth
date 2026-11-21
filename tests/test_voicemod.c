/* test_voicemod.c — per-voice modulation and what rides on it: comb filter (peaks at the multiples of f, hollow
 * version at the odd multiples of f/2, bounded at full feedback with a fast-moving tuning), just intonation
 * (exact 5/4 third), concert pitch (A4 = 432 Hz), the Fourier LFO shape, MPE (a channel's bend moves only its own note), per-voice LFO (vibrato) and per-voice
 * MSEG (level envelope). */
#include <math.h>
#include <stdlib.h>

#include "engines/sine.h"
#include "filter/comb.h"
#include "global_params.h"
#include "mod/partmod.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 48000
#define NFFT 32768

static float L[N], R[N], X[N], db[NFFT / 2 + 1];

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}
static void render(int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), L + i, R + i, n - i < 256 ? n - i : 256);
}
static void setg(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), v)); }
static void setpart(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(synth_part_param_info(id - SYNTH_PART_PARAM), v)); }
static void setmod(int i, float v) { synth_set_param(synth_default(), SYNTH_MOD_PARAM + i, param_to_norm(synth_mod_param_info(i), v)); }

static float level_at(const float *x, float f)
{
    const int k = (int)(f * NFFT / SR + 0.5f);
    float m = -400.0f;
    for (int d = -2; d <= 2; d++)
        m = fmaxf(m, db[k + d]);
    (void)x;
    return m;
}

static void sine_sustain(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
    synth_set_param(synth_default(), SINE_P_SUSTAIN, 1.0f);
}

void suite_voicemod(void)
{
    /* comb on white noise: peaks at k x 500 Hz, troughs between; hollow: peaks at 250, 750 */
    static comb_t c;
    for (int neg = 0; neg < 2; neg++) {
        uint32_t rng = dsp_seed(3u);
        comb_init(&c, SR);
        for (int i = 0; i < N; i += 32) {
            float blk[32];
            for (int j = 0; j < 32; j++)
                blk[j] = dsp_rand_bipolar(&rng);
            comb_set(&c, 500.0f, neg ? -0.9f : 0.9f, 0.0f);
            comb_process(&c, blk, X + i, 32);
        }
        spectrum_db(X + 8192, NFFT, db);
        const float peak = level_at(X, neg ? 750.0f : 1000.0f), trough = level_at(X, neg ? 1000.0f : 750.0f);
        CHECK(peak - trough > 15.0f, "comb%s: peak %.1f dB, trough %.1f dB", neg ? "_neg" : "", (double)peak, (double)trough);
    }
    comb_init(&c, SR);
    uint32_t rng = dsp_seed(5u);
    for (int i = 0; i < N; i += 32) { /* full feedback, tuning sweeping 3 octaves at 7 Hz */
        float blk[32];
        for (int j = 0; j < 32; j++)
            blk[j] = dsp_rand_bipolar(&rng);
        comb_set(&c, 200.0f * powf(8.0f, 0.5f + 0.5f * sinf(6.2831853f * 7.0f * (float)i / SR)), 0.98f, 0.2f);
        comb_process(&c, blk, X + i, 32);
    }
    CHECK(all_finite_bounded(X, N, 4.0f), "comb: unbounded with full feedback and a moving tuning (max %g)",
          (double)max_abs(X, N));

    /* just intonation from C: E4 is exactly 5/4 of C4 */
    sine_sustain();
    setg(SYNTH_PARAM_TUNING, 1.0f); /* just */
    midi3(0x90, 64, 100);
    render(N);
    float f = estimate_freq(L + 4800, N - 4800, SR);
    const float expect = 261.6256f * 1.25f;
    CHECK(fabsf(f - expect) < 0.05f, "just intonation: E4 = %.3f Hz, expected %.3f", (double)f, (double)expect);

    /* concert pitch: A4 at 432 Hz, in equal temperament and on top of just intonation (E4 = 5/4 of C4 still) */
    for (int just = 0; just < 2; just++) {
        sine_sustain();
        setg(SYNTH_PARAM_A4_HZ, 432.0f);
        setg(SYNTH_PARAM_TUNING, (float)just);
        midi3(0x90, just ? 64 : 69, 100);
        render(N);
        f = estimate_freq(L + 4800, N - 4800, SR);
        const float want = just ? 261.6256f * (432.0f / 440.0f) * 1.25f : 432.0f;
        CHECK(fabsf(f - want) < 0.05f, "a4_hz 432 (%s): %.3f Hz, expected %.3f", just ? "just" : "equal", (double)f,
              (double)want);
    }

    /* MPE: the bend on channel 2 moves only the note on channel 2 (range 2 st: half bend = 1 st) */
    sine_sustain();
    setpart(SYNTH_PART_MPE, 1.0f);
    setpart(SYNTH_PART_MPE_BEND, 2.0f);
    midi3(0x91, 69, 100);       /* A4 on channel 2 */
    midi3(0x92, 76, 100);       /* E5 on channel 3 */
    midi3(0xE1, 0x00, 0x60);    /* channel 2: bend +0.5 */
    render(N);
    spectrum_db(L + 9600, NFFT, db);
    const float a_bent = level_at(L, 466.16f), a_plain = level_at(L, 440.0f), e5 = level_at(L, 659.26f);
    CHECK(a_bent > a_plain + 40.0f && e5 > a_plain + 40.0f, "MPE: 466 Hz %.1f dB, 440 Hz %.1f dB, E5 %.1f dB",
          (double)a_bent, (double)a_plain, (double)e5);

    /* Fourier LFO shape: b1 = 1 is a sine; a2 alone is a cosine at twice the rate; any coefficients stay in -1..1 */
    {
        float c[2 * VM_FOURIER_H] = {0};
        c[1] = 1.0f;
        float err = 0.0f;
        for (int i = 0; i < 64; i++)
            err = fmaxf(err, fabsf(voicemod_fourier(c, (float)i / 64.0f) - fm_sin((float)i / 64.0f)));
        CHECK(err < 1e-6f, "fourier b1 = 1: off a sine by %g", (double)err);
        c[1] = 0.0f, c[2] = 1.0f;
        CHECK(fabsf(voicemod_fourier(c, 0.0f) - 1.0f) < 1e-3f && fabsf(voicemod_fourier(c, 0.25f) + 1.0f) < 1e-3f,
              "fourier a2: %g at 0, %g at 1/4", (double)voicemod_fourier(c, 0.0f), (double)voicemod_fourier(c, 0.25f));
        uint32_t r = dsp_seed(9u);
        float peak = 0.0f;
        for (int t = 0; t < 50; t++) {
            for (int k = 0; k < 2 * VM_FOURIER_H; k++)
                c[k] = dsp_rand_bipolar(&r);
            for (int i = 0; i < 256; i++)
                peak = fmaxf(peak, fabsf(voicemod_fourier(c, (float)i / 256.0f)));
        }
        CHECK(peak <= 1.0001f, "fourier with random coefficients: peak %g", (double)peak);
    }

    /* per-voice LFO on pitch: vibrato of +/- 0.12 st at 5 Hz */
    sine_sustain();
    setmod(PM_VLFO(PM_VLFO_RATE), 5.0f);
    setmod(PM_VROUTE(0, PM_VR_SRC), (float)VM_SRC_LFO);
    setmod(PM_VROUTE(0, PM_VR_DST), (float)VM_PITCH);
    setmod(PM_VROUTE(0, PM_VR_AMOUNT), 0.01f);
    midi3(0x90, 69, 100);
    render(N);
    float lo = 1e9f, hi = 0.0f;
    for (int i = 4800; i + 1920 <= N; i += 960) {
        const float fi = estimate_freq(L + i, 1920, SR);
        lo = fminf(lo, fi), hi = fmaxf(hi, fi);
    }
    CHECK(hi - lo > 3.0f && hi - lo < 9.0f, "per-voice vibrato: %.2f .. %.2f Hz", (double)lo, (double)hi);

    /* per-voice MSEG on level: from -24 dB up to 0 dB over 1 s */
    sine_sustain();
    setmod(PM_VMSEG(PM_MSEG_POINTS), 2.0f);
    setmod(PM_VMSEG(PM_MSEG_SUSTAIN), 0.0f);
    setmod(PM_VMSEG_PT(0, PM_PT_LEVEL), 0.0f);
    setmod(PM_VMSEG_PT(1, PM_PT_LEVEL), 1.0f);
    setmod(PM_VMSEG_PT(1, PM_PT_TIME), 1.0f);
    setmod(PM_VROUTE(0, PM_VR_SRC), (float)VM_SRC_MSEG);
    setmod(PM_VROUTE(0, PM_VR_DST), (float)VM_LEVEL);
    setmod(PM_VROUTE(0, PM_VR_AMOUNT), 1.0f);
    setmod(PM_VROUTE(1, PM_VR_SRC), (float)VM_SRC_NONE);
    synth_set_param(synth_default(), SINE_P_VOLUME, param_to_norm(&sine_params[SINE_P_VOLUME], -24.0f));
    midi3(0x90, 69, 100);
    render(N);
    const float early = rms(L + 2400, 2400), late = rms(L + N - 2400, 2400);
    CHECK(late > 8.0f * early, "per-voice MSEG level: %.4f -> %.4f", (double)early, (double)late);
}
