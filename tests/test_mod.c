/* test_mod.c — MSEG (segments, curves, sustain, loops held and free, retrigger without jump, zero times are
 * bounded) and the per-part modulation layer through the public API (a macro route moves the sound, a removed
 * route restores the base value, a non-realtime destination is ignored, a tempo-synced LFO follows the beat,
 * an MSEG route shapes the level); the wander walks of the macros (still at 0, smooth, bounded, one per macro,
 * deterministic). */
#include <math.h>
#include <stdlib.h>

#include "engines/fm6.h"
#include "engines/micro.h"
#include "engines/va.h"
#include "env/mseg.h"
#include "global_params.h"
#include "mod/partmod.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N 48000

static float L[N], R[N], L2[N], R2[N];

static void setmod(int index, float physical)
{
    synth_set_param(synth_default(), SYNTH_MOD_PARAM + index, param_to_norm(synth_mod_param_info(index), physical));
}
static void route(int r, int src, int dst_param, float amount)
{
    setmod(PM_ROUTE(r, PM_R_SRC), (float)src);
    setmod(PM_ROUTE(r, PM_R_DST), (float)(dst_param + 1));
    setmod(PM_ROUTE(r, PM_R_AMOUNT), amount);
}
static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}
static void render(float *l, float *r, int n)
{
    for (int i = 0; i < n; i += 256)
        synth_render(synth_default(), l + i, r + i, n - i < 256 ? n - i : 256);
}

static mseg_shape_t shape3(void)
{
    mseg_shape_t s = {{0}, {0}, {0}, 3, -1, -1, -1, MSEG_ONESHOT};
    s.level[0] = 0.0f, s.time[1] = 0.1f, s.level[1] = 1.0f, s.time[2] = 0.1f, s.level[2] = 0.5f;
    return s;
}

static float run(mseg_t *m, const mseg_shape_t *s, float seconds)
{
    float v = m->value;
    for (float t = 0.0f; t < seconds - 1e-6f; t += 0.001f)
        v = mseg_advance(m, s, 0.001f);
    return v;
}

static void sine_part(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MICRO);
    synth_set_param(synth_default(), MICRO_P_SUSTAIN, 1.0f);
}

/* a route aimed at an engine parameter loses its destination when the part changes engine */
static void routes_engine_switch(void)
{
    static partmod_t m;
    partmod_init(&m, SR, 1);
    partmod_engine(&m, &va_engine);
    partmod_set(&m, PM_ROUTE(0, PM_R_SRC), param_to_norm(&partmod_params[PM_ROUTE(0, PM_R_SRC)], (float)PM_SRC_LFO1));
    partmod_set(&m, PM_ROUTE(0, PM_R_DST), param_to_norm(&partmod_params[PM_ROUTE(0, PM_R_DST)], (float)(VA_P_CUTOFF + 1)));
    partmod_set(&m, PM_ROUTE(0, PM_R_AMOUNT), param_to_norm(&partmod_params[PM_ROUTE(0, PM_R_AMOUNT)], 0.5f));
    CHECK(m.n_routes == 1, "route not counted");
    partmod_engine(&m, &fm6_engine);
    CHECK(m.n_routes == 0 && m.p[PM_ROUTE(0, PM_R_DST)] == 0.0f && m.p[PM_ROUTE(0, PM_R_SRC)] == (float)PM_SRC_LFO1,
          "route kept its destination across an engine switch");
}

static void mset(int index, float physical)
{
    synth_set_param(synth_default(), SYNTH_MOD_PARAM + index, param_to_norm(&partmod_params[index], physical));
}

/* rms of a held sine note after the macros settle */
static float macro_level(float m1, float m2)
{
    mset(PM_MACRO(0), m1);
    mset(PM_MACRO(1), m2);
    synth_render(synth_default(), L, R, 4800); /* the 5 ms glide */
    synth_render(synth_default(), L, R, 4800);
    return rms(L, 4800);
}

