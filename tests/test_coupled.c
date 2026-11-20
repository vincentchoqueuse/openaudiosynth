/* test_coupled.c — coupled-oscillator engine: uncoupled it is an exact additive tone (pitch, partial 3 at 3 f0);
 * phase coupling pulls randomly detuned partials onto the harmonic series; coupling makes the spectrum move
 * more than the same partials uncoupled; freeze (diffusive coupling) extinguishes partials; extreme settings
 * stay finite, bounded and deterministic. */
#include <math.h>
#include <stdlib.h>

#include "engines/coupled.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N (48000 * 6)
#define NFFT 65536

static float L[N], R[N], L2[N], db[NFFT / 2 + 1];

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&coupled_params[id], v)); }

static void play(float *out, int note, int n)
{
    const uint8_t on[3] = {0x90, (uint8_t)note, 110};
    synth_midi(synth_default(), on, 3, 0);
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), out + i, R + i, n - i < 256 ? n - i : 256);
}

static void fresh(float ka, float kp, float beta, float spread)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_COUPLED);
    setp(CPL_P_SPREAD_PAN, 0.0f);
    setp(CPL_P_ATTACK, 0.01f);
    setp(CPL_P_AMP_COUPLING, ka);
    setp(CPL_P_PHASE_COUPLING, kp);
    setp(CPL_P_PITCH_BY_LEVEL, beta);
    setp(CPL_P_SPREAD, spread);
    setp(CPL_P_STRETCH, 0.0f);
}

/* frequency of the strongest peak within +/- 60 cents of target (parabolic interpolation) */
static float peak_near(float target)
{
    const float bin = SR / (float)NFFT;
    const int lo = (int)(target * 0.966f / bin), hi = (int)(target * 1.035f / bin) + 1;
    int k = lo;
    for (int i = lo; i <= hi; i++)
        if (db[i] > db[k])
            k = i;
    const float a = db[k - 1], b = db[k], c = db[k + 1];
    const float off = 0.5f * (a - c) / (a - 2.0f * b + c);
    return ((float)k + off) * bin;
}

static float movement(const float *x, int n)
{
    /* mean over 40 bands (100 Hz .. 4 kHz) of the std of their level over 85 ms frames */
    const int frame = 4096, nf = n / frame;
    double acc = 0.0;
    float *spec = malloc(sizeof(float) * (frame / 2 + 1));
    float *lv = malloc(sizeof(float) * (size_t)nf * 40);
    for (int f = 0; f < nf; f++) {
        spectrum_db(x + f * frame, frame, spec);
        for (int b = 0; b < 40; b++) {
            const int k = (int)(100.0f * powf(40.0f, (float)b / 40.0f) * (float)frame / SR);
            lv[f * 40 + b] = spec[k];
        }
    }
    for (int b = 0; b < 40; b++) {
        double m = 0.0, v = 0.0;
        for (int f = 0; f < nf; f++)
            m += (double)lv[f * 40 + b];
        m /= nf;
        for (int f = 0; f < nf; f++)
            v += ((double)lv[f * 40 + b] - m) * ((double)lv[f * 40 + b] - m);
        acc += sqrt(v / nf);
    }
    free(spec);
    free(lv);
    return (float)(acc / 40.0);
}

static int strong_peaks(void) __attribute__((unused));
static int strong_peaks(void)
{
    float top = -400.0f;
    for (int i = 0; i <= NFFT / 2; i++)
        top = fmaxf(top, db[i]);
    int count = 0;
    for (int i = 2; i < NFFT / 2 - 1; i++)
        if (db[i] > db[i - 1] && db[i] >= db[i + 1] && db[i] > top - 40.0f && i * SR / NFFT > 50.0f)
            count++;
    return count;
}

