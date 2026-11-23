/* test_drone2.c — drone field engine: the partials settle exactly on the harmonics of the held note, one per well;
 * from silence they bloom out of a cluster and travel at the asked speed; a new chord makes them migrate, audibly;
 * extra partials stay silent when wells are short; renewal and ecology move things; release brings silence and the
 * next note blooms again; every parameter at its extremes bounded and deterministic. Engine-level renders. */
#include <math.h>
#include <string.h>

#include "engines/drone2.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define BL SYNTH_BLOCK
#define N (10 * 48000)

static drone2_t eng;
static float L[N], R[N], L2[N];

static void setp(int id, float v) { drone2_engine.set_param(&eng, id, param_to_norm(&drone2_params[id], v)); }
static void fresh(void)
{
    drone2_engine.init(&eng, SR);
    setp(D2_P_TEMPERATURE, 0.0f);
    setp(D2_P_RESTLESS, 0.0f);
    setp(D2_P_ATTACK, 0.05f);
}
static void run(float *l, float *r, int n)
{
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i += BL)
        drone2_engine.render(&eng, l + i, r + i);
}
static void wait_s(float s)
{
    for (int b = 0; b < (int)(s * SR) / BL; b++) {
        memset(L, 0, sizeof(float) * BL);
        memset(R, 0, sizeof(float) * BL);
        drone2_engine.render(&eng, L, R);
    }
}
/* cents from partial i to the nearest harmonic k (1..h) of f0, and that k */
static float cents_off(int i, float f0, int h, int *kk)
{
    float best = 1e9f;
    for (int k = 1; k <= h; k++) {
        const float c = 1200.0f * (eng.q[i].x - log2f(f0 * (float)k));
        if (fabsf(c) < fabsf(best))
            best = c, *kk = k;
    }
    return best;
}
/* cents from partial i to its own well */
static float own_off(int i) { return eng.q[i].well < 0 ? 0.0f : 1200.0f * (eng.q[i].x - eng.wc[eng.q[i].well]); }
static int sounding(int i) { return eng.q[i].amp > 1e-4f; }

