/* test_fx.c — Dattorro plate reverb: bounded and finite, tail decays and gets longer with decay,
 * decorrelated stereo, deterministic, works at 44.1/48/96 kHz. Delay, chorus, freeze; the voice distortion:
 * bounded, deterministic, level held across drives, no comb filtering at a partial mix, aliasing below a threshold.
 * Phaser and flanger: untouched at mix 0, notches where the theory puts them (a static sweep), feedback moves the
 * resonances (positive: multiples of the delay's frequency, negative: odd multiples of half of it), bounded at full
 * feedback with a fast sweep, deterministic. The rack: the default order named in full is the default, another
 * order sounds different, the same order twice is identical, places naming an effect twice still run every effect
 * once, orders of the six stay bounded. Tape: untouched while off, level kept when clean, wow of the depth it says,
 * highs lost, hiss at its level, saturation that holds the peak, deterministic. Shimmer: the tail of a pure tone gains
 * its octave, bounded at full amount with an endless decay. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fx/chorus.h"
#include "fx/delay.h"
#include "fx/dist.h"
#include "fx/flanger.h"
#include "fx/rack.h"
#include "fx/phaser.h"
#include "fx/reverb.h"
#include "fx/tape.h"
#include "synth.h"
#include "test.h"

/* Index of the largest |x| in [a, b). */
static int argmax_abs(const float *x, int a, int b)
{
    int k = a;
    for (int i = a; i < b; i++)
        if (fabsf(x[i]) > fabsf(x[k]))
            k = i;
    return k;
}

static void test_delay(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 4;
    float *mem = malloc(sizeof(float) * delay_mem_floats(sr, 2.0f));
    float *x = calloc((size_t)n, sizeof(float)), *L = malloc(sizeof(float) * (size_t)n),
          *R = malloc(sizeof(float) * (size_t)n), *L2 = malloc(sizeof(float) * (size_t)n);
    delay_t d;

    /* echo time exact (no wow), right channel at ratio 1.5, repeats decay */
    delay_init(&d, sr, 2.0f, mem);
    delay_set(&d, 0.25f, 1.5f, 0.5f, 20000.0f, 0.0f, 0.0f, 0);
    d.d_cur[0] = d.d_target[0];
    d.d_cur[1] = d.d_target[1];
    x[0] = 0.5f;
    for (int i = 0; i < n; i += 32)
        delay_process(&d, x + i, x + i, L + i, R + i, 32);
    const int e1 = argmax_abs(L, 0, 18000), e2 = argmax_abs(L, 18000, 30000);
    CHECK(abs(e1 - 12000) <= 1, "delay: first echo at %d instead of 12000", e1);
    CHECK(abs(e2 - 24000) <= 2, "delay: second echo at %d instead of 24000", e2);
    CHECK(fabsf(L[e2]) < fabsf(L[e1]), "delay: repeats do not decay");
    const int r1 = argmax_abs(R, 0, 30000);
    CHECK(abs(r1 - 18000) <= 1, "delay: right echo at %d instead of 18000", r1);

    /* ping-pong: first repeat left, second right */
    delay_init(&d, sr, 2.0f, mem);
    delay_set(&d, 0.25f, 1.0f, 0.6f, 20000.0f, 0.0f, 0.0f, 1);
    for (int i = 0; i < n; i += 32)
        delay_process(&d, x + i, x + i, L + i, R + i, 32);
    CHECK(fabsf(L[argmax_abs(L, 6000, 18000)]) > 10.0f * fabsf(R[argmax_abs(R, 6000, 18000)]),
          "ping-pong: first repeat not on the left");
    CHECK(fabsf(R[argmax_abs(R, 18000, 30000)]) > 10.0f * fabsf(L[argmax_abs(L, 18000, 30000)]),
          "ping-pong: second repeat not on the right");

    /* worst case: max feedback, max drive and wow, noise in, time sweeps: bounded and deterministic */
    for (int run = 0; run < 2; run++) {
        uint32_t r = 77u;
        delay_init(&d, sr, 2.0f, mem);
        for (int i = 0; i < n; i += 32) {
            float in[32];
            for (int j = 0; j < 32; j++)
                in[j] = i < n / 4 ? dsp_rand_bipolar(&r) : 0.0f;
            delay_set(&d, 0.05f + 0.5f * (float)(i % 48000) / 48000.0f, 1.3f, 0.98f, 3000.0f, 1.0f, 1.0f, i > n / 2);
            delay_process(&d, in, in, (run ? L2 : L) + i, R + i, 32);
        }
    }
    CHECK(all_finite_bounded(L, n, 4.0f), "delay worst case: NaN or |y| = %.2f", (double)max_abs(L, n));
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "delay not deterministic");
    free(mem);
    free(x);
    free(L);
    free(R);
    free(L2);
}