/* a macro moves its target between two values along the target's own curve; two macros on one target add */
static void macro_targets(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MICRO);
    synth_set_param(synth_default(), MICRO_P_SUSTAIN, 0.7f); /* held notes */
    const param_desc_t *vol = &micro_params[MICRO_P_VOLUME];
    mset(PM_MAP(0, PM_M_MACRO), 1.0f);                    /* macro1 -> volume, -30 dB .. 0 dB */
    mset(PM_MAP(0, PM_M_DST), (float)(MICRO_P_VOLUME + 1));
    mset(PM_MAP(0, PM_M_FROM), param_to_norm(vol, -30.0f));
    mset(PM_MAP(0, PM_M_TO), param_to_norm(vol, 0.0f));
    const uint8_t on[3] = {0x90, 69, 100};
    synth_midi(synth_default(), on, 3, 0);
    synth_render(synth_default(), L, R, 24000);
    const float lo = macro_level(0.0f, 0.0f), hi = macro_level(1.0f, 0.0f), mid = macro_level(0.5f, 0.0f);
    const float span = 20.0f * log10f(hi / lo), half = 20.0f * log10f(mid / lo);
    CHECK(fabsf(span - 30.0f) < 0.5f, "macro1 0..1 moves the volume by %.2f dB instead of 30", (double)span);
    CHECK(fabsf(half - 15.0f) < 0.5f, "macro1 at 0.5: %.2f dB instead of 15 (along the dB curve)", (double)half);
    /* macro2 also on volume, +12 dB of travel: adds to macro1's */
    mset(PM_MAP(1, PM_M_MACRO), 2.0f);
    mset(PM_MAP(1, PM_M_DST), (float)(MICRO_P_VOLUME + 1));
    mset(PM_MAP(1, PM_M_FROM), 0.0f);
    mset(PM_MAP(1, PM_M_TO), param_to_norm(vol, 6.0f) - param_to_norm(vol, -6.0f));
    const float both = macro_level(0.0f, 1.0f);
    CHECK(fabsf(20.0f * log10f(both / lo) - 12.0f) < 0.5f, "two macros on one target do not add (%.2f dB)",
          (double)(20.0f * log10f(both / lo)));
    /* switched off (no macro): the base value comes back */
    mset(PM_MAP(0, PM_M_MACRO), 0.0f);
    mset(PM_MAP(1, PM_M_MACRO), 0.0f);
    const float base = macro_level(1.0f, 1.0f);
    CHECK(fabsf(20.0f * log10f(base / hi)) < 0.5f, "base volume not restored (%.2f dB from 0 dB)",
          (double)(20.0f * log10f(base / hi)));
}

/* a macro target on the master level (a synth parameter, not the part's), a CC that turns the macro, and the
 * master's own value back when the target goes away */
static void macro_global_and_cc(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_MICRO);
    synth_set_param(synth_default(), MICRO_P_SUSTAIN, 0.7f); /* held notes */
    const param_desc_t *master = synth_global_param(0);
    mset(PM_MAP(0, PM_M_MACRO), 1.0f);                    /* macro1 -> master, -24 dB .. 0 dB */
    mset(PM_MAP(0, PM_M_DST), (float)(SYNTH_PARAM_MASTER_DB + 1));
    mset(PM_MAP(0, PM_M_FROM), param_to_norm(master, -24.0f));
    mset(PM_MAP(0, PM_M_TO), param_to_norm(master, 0.0f));
    const uint8_t on[3] = {0x90, 69, 100};
    synth_midi(synth_default(), on, 3, 0);
    synth_render(synth_default(), L, R, 24000);
    const float lo = macro_level(0.0f, 0.0f), hi = macro_level(1.0f, 0.0f);
    CHECK(fabsf(20.0f * log10f(hi / lo) - 24.0f) < 0.5f, "macro on the master moves %.2f dB instead of 24",
          (double)(20.0f * log10f(hi / lo)));
    /* the macro's CC (default 102 for macro1) turns it: half way */
    const uint8_t cc[3] = {0xB0, 102, 64};
    synth_midi(synth_default(), cc, 3, 0);
    synth_render(synth_default(), L, R, 9600);
    synth_render(synth_default(), L, R, 4800);
    const float half = 20.0f * log10f(rms(L, 4800) / lo);
    CHECK(fabsf(half - 24.0f * 64.0f / 127.0f) < 0.6f, "CC 102 did not turn macro1 (%.2f dB)", (double)half);
    /* the master set meanwhile (-12 dB) is what comes back when the target goes away */
    synth_set_param(synth_default(), SYNTH_PARAM_MASTER_DB, param_to_norm(master, -12.0f));
    mset(PM_MAP(0, PM_M_MACRO), 0.0f);
    const float back = macro_level(1.0f, 0.0f);
    CHECK(fabsf(20.0f * log10f(back / hi) + 12.0f) < 0.5f, "the master's own value did not come back (%.2f dB)",
          (double)(20.0f * log10f(back / hi)));
}