void suite_drone2(void)
{
    /* settles: 8 partials on the 8 harmonics of A2, one each, within 1 cent (temperature 0) */
    fresh();
    setp(D2_P_PARTIALS, 8.0f);
    setp(D2_P_HARMONICS, 8.0f);
    setp(D2_P_TRAVEL, 1.0f);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    wait_s(15.0f);
    int seen = 0, worst_k = 0;
    float worst = 0.0f;
    for (int i = 0; i < 8; i++) {
        int k = 0;
        const float c = cents_off(i, 110.0f, 8, &k);
        seen |= 1 << (k - 1);
        if (fabsf(c) > fabsf(worst))
            worst = c, worst_k = k;
    }
    CHECK(seen == 0xFF && fabsf(worst) < 1.0f, "drone2 settle: harmonics taken 0x%X, worst %.2f cents (k %d)", seen,
          (double)worst, worst_k);

    /* bloom: from silence, bloom 1 starts the partials ~2.2 octaves above the note; bloom 0 starts them in place */
    for (int bloom = 0; bloom < 2; bloom++) {
        fresh();
        setp(D2_P_PARTIALS, 8.0f);
        setp(D2_P_HARMONICS, 8.0f);
        setp(D2_P_BLOOM, (float)bloom);
        vpoly_note_on(&eng.poly, 45, 1.0f);
        run(L, R, 4800);
        float far = 0.0f;
        for (int i = 0; i < 8; i++)
            far += fabsf(own_off(i)) / 8.0f;
        CHECK(bloom ? far > 150.0f : far < 1.0f, "drone2 bloom %d: mean %.1f cents from the wells after 0.1 s", bloom,
              (double)far);
    }

    /* travel: 2 s per octave; the fundamental's partial, starting ~2.2 octaves up, is still far after 2 s and home
     * (within 5 cents) after 9 s */
    fresh();
    setp(D2_P_PARTIALS, 4.0f);
    setp(D2_P_HARMONICS, 4.0f);
    setp(D2_P_BLOOM, 1.0f);
    setp(D2_P_TRAVEL, 2.0f);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    wait_s(2.0f);
    const int f1 = eng.owner[0];
    const float d2 = f1 >= 0 ? 1200.0f * (eng.q[f1].x - log2f(110.0f)) : 1e9f;
    wait_s(7.0f);
    const float d9 = f1 >= 0 ? 1200.0f * (eng.q[f1].x - log2f(110.0f)) : 1e9f;
    CHECK(d2 > 600.0f && fabsf(d9) < 5.0f, "drone2 travel: %.0f cents away at 2 s, %.1f at 9 s", (double)d2,
          (double)d9);

    /* migration: A2 then D3; while moving, partials sound between the wells; then on D3's harmonics */
    fresh();
    setp(D2_P_PARTIALS, 8.0f);
    setp(D2_P_HARMONICS, 8.0f);
    setp(D2_P_TRAVEL, 2.0f);
    setp(D2_P_RELEASE, 2.0f);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    wait_s(10.0f);
    vpoly_note_off(&eng.poly, 45);
    vpoly_note_on(&eng.poly, 50, 1.0f);
    int between = 0; /* the most partials sounding away from both chords' wells, over the first 2 s */
    for (int b = 0; b < (int)(2.0f * SR) / BL; b++) {
        memset(L, 0, sizeof(float) * BL);
        memset(R, 0, sizeof(float) * BL);
        drone2_engine.render(&eng, L, R);
        int now = 0;
        for (int i = 0; i < 8; i++) {
            int k;
            const float a = cents_off(i, 110.0f, 8, &k), c = cents_off(i, 146.832f, 8, &k);
            now += sounding(i) && fabsf(a) > 20.0f && fabsf(c) > 20.0f;
        }
        between = now > between ? now : between;
    }
    wait_s(12.0f);
    float worst_d = 0.0f;
    int on_d = 0;
    for (int i = 0; i < 8; i++) {
        int k;
        const float c = cents_off(i, 146.832f, 8, &k);
        if (sounding(i))
            on_d++, worst_d = fmaxf(worst_d, fabsf(c));
    }
    CHECK(between >= 2 && on_d == 8 && worst_d < 2.0f,
          "drone2 migration: %d sounding between the chords, then %d on D3 (worst %.1f cents)", between, on_d,
          (double)worst_d);

    /* more partials than wells: 4 wells, 32 partials, 4 sound */
    fresh();
    setp(D2_P_PARTIALS, 32.0f);
    setp(D2_P_HARMONICS, 4.0f);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    wait_s(2.0f);
    int nson = 0;
    for (int i = 0; i < D2_PARTIALS; i++)
        nson += sounding(i);
    CHECK(nson == 4, "drone2 wells short: %d partials sounding for 4 wells", nson);

    /* release: silent 3 s after a 1 s release; the next note blooms again (from the cluster) */
    fresh();
    setp(D2_P_PARTIALS, 8.0f);
    setp(D2_P_HARMONICS, 8.0f);
    setp(D2_P_RELEASE, 1.0f);
    setp(D2_P_BLOOM, 1.0f);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    wait_s(12.0f);
    vpoly_note_off(&eng.poly, 45);
    wait_s(3.0f);
    run(L, R, 48000);
    const float tail = rms(L, 48000);
    vpoly_note_on(&eng.poly, 45, 1.0f);
    run(L, R, 4800);
    float far = 0.0f;
    for (int i = 0; i < 8; i++)
        far += fabsf(own_off(i)) / 8.0f;
    CHECK(tail < 1e-6f && far > 150.0f, "drone2 release: tail %g, next note %.0f cents from its wells (bloom)",
          (double)tail, (double)far);

    /* renewal 1: partials die and are reborn within 20 s; with 0, never */
    for (int on = 0; on < 2; on++) {
        int d = 0;
        fresh();
        setp(D2_P_PARTIALS, 12.0f);
        setp(D2_P_RENEWAL, (float)on);
        vpoly_note_on(&eng.poly, 45, 1.0f);
        int was[D2_PARTIALS] = {0};
        for (int b = 0; b < (int)(20.0f * SR) / BL; b++) {
            drone2_engine.render(&eng, L, R);
            for (int i = 0; i < 12; i++) {
                d += eng.q[i].dying && !was[i];
                was[i] = eng.q[i].dying;
            }
        }
        CHECK(on ? d >= 6 : d == 0, "drone2 renewal %d: %d deaths in 20 s", on, d);
    }

    /* ecology: levels move against each other (the spread of a partial's level over time) */
    float spread[2];
    for (int on = 0; on < 2; on++) {
        fresh();
        setp(D2_P_PARTIALS, 16.0f);
        setp(D2_P_ECOLOGY, (float)on);
        vpoly_note_on(&eng.poly, 45, 1.0f);
        wait_s(5.0f);
        float lo[16], hi[16];
        for (int i = 0; i < 16; i++)
            lo[i] = 1e9f, hi[i] = 0.0f;
        for (int b = 0; b < (int)(20.0f * SR) / BL; b++) {
            drone2_engine.render(&eng, L, R);
            for (int i = 0; i < 16; i++)
                lo[i] = fminf(lo[i], eng.q[i].amp), hi[i] = fmaxf(hi[i], eng.q[i].amp);
        }
        spread[on] = 0.0f;
        for (int i = 0; i < 16; i++)
            spread[on] += (hi[i] - lo[i]) / (hi[i] + 1e-9f) / 16.0f;
    }
    CHECK(spread[0] < 0.01f && spread[1] > 0.2f, "drone2 ecology: level spread %.3f off, %.3f on", (double)spread[0],
          (double)spread[1]);

    /* everything at its extreme, an 8-note cluster: finite, bounded, deterministic */
    for (int r = 0; r < 2; r++) {
        drone2_engine.init(&eng, SR);
        for (int id = 0; id < D2_P_COUNT; id++)
            drone2_engine.set_param(&eng, id, id == D2_P_ATTACK || id == D2_P_RELEASE ? 0.0f : 1.0f);
        setp(D2_P_VOLUME, 0.0f);
        for (int k = 0; k < 8; k++)
            vpoly_note_on(&eng.poly, 30 + 9 * k, 1.0f);
        run(r ? L2 : L, R, N);
    }
    CHECK(all_finite_bounded(L, N, 1.0f), "drone2 extremes: NaN or out of bounds (max %g)", (double)max_abs(L, N));
    CHECK(memcmp(L, L2, sizeof(float) * N) == 0, "drone2 extremes: not deterministic");

    /* through the synth: a held note sounds, bounded */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_DRONE2);
    const uint8_t on[3] = {0x90, 45, 100};
    synth_midi(synth_default(), on, 3, 0);
    for (int i = 0; i < 4 * 48000; i += 256)
        synth_render(synth_default(), L + i, R + i, 256);
    const float lv = rms(L + 3 * 48000, 48000);
    CHECK(lv > 0.01f && all_finite_bounded(L, 4 * 48000, 1.0f), "drone2 through the synth: rms %g", (double)lv);
}