static void test_chorus(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 2;
    float *x = malloc(sizeof(float) * (size_t)n), *L = malloc(sizeof(float) * (size_t)n),
          *R = malloc(sizeof(float) * (size_t)n);
    uint32_t r = 3u;
    for (int i = 0; i < n; i++)
        x[i] = 0.5f * dsp_rand_bipolar(&r);
    chorus_t *c = malloc(sizeof(chorus_t));

    /* off: exact passthrough */
    chorus_init(c, sr);
    for (int i = 0; i < n; i += 32)
        chorus_process(c, x + i, x + i, L + i, R + i, 32);
    CHECK(memcmp(L, x, sizeof(float) * (size_t)n) == 0, "chorus off is not a passthrough");

    for (int m = CHORUS_I; m < CHORUS_MODE_COUNT; m++) {
        chorus_init(c, sr);
        chorus_set(c, (chorus_mode_t)m, 1.0f);
        for (int i = 0; i < n; i += 32)
            chorus_process(c, x + i, x + i, L + i, R + i, 32);
        CHECK(all_finite_bounded(L, n, 2.0f) && all_finite_bounded(R, n, 2.0f), "chorus mode %d: NaN or overshoot", m);
        double lr = 0, ll = 0, rr = 0;
        for (int i = 4800; i < n; i++) {
            lr += (double)L[i] * (double)R[i];
            ll += (double)L[i] * (double)L[i];
            rr += (double)R[i] * (double)R[i];
        }
        const double corr = lr / sqrt(ll * rr);
        CHECK(corr < 0.98, "chorus mode %d: no stereo width (corr %.3f)", m, corr);
    }
    free(c);
    free(x);
    free(L);
    free(R);
}

static void test_freeze(void)
{
    const float sr = 48000.0f;
    reverb_t r;
    float *mem = malloc(sizeof(float) * reverb_mem_floats(sr, 1.0f));
    reverb_init(&r, sr, 1.0f, mem);
    reverb_set(&r, 0.5f, 6000.0f, 0.0f);
    float in[32], oL[32], oR[32];
    uint32_t s = 9u;
    float e_before = 0.0f, e_frozen = 0.0f, e_after = 0.0f;
    for (int b = 0; b < 1500 * 16; b++) { /* 16 s */
        for (int j = 0; j < 32; j++)
            in[j] = b < 1500 ? 0.3f * dsp_rand_bipolar(&s) : (b < 1500 * 13 ? 0.3f * dsp_rand_bipolar(&s) : 0.0f);
        if (b == 1500)
            reverb_set_freeze(&r, 1); /* input keeps playing but must not enter */
        if (b == 1500 * 13)
            reverb_set_freeze(&r, 0);
        reverb_process(&r, in, oL, oR, 32);
        const float e = rms(oL, 32);
        if (b == 1499)
            e_before = e;
        if (b > 1500 * 12 && b < 1500 * 13)
            e_frozen = fmaxf(e_frozen, e);
        if (b == 1500 * 16 - 1)
            e_after = e;
    }
    printf("  freeze: level before %.4f, after 11 s frozen %.4f, 3 s after release %.6f\n", (double)e_before,
           (double)e_frozen, (double)e_after);
    CHECK(e_frozen > 0.2f * e_before, "freeze does not hold the tail");
    CHECK(e_frozen < 3.0f * e_before, "freeze lets the input in or grows");
    CHECK(e_after < 0.01f * e_before, "tail does not decay after unfreeze");
    free(mem);
}