/* wander: slow walks of the macros. Nothing while its amount is 0; with it, each macro moves on its own walk,
 * smoothly, within its reach, and the same way at every start. */
static void wander_walks(void)
{
    static partmod_t m, m2;
    static tempo_t tp;
    tempo_init(&tp, SR, 120.0f);
    partmod_init(&m, SR, 1);
    partmod_init(&m2, SR, 1);
    const int blocks = (int)(60.0f * SR / 32.0f); /* a minute */
    for (int b = 0; b < 200; b++) {
        partmod_block(&m, &tp, 32, 0, 0);
        partmod_block(&m2, &tp, 32, 0, 0);
    }
    int still = 1;
    for (int k = 0; k < PM_MACROS; k++)
        still &= m.wd[k] == 0.0f;
    CHECK(still, "wander at 0 moves the macros");
    partmod_set(&m, PM_WANDER, 1.0f);
    partmod_set(&m2, PM_WANDER, 1.0f);
    partmod_set(&m, PM_WANDER_RATE, param_to_norm(&partmod_params[PM_WANDER_RATE], 0.5f));
    partmod_set(&m2, PM_WANDER_RATE, param_to_norm(&partmod_params[PM_WANDER_RATE], 0.5f));
    partmod_block(&m, &tp, 32, 0, 0); /* the amount just opened: the first block is where the walk stands */
    partmod_block(&m2, &tp, 32, 0, 0);
    float lo[2] = {1.0f, 1.0f}, hi[2] = {-1.0f, -1.0f}, step = 0.0f, prev = m.wd[0];
    int same = 0, det = 1;
    for (int b = 0; b < blocks; b++) {
        partmod_block(&m, &tp, 32, 0, 0);
        partmod_block(&m2, &tp, 32, 0, 0);
        for (int k = 0; k < 2; k++)
            lo[k] = fminf(lo[k], m.wd[k]), hi[k] = fmaxf(hi[k], m.wd[k]);
        step = fmaxf(step, fabsf(m.wd[0] - prev));
        prev = m.wd[0];
        same += m.wd[0] == m.wd[1];
        det &= m.wd[0] == m2.wd[0] && m.wd[7] == m2.wd[7];
    }
    printf("  wander 1 at 0.5 Hz, a minute: macro 1 offset %.2f .. %.2f, largest step per block %.4f\n", (double)lo[0], (double)hi[0], (double)step);
    CHECK(hi[0] - lo[0] > 0.2f && hi[1] - lo[1] > 0.2f, "wander does not move: %.2f and %.2f", (double)(hi[0] - lo[0]), (double)(hi[1] - lo[1]));
    CHECK(lo[0] >= -PM_WANDER_REACH && hi[0] <= PM_WANDER_REACH, "wander beyond its reach: %.2f .. %.2f", (double)lo[0], (double)hi[0]);
    CHECK(step < 0.002f, "wander is not smooth: step %.4f in a block", (double)step);
    CHECK(same < blocks / 100, "the walks of macros 1 and 2 are the same");
    CHECK(det, "wander is not the same at every start");
}

/* the duck shape of a part LFO, a side-chain compressor's gain over each cycle: down to -1 within the attack, held,
 * then 1 - 2 exp(-t / release); no jump where a cycle begins */
