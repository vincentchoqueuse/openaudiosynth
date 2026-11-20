/* wt_factory.c — generators of the built-in wavetables (see wt_factory.h). Each one draws one cycle of the
 * frame at position t (0..1); wt_build band-limits it. Formant values: G. Peterson & H. Barney (1952),
 * average adult vowels; phase-distortion resonance after the published principle of window x sine. */
#include "wt_factory.h"
#include "../core/fastmath.h"

const char *const wt_factory_names[] = {"basic", "pwm", "sync", "harmonics", "vowel", "fold", "growl", "resonant", "digital", NULL};

/* sine -> triangle -> saw -> square, all rising through zero at phase 0 */
static float shape(int k, float x)
{
    switch (k) {
    case 0: return fm_sin(x);
    case 1: return x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f);
    case 2: return 2.0f * fm_frac(x + 0.5f) - 1.0f;
    default: return x < 0.5f ? 1.0f : -1.0f;
    }
}
static void gen_basic(float t, float *o, int n)
{
    const int seg = t >= 1.0f ? 2 : (int)(t * 3.0f);
    const float m = t * 3.0f - (float)seg;
    for (int i = 0; i < n; i++) {
        const float x = (float)i / (float)n;
        o[i] = (1.0f - m) * shape(seg, x) + m * shape(seg + 1, x);
    }
}

static void gen_pwm(float t, float *o, int n)
{
    const float d = 0.5f - 0.46f * t;
    for (int i = 0; i < n; i++)
        o[i] = (float)i < d * (float)n ? 1.0f : -1.0f;
}

static void gen_sync(float t, float *o, int n)
{
    const float r = 1.0f + 7.0f * t;
    for (int i = 0; i < n; i++)
        o[i] = 2.0f * fm_frac((float)i / (float)n * r) - 1.0f;
}

/* adds sine harmonics h with amplitude amp[h] (h = 1..hn) and phase ph[h] (cycles; NULL = 0) */
static void additive(float *o, int n, const float *amp, const float *ph, int hn)
{
    for (int i = 0; i < n; i++)
        o[i] = 0.0f;
    for (int h = 1; h <= hn; h++) {
        if (amp[h] == 0.0f)
            continue;
        const float p0 = ph ? ph[h] : 0.0f;
        for (int i = 0; i < n; i++)
            o[i] += amp[h] * fm_sin(p0 + (float)(h * i % n) / (float)n);
    }
}

static void gen_harmonics(float t, float *o, int n)
{
    float amp[65];
    const float k = 1.0f + 63.0f * t; /* number of harmonics, the last one fading in */
    for (int h = 0; h <= 64; h++)
        amp[h] = h == 0 ? 0.0f : ((float)h <= k ? 1.0f : ((float)h < k + 1.0f ? k + 1.0f - (float)h : 0.0f)) / (float)h;
    additive(o, n, amp, NULL, 64);
}

static void gen_vowel(float t, float *o, int n)
{
    /* formants (Hz) of a, e, i, o, u, laid out for a 130 Hz fundamental */
    static const float F[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
    static const float gain[3] = {1.0f, 0.5f, 0.25f}, bw[3] = {90.0f, 110.0f, 160.0f};
    const int seg = t >= 1.0f ? 3 : (int)(t * 4.0f);
    const float m = t * 4.0f - (float)seg;
    float amp[161];
    amp[0] = 0.0f;
    for (int h = 1; h <= 160; h++) {
        const float f = 130.0f * (float)h;
        float a = 0.0f;
        for (int j = 0; j < 3; j++) {
            const float fj = (1.0f - m) * F[seg][j] + m * F[seg + 1][j];
            const float z = (f - fj) / bw[j];
            a += gain[j] * fm_exp2(-FM_LOG2E * z * z);
        }
        amp[h] = a + 0.02f / (float)h; /* a little glottal tilt keeps the low harmonics alive */
    }
    additive(o, n, amp, NULL, 160);
}

static void gen_fold(float t, float *o, int n)
{
    const float g = 1.0f + 9.0f * t; /* sin(pi/2 g sin(2 pi x)): a sine folded g times */
    for (int i = 0; i < n; i++)
        o[i] = fm_sin(0.25f * g * fm_sin((float)i / (float)n));
}

static void gen_growl(float t, float *o, int n)
{
    const float idx = 6.0f * t * (1.0f / FM_TWO_PI); /* phase deviation in cycles */
    for (int i = 0; i < n; i++) {
        const float x = (float)i / (float)n;
        o[i] = fm_sin(x + idx * fm_sin(2.0f * x) + 0.3f * idx * fm_sin(3.0f * x + 0.25f));
    }
}

static void gen_resonant(float t, float *o, int n)
{
    const float r = 1.0f + 15.0f * t; /* resonance frequency in harmonics, under a decaying window */
    for (int i = 0; i < n; i++) {
        const float x = (float)i / (float)n;
        o[i] = (1.0f - x) * fm_sin(r * x);
    }
}

static void gen_digital(float t, float *o, int n)
{
    float amp[65], ph[65], key[4][65];
    uint32_t rng = dsp_seed(0xD161u);
    for (int h = 0; h <= 64; h++)
        ph[h] = dsp_rand_unipolar(&rng);
    for (int k = 0; k < 4; k++)
        for (int h = 0; h <= 64; h++) {
            const float r = dsp_rand_unipolar(&rng);
            key[k][h] = h == 0 ? 0.0f : r * r * fm_exp2(-0.8f * fm_log2((float)h)); /* sparse, h^-0.8 */
        }
    const int seg = t >= 1.0f ? 2 : (int)(t * 3.0f);
    const float m = t * 3.0f - (float)seg;
    for (int h = 0; h <= 64; h++)
        amp[h] = (1.0f - m) * key[seg][h] + m * key[seg + 1][h];
    additive(o, n, amp, ph, 64);
}

static const wt_gen_fn generators[WT_FACTORY_COUNT] = {gen_basic, gen_pwm, gen_sync, gen_harmonics, gen_vowel,
                                                        gen_fold, gen_growl, gen_resonant, gen_digital};

int wt_factory_build(float *mem, size_t floats)
{
    if (floats < WT_TABLE_FLOATS + WT_SCRATCH_FLOATS)
        return 0;
    int count = (int)((floats - WT_SCRATCH_FLOATS) / WT_TABLE_FLOATS);
    if (count > WT_FACTORY_COUNT)
        count = WT_FACTORY_COUNT;
    float *scratch = mem + (size_t)count * WT_TABLE_FLOATS;
    for (int i = 0; i < count; i++)
        wt_build(mem + (size_t)i * WT_TABLE_FLOATS, generators[i], scratch);
    return count;
}