/* Impulse response; returns the time (s) for the smoothed energy to fall 60 dB below its maximum. */
static float impulse(float sr, float decay, float *L, float *R, int n)
{
    reverb_t r;
    float *mem = malloc(sizeof(float) * reverb_mem_floats(sr, 1.0f));
    reverb_init(&r, sr, 1.0f, mem);
    reverb_set(&r, decay, 8000.0f, 0.0f);
    float *in = calloc((size_t)n, sizeof(float));
    in[0] = 1.0f;
    for (int i = 0; i < n; i += 32)
        reverb_process(&r, in + i, L + i, R + i, 32);
    free(in);
    free(mem);
    /* energy in 10 ms windows */
    const int w = (int)(0.01f * sr);
    float emax = 0.0f, t60 = -1.0f;
    for (int i = 0; i + w <= n; i += w)
        emax = fmaxf(emax, rms(L + i, w));
    for (int i = 0; i + w <= n; i += w)
        if (rms(L + i, w) > emax * 0.001f)
            t60 = (float)(i + w) / sr;
    return t60;
}


/* Worst line (dB re the fundamental at bin k0) outside +/-4 bins of the harmonics, below bin kmax. */
static float dist_alias_db(const float *db, int k0, int kmax)
{
    float worst = -300.0f;
    for (int k = 5; k < kmax; k++) {
        const int r = k % k0;
        if (r <= 4 || r >= k0 - 4)
            continue;
        if (db[k] > worst)
            worst = db[k];
    }
    return worst - db[k0];
}

static void test_dist(void)
{
    const float sr = 48000.0f;
    const int n = 16384, k0 = 509; /* 1491 Hz on a bin (not a divisor of the rate: aliases fall between harmonics) */
    float *x = malloc(sizeof(float) * (size_t)n), *y = malloc(sizeof(float) * (size_t)n), *y2 = malloc(sizeof(float) * (size_t)n);
    float *db = malloc(sizeof(float) * (size_t)(n / 2 + 1));
    static const char *const names[] = {"off", "soft", "tube", "hard", "fold"};
    dist_t d;
    for (int i = 0; i < n; i++)
        x[i] = 0.7f * sinf(6.28318530717959f * (float)k0 * (float)i / (float)n);
    for (int mode = DIST_SOFT; mode < DIST_MODE_COUNT; mode++) {
        float lo = 1e9f, hi = 0.0f;
        for (int k = 0; k <= 4; k++) {
            const float drive = 9.0f * (float)k; /* 0, 9, 18, 27, 36 dB */
            dist_init(&d, sr);
            dist_set(&d, (dist_mode_t)mode, drive, 20000.0f, 1.0f);
            for (int i = 0; i < n; i += 32)
                dist_process(&d, x + i, y + i, 32);
            CHECK(all_finite_bounded(y, n, 2.0f), "dist %s %.0f dB: NaN or |y| > 2", names[mode], (double)drive);
            const float r = rms(y + 1024, n - 1024);
            lo = fminf(lo, r), hi = fmaxf(hi, r);
            /* determinism */
            dist_init(&d, sr);
            dist_set(&d, (dist_mode_t)mode, drive, 20000.0f, 1.0f);
            for (int i = 0; i < n; i += 32)
                dist_process(&d, x + i, y2 + i, 32);
            CHECK(memcmp(y, y2, sizeof(float) * (size_t)n) == 0, "dist %s: not deterministic", names[mode]);
            if (k == 2) { /* 18 dB: aliases below 10 kHz */
                spectrum_db(y, n, db);
                const float al = dist_alias_db(db, k0, (int)(10000.0f * (float)n / sr));
                printf("  dist %-4s 18 dB on 1491 Hz: worst alias below 10 kHz %.1f dB\n", names[mode], (double)al);
                CHECK(al < -50.0f, "dist %s: alias at %.1f dB", names[mode], (double)al);
            }
        }
        const float spread = 20.0f * log10f(hi / lo);
        printf("  dist %-4s level over 0..36 dB of drive: %.1f dB spread\n", names[mode], (double)spread);
        CHECK(spread < 8.0f, "dist %s: level moves %.1f dB with the drive", names[mode], (double)spread);
    }
    /* a partial mix at a low drive is the dry signal (no comb notch): a 9 kHz sine keeps its level */
    for (int i = 0; i < n; i++)
        x[i] = 0.05f * sinf(6.28318530717959f * 9000.0f * (float)i / sr);
    dist_init(&d, sr);
    dist_set(&d, DIST_SOFT, 0.0f, 20000.0f, 0.5f);
    for (int i = 0; i < n; i += 32)
        dist_process(&d, x + i, y + i, 32);
    const float ratio = 20.0f * log10f(rms(y + 1024, n - 1024) / rms(x + 1024, n - 1024));
    printf("  dist mix 0.5 at 9 kHz: %.2f dB\n", (double)ratio);
    CHECK(fabsf(ratio) < 1.5f, "dist: a partial mix notches 9 kHz (%.2f dB)", (double)ratio);
    free(x), free(y), free(y2), free(db);
}