static void lfo_duck(void)
{
    static partmod_t m;
    static tempo_t tp;
    tempo_init(&tp, SR, 120.0f);
    partmod_init(&m, SR, 1);
    const float att = 0.005f, hold = 0.06f, rel = 0.1f;
    partmod_set(&m, PM_LFO(0, PM_LFO_WAVE), param_to_norm(&partmod_params[PM_LFO(0, PM_LFO_WAVE)], (float)PM_WAVE_DUCK));
    partmod_set(&m, PM_LFO(0, PM_LFO_RATE), param_to_norm(&partmod_params[PM_LFO(0, PM_LFO_RATE)], 1.0f));
    partmod_set(&m, PM_DUCK(0, PM_DUCK_ATTACK), param_to_norm(&partmod_params[PM_DUCK(0, PM_DUCK_ATTACK)], att));
    partmod_set(&m, PM_DUCK(0, PM_DUCK_HOLD), param_to_norm(&partmod_params[PM_DUCK(0, PM_DUCK_HOLD)], hold));
    partmod_set(&m, PM_DUCK(0, PM_DUCK_RELEASE), param_to_norm(&partmod_params[PM_DUCK(0, PM_DUCK_RELEASE)], rel));
    float lo = 1.0f, hi = -1.0f, worst = 0.0f, jump = 0.0f, prev = 1.0f;
    int held = 0;
    for (int b = 0; b < (int)(2.0f * SR / 32.0f); b++) {
        partmod_block(&m, &tp, 32, 0, 0);
        const float v = m.src[PM_SRC_LFO1], since = m.lfo_phase[0]; /* 1 Hz: the phase is the time */
        lo = fminf(lo, v), hi = fmaxf(hi, v);
        if (since > att && since < att + hold)
            held += v == -1.0f;
        if (since > att + hold)
            worst = fmaxf(worst, fabsf(v - (1.0f - 2.0f * expf(-(since - att - hold) / rel))));
        if (b > 0)
            jump = fmaxf(jump, fabsf(v - prev));
        prev = v;
        CHECK(v >= -1.0f && v <= 1.0f, "duck out of -1..1: %g", (double)v);
    }
    CHECK(lo == -1.0f && hi > 0.99f, "duck: %.3f .. %.3f (-1 .. 1 expected)", (double)lo, (double)hi);
    CHECK(held > 100, "duck: not held at -1 (%d blocks)", held);
    CHECK(worst < 0.01f, "duck release: %.4f away from 1 - 2 exp(-t / 0.1 s)", (double)worst);
    /* the attack spans about 7 blocks: no step bigger than a share of the full travel */
    CHECK(jump < 0.4f, "duck: a step of %.3f in a block (the attack should ramp)", (double)jump);
}

