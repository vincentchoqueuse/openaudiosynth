/* test_wt.c — wavetable oscillator and engine: factory tables finite and normalized, sine frame pure and in
 * tune, aliasing of bright frames below a threshold (also with sync warp), position morph changes the
 * timbre, unison keeps the loudness, every table x warp x distortion bounded and deterministic, the sub's direct
 * route, modulation matrix,
 * graceful behaviour with less table memory. */
#include <math.h>
#include <stdlib.h>

#include "engines/wt.h"
#include "osc/wt_factory.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define N_FFT 32768

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

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&wt_params[id], v)); }

static void wt(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_WT);
    setp(WT_P_SPREAD, 0.0f);
    setp(WT_P_FILTER_TYPE, 0.0f);
}

/* strongest component away from the harmonics of f0 (+/- 8 bins), relative to the strongest peak (dB) */
static float inharmonic_db(const float *x, float f0, float *db)
{
    spectrum_db(x, N_FFT, db);
    float peak = -400.0f, worst = -400.0f;
    int wk = 0;
    for (int k = 20; k <= (int)(20000.0f * N_FFT / SR); k++) { /* audible band */
        peak = fmaxf(peak, db[k]);
        const float h = (float)k * SR / (float)N_FFT / f0;
        const float dist = fabsf(h - floorf(h + 0.5f)) * f0 * (float)N_FFT / SR; /* in bins */
        if ((dist > 8.0f || h < 0.5f) && db[k] > worst)
            worst = db[k], wk = k;
    }
    if (getenv("WT_DEBUG"))
        printf("    worst at %.0f Hz\n", (double)((float)wk * SR / (float)N_FFT));
    return worst - peak;
}

static float centroid(const float *x, float *db)
{
    spectrum_db(x, N_FFT, db);
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= N_FFT / 2; k++) {
        const double pw = pow(10.0, (double)db[k] / 10.0);
        num += pw * (double)k;
        den += pw;
    }
    return (float)(num / den) * SR / (float)N_FFT;
}