/* gain in dB of a sine at f through a phaser or a flanger (static settings), after 0.2 s */
static float modfx_gain(int flanger, float f, float a, float b, float c, float d, float e, int stages)
{
    static phaser_t ph;
    static flanger_t fl;
    const float sr = 48000.0f;
    float x[32], y[32];
    double in = 0, out = 0;
    if (flanger) {
        flanger_init(&fl, sr);
        flanger_set(&fl, a, b, c, d, e);
    } else {
        phaser_init(&ph, sr);
        phaser_set(&ph, a, b, c, d, e, stages);
    }
    for (int i = 0; i < 48000; i += 32) {
        for (int j = 0; j < 32; j++)
            x[j] = y[j] = 0.1f * sinf(6.28318530717959f * f * (float)(i + j) / sr);
        if (flanger)
            flanger_process(&fl, y, x, 32);
        else
            phaser_process(&ph, y, x, 32);
        for (int j = 0; j < 32 && i >= 9600; j++) {
            const float s = 0.1f * sinf(6.28318530717959f * f * (float)(i + j) / sr);
            in += (double)s * (double)s, out += (double)y[j] * (double)y[j];
        }
    }
    return (float)(10.0 * log10(out / in));
}

static void test_modfx(void)
{
    const float sr = 48000.0f;
    /* phaser, 4 stages at 1 kHz, no sweep, no feedback: notches at fc tan(22.5) and fc tan(67.5), full level at fc */
    const float n1 = modfx_gain(0, 414.2f, 1.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 4);
    const float n2 = modfx_gain(0, 2414.2f, 1.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 4);
    const float pk = modfx_gain(0, 1000.0f, 1.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 4);
    printf("  phaser 4 stages at 1 kHz: %.1f dB at 414 Hz, %.1f dB at 2414 Hz, %.1f dB at 1 kHz\n", (double)n1, (double)n2, (double)pk);
    CHECK(n1 < -30.0f && n2 < -30.0f && fabsf(pk) < 0.5f, "phaser notches: %.1f, %.1f dB; %.1f dB at fc", (double)n1, (double)n2, (double)pk);
    /* flanger, 1 ms, no sweep, no feedback: a notch at 500 Hz, full level at 1 kHz */
    const float fn = modfx_gain(1, 500.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0);
    const float fp = modfx_gain(1, 1000.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0);
    printf("  flanger 1 ms: %.1f dB at 500 Hz, %.1f dB at 1 kHz\n", (double)fn, (double)fp);
    CHECK(fn < -30.0f && fabsf(fp) < 0.5f, "flanger notch %.1f dB, peak %.1f dB", (double)fn, (double)fp);
    /* feedback: positive rings at 1 kHz, negative (hollow) at 500 Hz */
    const float pos = modfx_gain(1, 1000.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.9f, 0) - modfx_gain(1, 500.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.9f, 0);
    const float neg = modfx_gain(1, 500.0f, 1.0f, 0.0f, 1.0f, 0.0f, -0.9f, 0) - modfx_gain(1, 1000.0f, 1.0f, 0.0f, 1.0f, 0.0f, -0.9f, 0);
    printf("  flanger feedback +0.9: 1 kHz over 500 Hz by %.1f dB; -0.9: 500 Hz over 1 kHz by %.1f dB\n", (double)pos, (double)neg);
    CHECK(pos > 6.0f && neg > 6.0f, "flanger feedback: +0.9 favours 1 kHz by %.1f dB, -0.9 favours 500 Hz by %.1f dB", (double)pos, (double)neg);

    /* off: untouched; full feedback and a fast sweep on loud noise: bounded, deterministic */
    static phaser_t ph;
    static flanger_t fl;
    const int n = 48000 * 2;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n), *L2 = malloc(sizeof(float) * (size_t)n);
    for (int run = 0; run < 3; run++) {
        uint32_t s = dsp_seed(11u);
        for (int i = 0; i < n; i++)
            L[i] = R[i] = dsp_rand_bipolar(&s);
        if (run == 0)
            memcpy(L2, L, sizeof(float) * (size_t)n);
        phaser_init(&ph, sr);
        flanger_init(&fl, sr);
        if (run) {
            phaser_set(&ph, 1.0f, 8.0f, 2000.0f, 1.0f, 0.9f, 12);
            flanger_set(&fl, 1.0f, 8.0f, 10.0f, 1.0f, -0.9f);
        }
        for (int i = 0; i < n; i += 32) {
            phaser_process(&ph, L + i, R + i, 32);
            flanger_process(&fl, L + i, R + i, 32);
        }
        if (run == 0)
            CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "phaser and flanger at mix 0 change the signal");
        else if (run == 1) {
            CHECK(all_finite_bounded(L, n, 8.0f) && all_finite_bounded(R, n, 8.0f), "phaser + flanger at full feedback: max %g",
                  (double)fmaxf(max_abs(L, n), max_abs(R, n)));
            memcpy(L2, L, sizeof(float) * (size_t)n);
        } else
            CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "phaser + flanger not deterministic");
    }
    free(L), free(R), free(L2);
}