void suite_mod(void)
{
    wander_walks();
    lfo_duck();
    routes_engine_switch();
    macro_targets();
    macro_global_and_cc();
    /* oneshot: linear segments, ends on the last level */
    mseg_shape_t s = shape3();
    mseg_t m;
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    CHECK(fabsf(run(&m, &s, 0.05f) - 0.5f) < 0.02f, "mseg: half of segment 1 = %.3f", (double)m.value);
    CHECK(fabsf(run(&m, &s, 0.05f) - 1.0f) < 0.02f, "mseg: point 1 = %.3f", (double)m.value);
    CHECK(fabsf(run(&m, &s, 0.1f) - 0.5f) < 0.02f, "mseg: point 2 = %.3f", (double)m.value);
    CHECK(run(&m, &s, 1.0f) == 0.5f, "mseg: end level %.3f", (double)m.value);

    /* curve +1: fast start (above the straight line at mid-segment), -1: slow start */
    for (int k = 0; k < 2; k++) {
        s = shape3();
        s.curve[1] = k ? -1.0f : 1.0f;
        mseg_reset(&m, &s);
        mseg_gate(&m, &s, 1);
        const float v = run(&m, &s, 0.05f);
        CHECK(k ? v < 0.2f : v > 0.8f, "mseg curve %+d: %.3f at mid-segment", k ? -1 : 1, (double)v);
    }

    /* sustain at point 2, release to point 3 */
    s = shape3();
    s.count = 4, s.sustain = 2, s.level[2] = 0.6f, s.time[3] = 0.2f, s.level[3] = 0.0f;
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    CHECK(fabsf(run(&m, &s, 1.0f) - 0.6f) < 1e-6f, "mseg sustain: %.3f", (double)m.value);
    mseg_gate(&m, &s, 0);
    CHECK(fabsf(run(&m, &s, 0.1f) - 0.3f) < 0.02f, "mseg release half-way: %.3f", (double)m.value);
    CHECK(run(&m, &s, 0.2f) == 0.0f, "mseg release end: %.3f", (double)m.value);

    /* a note-off does not restart the segment in progress: no sustain, 1 s decay, released after 0.5 s */
    s = shape3();
    s.time[2] = 1.0f, s.level[2] = 0.0f;
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    run(&m, &s, 0.6f); /* 0.1 s attack + half of the decay */
    mseg_gate(&m, &s, 0);
    CHECK(fabsf(run(&m, &s, 0.25f) - 0.25f) < 0.02f, "mseg: note-off restarted the decay (%.3f, 0.25 expected)", (double)m.value);
    CHECK(run(&m, &s, 0.3f) == 0.0f, "mseg: decay not finished on time (%.3f)", (double)m.value);

    /* a held loop ending on the last point: releasing it does not jump to the last level */
    s = shape3();
    s.mode = MSEG_LOOP_HELD, s.loop_start = 0, s.loop_end = 2, s.time[2] = 0.4f, s.level[2] = 0.0f;
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    run(&m, &s, 0.2f); /* attack done, a quarter into the 0.4 s fall: 0.75 */
    {
        const float before = m.value;
        mseg_gate(&m, &s, 0);
        const float after = mseg_advance(&m, &s, 0.001f);
        CHECK(fabsf(after - before) < 0.01f, "mseg: loop release jumped from %.3f to %.3f", (double)before, (double)after);
    }

    /* loop while held (points 1..2), then out; free loop keeps going after release */
    for (int mode = MSEG_LOOP_HELD; mode <= MSEG_LOOP_FREE; mode++) {
        s = shape3();
        s.count = 4, s.mode = mode, s.loop_start = 1, s.loop_end = 2, s.level[2] = 0.0f, s.time[3] = 0.1f, s.level[3] = 0.25f;
        mseg_reset(&m, &s);
        mseg_gate(&m, &s, 1);
        float lo = 1.0f, hi = 0.0f;
        for (int i = 0; i < 3000; i++) {
            const float v = mseg_advance(&m, &s, 0.001f);
            if (i > 1000)
                lo = fminf(lo, v), hi = fmaxf(hi, v);
        }
        CHECK(lo < 0.05f && hi > 0.95f, "mseg loop mode %d while held: range %.2f..%.2f", mode, (double)lo, (double)hi);
        mseg_gate(&m, &s, 0);
        lo = 1.0f, hi = 0.0f;
        for (int i = 0; i < 2000; i++) {
            const float v = mseg_advance(&m, &s, 0.001f);
            if (i > 1000)
                lo = fminf(lo, v), hi = fmaxf(hi, v);
        }
        if (mode == MSEG_LOOP_HELD)
            CHECK(lo == 0.25f && hi == 0.25f, "mseg loop_held after release: %.2f..%.2f", (double)lo, (double)hi);
        else
            CHECK(hi - lo > 0.9f, "mseg loop_free after release: %.2f..%.2f", (double)lo, (double)hi);
    }

    /* retrigger from the current value: no jump */
    s = shape3();
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    const float before = run(&m, &s, 0.15f);
    mseg_gate(&m, &s, 1);
    const float after = mseg_advance(&m, &s, 0.001f);
    CHECK(fabsf(after - before) < 0.02f, "mseg retrigger jump %.3f -> %.3f", (double)before, (double)after);

    /* zero times everywhere, looping: bounded work */
    s = shape3();
    s.time[1] = s.time[2] = 0.0f, s.mode = MSEG_LOOP_FREE, s.loop_start = 0, s.loop_end = 2;
    mseg_reset(&m, &s);
    mseg_gate(&m, &s, 1);
    const float z = mseg_advance(&m, &s, 1000.0f);
    CHECK(dsp_isfinite(z), "mseg zero times: %g", (double)z);

    /* macro route: macro1 -> va cutoff closes the (default, wide open) filter; amount 0 = no route, bit for bit */
    float c[2];
    for (int k = 0; k < 2; k++) {
        synth_init(synth_default(), SR);
        route(0, PM_SRC_MACRO1, VA_P_CUTOFF, -1.0f);
        setmod(PM_MACRO(0), (float)k * 0.6f);
        midi3(0x90, 45, 100);
        render(L, R, N);
        c[k] = rms(L + N / 2, N / 2);
        (void)R;
    }
    CHECK(c[1] < 0.7f * c[0], "macro -> cutoff: level %.4f -> %.4f", (double)c[0], (double)c[1]);
    for (int k = 0; k < 2; k++) {
        synth_init(synth_default(), SR);
        if (k)
            route(0, PM_SRC_MACRO1, VA_P_CUTOFF, 0.0f);
        midi3(0x90, 45, 100);
        render(k ? L2 : L, k ? R2 : R, N);
    }
    CHECK(memcmp(L, L2, sizeof L) == 0, "route with amount 0 changed the sound");

    /* removing a route restores the base value: sine volume pulled down by a macro, then released */
    sine_part();
    midi3(0x90, 69, 100);
    render(L, R, N / 4);
    const float ref = rms(L + N / 8, N / 8);
    route(0, PM_SRC_MACRO1, MICRO_P_VOLUME, -0.5f);
    setmod(PM_MACRO(0), 1.0f);
    render(L, R, N / 4);
    const float down = rms(L + N / 8, N / 8);
    setmod(PM_ROUTE(0, PM_R_AMOUNT), 0.0f);
    render(L, R, N / 4);
    const float back = rms(L + N / 8, N / 8);
    CHECK(down < 0.5f * ref && fabsf(back / ref - 1.0f) < 0.01f, "route removed: %.4f -> %.4f -> %.4f", (double)ref,
          (double)down, (double)back);

    /* non-realtime destination (va voice_mode): ignored */
    for (int k = 0; k < 2; k++) {
        synth_init(synth_default(), SR);
        if (k) {
            route(0, PM_SRC_MACRO1, VA_P_VOICE_MODE, 1.0f);
            setmod(PM_MACRO(0), 1.0f);
        }
        midi3(0x90, 45, 100);
        midi3(0x90, 52, 100);
        render(k ? L2 : L, k ? R2 : R, N / 4);
    }
    CHECK(memcmp(L, L2, sizeof(float) * N / 4) == 0, "non-realtime destination was modulated");

    /* tempo-synced LFO (1/4 at 120 BPM = 2 Hz) on the sine volume: the level repeats every 0.5 s */
    sine_part();
    synth_set_param(synth_default(), SYNTH_PARAM_TEMPO, param_to_norm(synth_global_param(SYNTH_PARAM_TEMPO - SYNTH_PARAM_GLOBAL), 120.0f));
    setmod(PM_LFO(0, PM_LFO_SYNC), 9.0f); /* 1/4 */
    route(0, PM_SRC_LFO1, MICRO_P_VOLUME, 0.3f);
    midi3(0x90, 69, 100);
    render(L, R, N);
    float env[100];
    for (int b = 0; b < 100; b++)
        env[b] = rms(L + b * 480, 480);
    float same = 0.0f, half = 0.0f;
    for (int b = 20; b < 50; b++)
        same += fabsf(env[b] - env[b + 50]), half += fabsf(env[b] - env[b + 25]);
    CHECK(same < 0.1f * half, "synced LFO period: diff at 0.5 s %.4f vs at 0.25 s %.4f", (double)same, (double)half);

    /* MSEG route: a slow rise of the sine volume */
    sine_part();
    setmod(PM_MSEG(0, PM_MSEG_SUSTAIN), 0.0f);
    setmod(PM_MSEG(0, PM_MSEG_POINTS), 2.0f);
    setmod(PM_MSEG_PT(0, 1, PM_PT_TIME), 1.0f);
    route(0, PM_SRC_MSEG1, MICRO_P_VOLUME, 0.5f);
    synth_set_param(synth_default(), MICRO_P_VOLUME, param_to_norm(&micro_params[MICRO_P_VOLUME], -30.0f));
    midi3(0x90, 69, 100);
    render(L, R, N);
    CHECK(rms(L + N - 4800, 4800) > 3.0f * rms(L + 2400, 4800), "mseg -> volume: %.4f then %.4f",
          (double)rms(L + 2400, 4800), (double)rms(L + N - 4800, 4800));

    /* a parameter changed while a note sounds glides (~5 ms) instead of jumping, for every engine: fm6 has no
     * smoothing of its own on operator levels */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FM6);
    midi3(0x90, 69, 100);
    render(L, R, 4800);
    synth_set_param(synth_default(), FM6_OP(0, FM6_LEVEL), param_to_norm(&fm6_params[FM6_OP(0, FM6_LEVEL)], 59.0f)); /* -30 dB */
    render(L, R, 4800);
    const float r0 = rms(L, 32), r_settled = rms(L + 2400, 480);
    const float r_after = rms(L + 64, 64); /* 1.3..2.7 ms after the change */
    CHECK(r_settled < 0.05f * r0, "glide: level did not settle (%.4f -> %.4f)", (double)r0, (double)r_settled);
    CHECK(r_after > 0.3f * r0, "glide: jumped instead of gliding (%.4f just after, was %.4f)", (double)r_after, (double)r0);
}