void suite_coupled(void)
{
    /* uncoupled, no detune: an exact additive tone */
    fresh(0.0f, 0.0f, 0.0f, 0.0f);
    play(L, 57, N / 2); /* A3 = 220 Hz */
    spectrum_db(L + 48000, NFFT, db);
    const float f = peak_near(220.0f);
    CHECK(fabsf(f - 220.0f) < 0.3f, "coupled uncoupled: fundamental at %.3f Hz", (double)f);
    const float p3 = peak_near(660.0f);
    CHECK(fabsf(p3 - 660.0f) < 0.3f, "coupled uncoupled: partial 3 at %.2f Hz", (double)p3);

    /* phase coupling pulls detuned partials onto the harmonic series */
    float dev[2];
    for (int k = 0; k < 2; k++) {
        fresh(0.3f, k ? 3.0f : 0.0f, 0.0f, 40.0f);
        play(L, 45, N); /* A2 = 110 Hz */
        spectrum_db(L + N - NFFT, NFFT, db);
        float sum = 0.0f;
        for (int h = 2; h <= 9; h++)
            sum += fabsf(1200.0f * log2f(peak_near(110.0f * (float)h) / (110.0f * (float)h)));
        dev[k] = sum / 8.0f;
    }
    printf("  coupled: mean deviation from the harmonic series %.1f cents (free) -> %.1f cents (coupled)\n",
           (double)dev[0], (double)dev[1]);
    CHECK(dev[1] < 0.5f * dev[0], "phase coupling did not pull the partials in (%.1f -> %.1f cents)", (double)dev[0],
          (double)dev[1]);

    /* coupling makes the same partials move more */
    float mv[2];
    for (int k = 0; k < 2; k++) {
        fresh(k ? 0.6f : 0.0f, k ? 0.6f : 0.0f, k ? 0.05f : 0.0f, 15.0f);
        play(L, 45, N);
        mv[k] = movement(L + 96000, N - 96000);
    }
    printf("  coupled: spectral movement %.2f dB (free) -> %.2f dB (coupled)\n", (double)mv[0], (double)mv[1]);
    CHECK(mv[1] > 1.5f * mv[0], "coupling did not animate the spectrum (%.2f -> %.2f dB)", (double)mv[0], (double)mv[1]);

    /* freeze: diffusive coupling sinks the partials that cannot lock (about -12 dB) and keeps the few coherent
     * ones: the spread of the partial levels (tilt removed) grows */
    float spread_db[2];
    for (int k = 0; k < 2; k++) {
        fresh(k ? 2.0f : 0.0f, 0.0f, 0.03f, 40.0f); /* the engine's default stretch and pitch_by_level */
        setp(CPL_P_STRETCH, 0.0003f);
        setp(CPL_P_FREEZE, (float)k);
        play(L, 45, N);
        spectrum_db(L + N - NFFT, NFFT, db);
        float lv[16], m = 0.0f, v = 0.0f;
        for (int h = 1; h <= 16; h++) {
            const float fh = peak_near(110.0f * (float)h);
            lv[h - 1] = db[(int)(fh * (float)NFFT / SR + 0.5f)] + 18.0f * log10f((float)h); /* tilt 0.9 removed */
            m += lv[h - 1];
        }
        m /= 16.0f;
        for (int h = 0; h < 16; h++)
            v += (lv[h] - m) * (lv[h] - m);
        spread_db[k] = sqrtf(v / 16.0f);
    }
    printf("  coupled: spread of partial levels %.1f dB (free) -> %.1f dB (frozen)\n", (double)spread_db[0],
           (double)spread_db[1]);
    CHECK(spread_db[1] > spread_db[0] + 10.0f, "freeze did not hollow the spectrum (%.1f -> %.1f dB)", (double)spread_db[0],
          (double)spread_db[1]);

    /* extreme settings: finite, bounded, deterministic */
    static const float cfg[4][6] = {
        /* ka, kp, beta, spread, drift, freeze */
        {2.0f, 3.0f, 0.15f, 100.0f, 60.0f, 0.0f}, {-2.0f, 3.0f, 0.15f, 100.0f, 60.0f, 0.0f},
        {2.0f, 3.0f, 0.15f, 100.0f, 60.0f, 1.0f}, {-2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    for (int c = 0; c < 4; c++) {
        for (int run = 0; run < 2; run++) {
            fresh(cfg[c][0], cfg[c][1], cfg[c][2], cfg[c][3]);
            setp(CPL_P_DRIFT, cfg[c][4]);
            setp(CPL_P_FREEZE, cfg[c][5]);
            setp(CPL_P_PARTIALS, 24.0f);
            setp(CPL_P_REACH, 4.0f);
            setp(CPL_P_TILT, 0.0f);
            const uint8_t notes[4] = {28, 52, 76, 100};
            for (int k = 0; k < 4; k++) {
                const uint8_t on[3] = {0x90, notes[k], 127};
                synth_midi(synth_default(), on, 3, 0);
            }
            float *out = run ? L2 : L;
            for (int i = 0; i < 96000; i += 256)
                synth_render(synth_default(), out + i, R + i, 256);
        }
        CHECK(all_finite_bounded(L, 96000, 1.0f), "coupled config %d: NaN or > 0 dBFS", c);
        CHECK(rms(L + 48000, 48000) > 1e-4f || c == 3, "coupled config %d: silent", c);
        CHECK(memcmp(L, L2, sizeof(float) * 96000) == 0, "coupled config %d: not deterministic", c);
    }
}