/* One note of the default instrument through a phaser, a delay and a reverb, the rack's places as given. */
static void rack_render(const int *slots, float *L, float *R, int n)
{
    synth_t *s = synth_default();
    synth_init(s, 48000.0f);
    synth_set_engine(s, SYNTH_ENGINE_VA);
    synth_set_param(s, SYNTH_PARAM_PHASER_MIX, 1.0f);
    synth_set_param(s, SYNTH_PARAM_DELAY_MIX, 0.5f);
    synth_set_param(s, SYNTH_PARAM_REVERB_MIX, 0.4f);
    synth_set_param(s, SYNTH_PARAM_FLANGER_MIX, 0.6f);
    synth_set_param(s, SYNTH_PARAM_CHORUS_MODE, 0.5f);
    synth_set_param(s, SYNTH_PARAM_TAPE, 1.0f);
    if (slots)
        for (int k = 0; k < 6; k++)
            synth_set_param(s, SYNTH_PARAM_FX_SLOT_1 + k, (float)slots[k] / 5.0f);
    const uint8_t on[3] = {0x90, 57, 100}, off[3] = {0x80, 57, 0};
    synth_midi(s, on, 3, 0);
    for (int i = 0; i < n; i += 64) {
        if (i == 24000 - 24000 % 64)
            synth_midi(s, off, 3, 0);
        synth_render(s, L + i, R + i, n - i < 64 ? n - i : 64);
    }
}

static void test_rack(void)
{
    const int n = 48000 * 2;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);
    const size_t bytes = sizeof(float) * (size_t)n;
    static const int usual[6] = {0, 1, 2, 3, 4, 5};    /* phaser, flanger, chorus, delay, reverb, tape */
    static const int reversed[6] = {5, 4, 3, 2, 1, 0}; /* the reverb before everything but the tape */
    static const int twice[6] = {4, 4, 4, 3, 3, 3};    /* reverb, delay; those not named go before their usual follower */
    static const int same[6] = {4, 0, 1, 2, 3, 5};
    static const int front[6] = {5, 1, 2, 3, 4, 5};    /* only the first place renamed: the tape first, the rest as usual */
    static const int moved[6] = {5, 0, 1, 2, 3, 4};

    rack_render(NULL, L, R, n);
    rack_render(usual, L2, R2, n);
    CHECK(memcmp(L, L2, bytes) == 0 && memcmp(R, R2, bytes) == 0, "rack: the usual order named in full is not the default");
    CHECK(max_abs(L, n) > 0.05f, "rack: silent (max %g)", (double)max_abs(L, n));

    rack_render(reversed, L2, R2, n);
    double diff = 0.0, ref = 0.0;
    for (int i = 0; i < n; i++)
        diff += (double)(L[i] - L2[i]) * (double)(L[i] - L2[i]), ref += (double)L[i] * (double)L[i];
    printf("  rack reversed against the usual order: difference %.1f dB of the signal\n", 10.0 * log10(diff / ref));
    CHECK(diff > 0.01 * ref, "rack: the reversed order sounds the same (difference %.1f dB)", 10.0 * log10(diff / ref));
    rack_render(reversed, L, R, n);
    CHECK(memcmp(L, L2, bytes) == 0 && memcmp(R, R2, bytes) == 0, "rack: an order is not deterministic");

    rack_render(twice, L, R, n);
    rack_render(same, L2, R2, n);
    CHECK(memcmp(L, L2, bytes) == 0 && memcmp(R, R2, bytes) == 0, "rack: places naming an effect twice do not run every effect once");
    rack_render(front, L, R, n);
    rack_render(moved, L2, R2, n);
    CHECK(memcmp(L, L2, bytes) == 0 && memcmp(R, R2, bytes) == 0, "rack: naming the first place alone does not keep the others in the usual order");

    /* orders of the six: finite and bounded */
    int order[6], bad = 0;
    float worst = 0.0f;
    for (int code = 0; code < 46656; code++) {
        int seen = 0, c = code;
        for (int k = 0; k < 6; k++)
            order[k] = c % 6, c /= 6, seen |= 1 << order[k];
        if (seen != 63 || code % 97) /* the permutations, one in 97 of them */
            continue;
        rack_render(order, L, R, 24000);
        if (!all_finite_bounded(L, 24000, 2.0f) || !all_finite_bounded(R, 24000, 2.0f))
            bad++;
        worst = fmaxf(worst, fmaxf(max_abs(L, 24000), max_abs(R, 24000)));
    }
    CHECK(bad == 0, "rack: %d orders not finite or over 2 (max %g)", bad, (double)worst);
    synth_init(synth_default(), 48000.0f);
    free(L), free(R), free(L2), free(R2);
}

