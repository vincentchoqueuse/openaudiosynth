/* test_env.c — exponential ADSR: attack/decay/release times within +/-2 %, retrigger without jump,
 * extreme times, bounded LFO. */
#include <math.h>
#include <stdlib.h>

#include "env/adsr.h"
#include "mod/lfo.h"
#include "test.h"

#define SR 48000.0f

/* Runs the envelope in blocks of 32 until pred is true; returns the sample index. */
static int run_until(adsr_t *e, int max, int (*pred)(const adsr_t *, float), float *last)
{
    float buf[32];
    for (int i = 0; i < max; i += 32) {
        adsr_process(e, buf, 32);
        for (int k = 0; k < 32; k++) {
            /* replay the state sample by sample through the produced value */
            if (pred(e, buf[k])) {
                *last = buf[k];
                return i + k + 1;
            }
        }
    }
    return -1;
}
static int reached_one(const adsr_t *e, float y) { (void)e; return y >= 1.0f; }
static int reached_half(const adsr_t *e, float y) { (void)e; return y <= 0.5f; }
static int reached_zero(const adsr_t *e, float y) { (void)e; return y <= 0.0f; }

void suite_env(void)
{
    adsr_t e;
    float last;

    adsr_init(&e, SR);
    adsr_set(&e, 0.010f, 0.100f, 0.5f, 0.200f);
    adsr_gate(&e, 1);
    int na = run_until(&e, 48000, reached_one, &last);
    CHECK(fabsf((float)na - 480.0f) <= 0.02f * 480.0f, "attack 10 ms: %d samples instead of 480", na);
    int nd = run_until(&e, 48000, reached_half, &last);
    CHECK(fabsf((float)nd - 4800.0f) <= 0.02f * 4800.0f + 32.0f, "decay 100 ms: %d samples instead of 4800", nd);
    CHECK(e.stage == ADSR_SUSTAIN, "not in sustain after decay");

    /* release from a sustain of 1: exact time */
    adsr_reset(&e);
    adsr_set(&e, 0.001f, 0.01f, 1.0f, 0.200f);
    adsr_gate(&e, 1);
    run_until(&e, 48000, reached_one, &last);
    float buf[32];
    for (int i = 0; i < 10; i++)
        adsr_process(&e, buf, 32);
    adsr_gate(&e, 0);
    int nr = run_until(&e, 48000 * 2, reached_zero, &last);
    CHECK(fabsf((float)nr - 9600.0f) <= 0.02f * 9600.0f, "release 200 ms: %d samples instead of 9600", nr);
    CHECK(!adsr_active(&e), "envelope active after release");

    /* Retrigger mid-release: the output restarts from the current level (no jump) */
    {
        adsr_reset(&e);
        adsr_set(&e, 0.005f, 0.1f, 0.8f, 0.5f);
        adsr_gate(&e, 1);
        float *y = malloc(sizeof(float) * 48000);
        int pos = 0;
        for (; pos < 9600; pos += 32)
            adsr_process(&e, y + pos, 32);
        adsr_gate(&e, 0);
        for (; pos < 19200; pos += 32)
            adsr_process(&e, y + pos, 32);
        adsr_gate(&e, 1);
        for (; pos < 48000; pos += 32)
            adsr_process(&e, y + pos, 32);
        float maxd = 0.0f;
        for (int i = 1; i < 48000; i++)
            maxd = fmaxf(maxd, fabsf(y[i] - y[i - 1]));
        /* 5 ms attack = 240 samples: max slope ~= 1.3*ln(1.3/0.3)/240 ~= 0.008 */
        CHECK(maxd < 0.01f, "jump of %.4f in the envelope (retrigger)", (double)maxd);
        CHECK(all_finite_bounded(y, 48000, 1.0f), "envelope outside [0, 1]");
        free(y);
    }

    /* Extreme times: 0.5 ms and 30 s, no NaN */
    {
        adsr_reset(&e);
        adsr_set(&e, 0.0001f, 0.0005f, 0.3f, 60.0f);
        adsr_gate(&e, 1);
        int ok = 1;
        for (int i = 0; i < 1000; i++) {
            adsr_process(&e, buf, 32);
            ok &= all_finite_bounded(buf, 32, 1.0f);
        }
        adsr_gate(&e, 0);
        for (int i = 0; i < 1000; i++) {
            adsr_process(&e, buf, 32);
            ok &= all_finite_bounded(buf, 32, 1.0f);
        }
        CHECK(ok, "extreme times: NaN or out of bounds");
        CHECK(adsr_active(&e), "30 s release finished too early");
    }

    /* LFO: all shapes bounded, correct frequency */
    {
        lfo_t l;
        lfo_init(&l, SR, 7);
        for (int w = 0; w < LFO_WAVE_COUNT; w++) {
            lfo_set(&l, 3.0f, (lfo_wave_t)w);
            int ok = 1;
            for (int i = 0; i < 3000; i++) {
                const float v = lfo_advance(&l, 32);
                ok &= dsp_isfinite(v) && fabsf(v) <= 1.0f;
            }
            CHECK(ok, "LFO shape %d outside [-1, 1]", w);
        }
        lfo_reset(&l, 0.0f);
        lfo_set(&l, 3.0f, LFO_SAW);
        int wraps = 0;
        float prev = lfo_advance(&l, 32);
        for (int i = 1; i < 1500 * 2 + 75; i++) { /* 2.05 s at control rate */
            const float v = lfo_advance(&l, 32);
            wraps += v < prev;
            prev = v;
        }
        CHECK(wraps == 6, "LFO 3 Hz: %d cycles in 2.05 s", wraps);
    }
}