void suite_wt(void)
{
    const int n = 48000;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);
    float *db = malloc(sizeof(float) * (N_FFT / 2 + 1));

    /* factory tables: finite, every frame peaks at 1 (full band level) */
    {
        const size_t floats = WT_FACTORY_FLOATS;
        float *mem = malloc(sizeof(float) * floats);
        const int built = wt_factory_build(mem, floats);
        CHECK(built == WT_FACTORY_COUNT, "factory: %d tables built", built);
        for (int t = 0; t < built; t++) {
            const float *tab = wt_factory_table(mem, built, t);
            /* frames peak at 1 with all their harmonics; a sparser mip of a folded sine can peak higher (its
             * fundamental is stronger than the folded waveform's peak): harmless with -18 dBFS voices */
            CHECK(all_finite_bounded(tab, WT_TABLE_FLOATS, 1.6f), "table %s: NaN or > 1.6 (peak %.2f)", wt_factory_names[t],
                  (double)max_abs(tab, WT_TABLE_FLOATS));
            CHECK(max_abs(tab, 2048) > 0.999f, "table %s: frame 0 not normalized", wt_factory_names[t]);
        }
        /* less memory: fewer tables, missing ones fall back to table 0 */
        const int few = wt_factory_build(mem, 2 * WT_TABLE_FLOATS + WT_SCRATCH_FLOATS);
        CHECK(few == 2 && wt_factory_table(mem, few, 5) == mem, "factory with small memory: %d tables", few);
        CHECK(wt_factory_build(mem, 1000) == 0, "factory with no memory");
        free(mem);
    }

    /* sine frame (basic table, position 0): in tune and pure */
    wt();
    setp(WT_OSC(0, WT_O_POSITION), 0.0f);
    midi3(0x90, 69, 127);
    render(L, R, n);
    const float f = estimate_freq(L + 4800, n - 4800, SR);
    CHECK(fabsf(f - 440.0f) < 0.05f, "wt sine: %.4f Hz", (double)f);
    float a = inharmonic_db(L + 9600, 440.0f, db);
    CHECK(a < -80.0f, "wt sine: spurious components at %.1f dB", (double)a);
    CHECK(rms(L + 4800, 4800) > 0.03f, "wt sine too quiet");

    /* bright frames at high pitch: what folds back stays far below the tone */
    static const struct { int table; float pos; int warp; float amount; int note; float limit; } alias[] = {
        {WT_BASIC, 1.0f, WT_WARP_OFF, 0.0f, 93, -70.0f},   /* square at A6 */
        {WT_BASIC, 0.667f, WT_WARP_OFF, 0.0f, 100, -70.0f}, /* saw-ish at E7 */
        {WT_DIGITAL, 0.5f, WT_WARP_OFF, 0.0f, 88, -70.0f},
        /* sync: the reset step is polyBLEP-corrected (+9 dB); the slope break is not (-50 dB near 20 kHz) */
        {WT_BASIC, 0.667f, WT_WARP_SYNC, 0.6f, 81, -45.0f},
    };
    for (size_t k = 0; k < sizeof alias / sizeof alias[0]; k++) {
        wt();
        setp(WT_OSC(0, WT_O_TABLE), (float)alias[k].table);
        setp(WT_OSC(0, WT_O_POSITION), alias[k].pos);
        setp(WT_OSC(0, WT_O_WARP_MODE), (float)alias[k].warp);
        setp(WT_OSC(0, WT_O_WARP), alias[k].amount);
        midi3(0x90, (uint8_t)alias[k].note, 127);
        render(L, R, n);
        const float f0 = 440.0f * powf(2.0f, (float)(alias[k].note - 69) / 12.0f);
        a = inharmonic_db(L + 9600, f0, db);
        printf("  wt aliasing %-8s pos %.2f warp %d at %6.0f Hz: %.1f dB\n", wt_factory_names[alias[k].table],
               (double)alias[k].pos, alias[k].warp, (double)f0, (double)a);
        CHECK(a < alias[k].limit, "wt aliasing case %zu: %.1f dB", k, (double)a);
    }

    /* position morph: sine end darker than square end */
    float c[2];
    for (int k = 0; k < 2; k++) {
        wt();
        setp(WT_OSC(0, WT_O_POSITION), (float)k);
        midi3(0x90, 57, 127);
        render(L, R, n);
        c[k] = centroid(L + 9600, db);
    }
    CHECK(c[1] > 2.0f * c[0], "wt position morph: centroid %.0f -> %.0f Hz", (double)c[0], (double)c[1]);

    /* unison 8: same loudness within 3 dB, bounded */
    float lv[2];
    for (int k = 0; k < 2; k++) {
        wt();
        setp(WT_OSC(0, WT_O_UNISON), k ? 8.0f : 1.0f);
        setp(WT_OSC(0, WT_O_DETUNE), 20.0f);
        midi3(0x90, 45, 127);
        render(L, R, n);
        lv[k] = rms(L + 9600, n - 9600);
    }
    const float ud = 20.0f * log10f(lv[1] / lv[0]);
    CHECK(fabsf(ud) < 3.0f, "wt unison loudness: %+.1f dB", (double)ud);

    /* every table x warp, both oscillators, full modulation: bounded, finite, deterministic */
    for (int t = 0; t < WT_FACTORY_COUNT; t++)
        for (int w = 0; w < WT_WARP_COUNT; w++) {
            for (int run = 0; run < 2; run++) {
                wt();
                for (int o = 0; o < 2; o++) {
                    setp(WT_OSC(o, WT_O_TABLE), (float)t);
                    setp(WT_OSC(o, WT_O_WARP_MODE), (float)w);
                    setp(WT_OSC(o, WT_O_WARP), 1.0f);
                    setp(WT_OSC(o, WT_O_LEVEL), 1.0f);
                    setp(WT_OSC(o, WT_O_UNISON), 8.0f);
                }
                setp(WT_P_SUB, 1.0f);
                setp(WT_P_NOISE, 1.0f);
                setp(WT_P_FILTER_TYPE, (float)(t % 6));
                setp(WT_P_RESO, 1.0f);
                setp(WT_P_DIST_MODE, (float)(w % DIST_MODE_COUNT)); /* every shaper, at full drive, moved by LFO 2 */
                setp(WT_P_DIST_DRIVE, 36.0f);
                setp(WT_P_DIST_PLACE, (float)(t & 1));
                setp(WT_MOD(2, WT_M_SRC), WT_SRC_LFO2);
                setp(WT_MOD(2, WT_M_DST), WT_DST_DIST);
                setp(WT_MOD(2, WT_M_AMOUNT), 1.0f);
                setp(WT_MOD(0, WT_M_SRC), WT_SRC_LFO1);
                setp(WT_MOD(0, WT_M_DST), WT_DST_A_POS);
                setp(WT_MOD(0, WT_M_AMOUNT), 1.0f);
                setp(WT_MOD(1, WT_M_SRC), WT_SRC_LFO2);
                setp(WT_MOD(1, WT_M_DST), WT_DST_PITCH);
                setp(WT_MOD(1, WT_M_AMOUNT), 1.0f);
                setp(WT_P_LFO1_RATE, 40.0f);
                for (int k = 0; k < 8; k++)
                    midi3(0x90, (uint8_t)(24 + 11 * k), 127);
                render(run ? L2 : L, run ? R2 : R, 12000);
            }
            CHECK(all_finite_bounded(L, 12000, 1.0f), "table %d warp %d: NaN or > 0 dBFS", t, w);
            CHECK(rms(L, 12000) > 1e-3f, "table %d warp %d: silent", t, w);
            CHECK(memcmp(L, L2, sizeof(float) * 12000) == 0, "table %d warp %d: not deterministic", t, w);
        }

    /* the sub's route: through a vowel filter at 1 kHz it nearly vanishes; direct, it stays whole (A1, 55 Hz) */
    {
        float lvl[2];
        for (int direct = 0; direct < 2; direct++) {
            wt();
            setp(WT_OSC(0, WT_O_LEVEL), 0.0f);
            setp(WT_P_SUB, 1.0f);
            setp(WT_P_FILTER_TYPE, 8.0f); /* formant */
            setp(WT_P_CUTOFF, 1000.0f);
            setp(WT_P_DIST_MODE, 3.0f); /* a hard clip after the filter: the direct sub skips it too */
            setp(WT_P_SUB_ROUTE, (float)direct);
            midi3(0x90, 45, 127);
            render(L, R, 24000);
            lvl[direct] = rms(L + 12000, 12000);
        }
        printf("  sub through a formant filter %.4f, direct %.4f\n", (double)lvl[0], (double)lvl[1]);
        CHECK(lvl[1] > 4.0f * lvl[0], "sub route: direct %.4f not well above filtered %.4f", (double)lvl[1], (double)lvl[0]);
    }

    /* modulation matrix: arithmetic, then LFO -> position makes the brightness move */
    {
        const mod_slot_t slots[3] = {{1, 2, 0, 0.5f}, {2, 2, 3, -1.0f}, {1, 9, 0, 1.0f}};
        const float src[4] = {123.0f, 0.4f, 0.5f, 0.2f};
        float dst[4];
        modmatrix_eval(slots, 3, src, 4, dst, 4);
        CHECK(fabsf(dst[2] - (0.5f * 0.4f - 0.5f * 0.2f)) < 1e-7f && dst[1] == 0.0f && dst[3] == 0.0f,
              "modmatrix: dst %g %g %g", (double)dst[1], (double)dst[2], (double)dst[3]);
    }
    float spread[2];
    for (int k = 0; k < 2; k++) {
        wt();
        setp(WT_OSC(0, WT_O_POSITION), 0.5f);
        setp(WT_MOD(0, WT_M_SRC), WT_SRC_LFO1);
        setp(WT_MOD(0, WT_M_DST), WT_DST_A_POS);
        setp(WT_MOD(0, WT_M_AMOUNT), k ? 0.5f : 0.0f);
        setp(WT_P_LFO1_RATE, 1.0f);
        midi3(0x90, 57, 127);
        render(L, R, n);
        float lo = 1e9f, hi = 0.0f;
        for (int s = 0; s + 4096 <= n; s += 4096) {
            spectrum_db(L + s, 4096, db);
            double num = 0.0, den = 0.0;
            for (int b = 1; b <= 2048; b++) {
                const double pw = pow(10.0, (double)db[b] / 10.0);
                num += pw * b, den += pw;
            }
            const float cc = (float)(num / den);
            lo = fminf(lo, cc), hi = fmaxf(hi, cc);
        }
        spread[k] = hi / lo;
    }
    CHECK(spread[0] < 1.05f && spread[1] > 1.5f, "wt LFO -> position: brightness ratio %.2f (off) %.2f (on)",
          (double)spread[0], (double)spread[1]);

    free(L);
    free(R);
    free(L2);
    free(R2);
    free(db);
}