static void tape_run(tape_t *t, float *L, float *R, int n)
{
    for (int i = 0; i < n; i += 32)
        tape_process(t, L + i, R + i, n - i < 32 ? n - i : 32);
}

static void test_tape(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 4;
    static tape_t t;
    static float tape_mem[TAPE_MEM_FLOATS];
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n), *L2 = malloc(sizeof(float) * (size_t)n);
    #define TAPE_SINE(f, a)                                                             \
        for (int i = 0; i < n; i++)                                                     \
            L[i] = R[i] = (a) * sinf(6.2831853f * (f) * (float)i / sr)
    /* off: untouched */
    TAPE_SINE(1000.0f, 0.5f);
    memcpy(L2, L, sizeof(float) * (size_t)n);
    tape_init(&t, sr, tape_mem);
    tape_run(&t, L, R, n);
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "tape off changes the signal");
    /* on, everything at rest: the level is kept, the pitch too */
    tape_init(&t, sr, tape_mem);
    tape_set(&t, 1, 0.0f, 0.0f, 0.0f, 0.0f, 18000.0f);
    tape_run(&t, L, R, n);
    const float lv = 20.0f * log10f(rms(L + 24000, n - 24000) / rms(L2 + 24000, n - 24000));
    const float f0 = estimate_freq(L + 24000, n - 24000, sr);
    printf("  tape at rest: %.2f dB, %.2f Hz\n", (double)lv, (double)f0);
    CHECK(fabsf(lv) < 1.0f && fabsf(f0 - 1000.0f) < 0.5f, "tape at rest: %.2f dB, %.2f Hz", (double)lv, (double)f0);
    /* full wow: the pitch wanders by about 0.5 % either way (measured over 50 ms windows) */
    TAPE_SINE(1000.0f, 0.5f);
    tape_init(&t, sr, tape_mem);
    tape_set(&t, 1, 1.0f, 0.0f, 0.0f, 0.0f, 18000.0f);
    tape_run(&t, L, R, n);
    float lo = 1e9f, hi = 0.0f;
    for (int i = 24000; i + 2400 <= n; i += 1200) {
        const float f = estimate_freq(L + i, 2400, sr);
        lo = fminf(lo, f), hi = fmaxf(hi, f);
    }
    printf("  tape wow 1: %.1f .. %.1f Hz around 1000\n", (double)lo, (double)hi);
    CHECK(hi - lo > 5.0f && hi - lo < 14.0f && lo < 1000.0f && hi > 1000.0f, "tape wow: %.1f .. %.1f Hz", (double)lo, (double)hi);
    /* tone at 1 kHz: an 8 kHz sine loses more than 12 dB */
    TAPE_SINE(8000.0f, 0.5f);
    tape_init(&t, sr, tape_mem);
    tape_set(&t, 1, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f);
    tape_run(&t, L, R, n);
    const float loss = 20.0f * log10f(rms(L + 24000, n - 24000) / (0.5f * 0.70710678f));
    CHECK(loss < -12.0f, "tape tone 1 kHz: 8 kHz only %.1f dB down", (double)loss);
    /* hiss alone: between -70 and -45 dBFS */
    for (int i = 0; i < n; i++)
        L[i] = R[i] = 0.0f;
    tape_init(&t, sr, tape_mem);
    tape_set(&t, 1, 0.0f, 0.0f, 0.0f, 1.0f, 18000.0f);
    tape_run(&t, L, R, n);
    const float hiss = 20.0f * log10f(rms(L + 24000, n - 24000) + 1e-12f);
    printf("  tape hiss 1: %.1f dBFS\n", (double)hiss);
    CHECK(hiss > -70.0f && hiss < -45.0f, "tape hiss: %.1f dBFS", (double)hiss);
    /* full drive on a peak of 0.25: the peak holds, the wave is squarer (more level for the same peak) */
    TAPE_SINE(200.0f, 0.25f);
    tape_init(&t, sr, tape_mem);
    tape_set(&t, 1, 0.0f, 0.0f, 1.0f, 0.0f, 18000.0f);
    tape_run(&t, L, R, n);
    const float pk = max_abs(L + 24000, n - 24000), crest = pk / rms(L + 24000, n - 24000);
    printf("  tape drive 1 on a sine at 0.25: peak %.3f, crest %.2f\n", (double)pk, (double)crest);
    CHECK(pk > 0.2f && pk < 0.3f && crest < 1.3f, "tape drive: peak %.3f, crest %.2f", (double)pk, (double)crest);
    /* everything at its top on loud noise: bounded, deterministic */
    for (int run = 0; run < 2; run++) {
        uint32_t s = dsp_seed(5u);
        for (int i = 0; i < n; i++)
            L[i] = R[i] = dsp_rand_bipolar(&s);
        tape_init(&t, sr, tape_mem);
        tape_set(&t, 1, 1.0f, 1.0f, 1.0f, 1.0f, 18000.0f);
        tape_run(&t, L, R, n);
        if (run == 0) {
            CHECK(all_finite_bounded(L, n, 1.5f), "tape at its top: max %g", (double)max_abs(L, n));
            memcpy(L2, L, sizeof(float) * (size_t)n);
        } else
            CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "tape not deterministic");
    }
    #undef TAPE_SINE
    free(L), free(R), free(L2);
}

/* level (dB) of one frequency in x: correlation with a sine and a cosine */
static float tone_db(const float *x, int n, float f, float sr)
{
    double a = 0.0, b = 0.0;
    for (int i = 0; i < n; i++) {
        const double ph = 6.283185307179586 * (double)f * (double)i / (double)sr;
        a += (double)x[i] * sin(ph), b += (double)x[i] * cos(ph);
    }
    return (float)(10.0 * log10((a * a + b * b) / ((double)n * (double)n) * 4.0 + 1e-30));
}

static void test_shimmer(void)
{
    const float sr = 48000.0f;
    const int n = 48000 * 6;
    static rack_t f;
    static float mem[320000];
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float oct[2];
    for (int k = 0; k < 2; k++) {
        rack_init(&f, sr, mem, 320000, 2.0f, 1.5f);
        rack_set_param(&f, RACK_P_REVERB_MIX, 1.0f);
        rack_set_param(&f, RACK_P_REVERB_DECAY, 0.93f);
        rack_set_param(&f, RACK_P_REVERB_DAMPING, 1.0f);
        rack_set_param(&f, RACK_P_REVERB_SHIMMER, k ? 0.8f : 0.0f);
        for (int i = 0; i < n; i++) /* a second of a pure 330 Hz, then its tail */
            L[i] = R[i] = i < 48000 ? 0.3f * sinf(6.2831853f * 330.0f * (float)i / sr) : 0.0f;
        for (int i = 0; i < n; i += 32) {
            rack_process(&f, L + i, R + i, 32);
        }
        oct[k] = tone_db(L + 96000, 96000, 660.0f, sr) - tone_db(L + 96000, 96000, 330.0f, sr);
        printf("  shimmer %d: max %.2f\n", k, (double)max_abs(L, n));
        CHECK(all_finite_bounded(L, n, 4.0f), "shimmer %d: max %g", k, (double)max_abs(L, n));
    }
    printf("  shimmer: octave against the tone in the tail %.1f dB without, %.1f dB with\n", (double)oct[0], (double)oct[1]);
    CHECK(oct[1] > oct[0] + 20.0f && oct[1] > -25.0f, "shimmer adds no octave: %.1f dB -> %.1f dB", (double)oct[0], (double)oct[1]);
    /* the worst case: full amount, endless decay, loud noise for 20 s. The loop is bounded by its tanh, so the
     * shimmer can add at most what a full-scale input would: the level stays within 3x the reverb's own */
    float worst[2] = {0.0f, 0.0f};
    int finite = 1;
    for (int k = 0; k < 2; k++) {
        rack_init(&f, sr, mem, 320000, 2.0f, 1.5f);
        rack_set_param(&f, RACK_P_REVERB_MIX, 1.0f);
        rack_set_param(&f, RACK_P_REVERB_DECAY, 1.0f);
        rack_set_param(&f, RACK_P_REVERB_SHIMMER, k ? 1.0f : 0.0f);
        uint32_t s = dsp_seed(3u);
        for (int b = 0; b < 30000; b++) {
            float l[32], r[32];
            for (int i = 0; i < 32; i++)
                l[i] = r[i] = 0.5f * dsp_rand_bipolar(&s);
            rack_process(&f, l, r, 32);
            finite &= all_finite_bounded(l, 32, 1e6f);
            worst[k] = fmaxf(worst[k], max_abs(l, 32));
        }
    }
    printf("  endless decay, noise for 20 s: max %.1f without shimmer, %.1f with\n", (double)worst[0], (double)worst[1]);
    CHECK(finite && worst[1] < 3.0f * worst[0], "shimmer runs away: max %g against %g", (double)worst[1], (double)worst[0]);
    free(L), free(R);
}

void suite_fx(void)
{
    test_rack();
    test_tape();
    test_shimmer();
    test_modfx();
    test_dist();
    test_delay();
    test_chorus();
    test_freeze();

    const float sr = 48000.0f;
    const int n = 48000 * 12;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    const float t_short = impulse(sr, 0.5f, L, R, n);
    CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "reverb: NaN or |y| > 1");
    const float t_long = impulse(sr, 0.9f, L, R, n);
    printf("  reverb decay 0.5 -> T60 %.2f s, decay 0.9 -> T60 %.2f s\n", (double)t_short, (double)t_long);
    CHECK(t_short > 0.3f && t_short < 4.0f, "decay 0.5: T60 %.2f s", (double)t_short);
    CHECK(t_long > 2.0f * t_short, "decay 0.9 not much longer (%.2f s)", (double)t_long);
    CHECK(t_long < 11.5f, "decay 0.9 never dies out");

    /* stereo: decorrelated outputs */
    {
        double sl = 0, sr2 = 0, slr = 0;
        for (int i = 4800; i < 4800 * 10; i++) {
            sl += (double)L[i] * (double)L[i];
            sr2 += (double)R[i] * (double)R[i];
            slr += (double)L[i] * (double)R[i];
        }
        const double c = slr / sqrt(sl * sr2 + 1e-30);
        CHECK(fabs(c) < 0.5, "reverb L/R correlation %.2f", c);
    }

    /* deterministic */
    impulse(sr, 0.9f, L2, R2, n);
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "reverb not deterministic");

    /* other rates, full-scale noise input with max decay: still bounded */
    static const float rates[] = {44100.0f, 96000.0f};
    for (int k = 0; k < 2; k++) {
        reverb_t r;
        float *mem = malloc(sizeof(float) * reverb_mem_floats(rates[k], 2.0f));
        reverb_init(&r, rates[k], 2.0f, mem);
        reverb_set(&r, 0.99f, 20000.0f, 0.25f);
        uint32_t s = 5u;
        int ok = 1;
        float in[32], oL[32], oR[32];
        for (int i = 0; i < (int)rates[k] * 10; i += 32) {
            for (int j = 0; j < 32; j++)
                in[j] = dsp_rand_bipolar(&s);
            reverb_process(&r, in, oL, oR, 32);
            ok &= all_finite_bounded(oL, 32, 20.0f) && all_finite_bounded(oR, 32, 20.0f);
        }
        CHECK(ok, "reverb at %.0f Hz, decay 0.99: NaN or runaway", (double)rates[k]);
        free(mem);
    }
    free(L);
    free(R);
    free(L2);
    free(R2);
}
