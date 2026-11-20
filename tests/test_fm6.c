/* test_fm6.c — 6-operator FM engine and sysex import: init voice is an exact sine, fixed frequency,
 * a 1:1 modulator creates harmonics, every algorithm is bounded at full levels and feedback, envelope
 * release speed follows the rate, deterministic output; sysex bulk dump round trip with checksum. */
#include <math.h>
#include <stdlib.h>

#include "fm6_sysex.h"
#include "engines/fm6.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f

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

static void setp(int id, float v) { synth_set_param(synth_default(), id, param_to_norm(&fm6_params[id], v)); }

static void fm(void)
{
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_FM6);
    setp(FM6_P_SPREAD, 0.0f);
}

/* 10 ms RMS: time at which it first falls 40 dB below its maximum */
static float len40(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 0; i + 480 <= n; i += 480)
        m = fmaxf(m, rms(x + i, 480));
    for (int i = 0; i + 480 <= n; i += 480)
        if (rms(x + i, 480) < 0.01f * m && i > 960)
            return (float)i / SR;
    return (float)n / SR;
}

void suite_fm6(void)
{
    const int n = 48000 * 2;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    /* init voice (op 1 alone): an exact sine at the note */
    fm();
    midi3(0x90, 69, 100);
    render(L, R, n);
    float f = estimate_freq(L + 4800, n - 4800, SR);
    CHECK(fabsf(f - 440.0f) < 0.05f, "fm6 init voice: %.4f Hz", (double)f);
    CHECK(rms(L + 4800, 9600) > 0.03f, "fm6 init voice too quiet");

    /* fixed frequency: 100 Hz whatever the note */
    fm();
    setp(FM6_OP(0, FM6_MODE), 1.0f);
    setp(FM6_OP(0, FM6_COARSE), 2.0f);
    midi3(0x90, 84, 100);
    render(L, R, n);
    f = estimate_freq(L + 4800, n - 4800, SR);
    CHECK(fabsf(f - 100.0f) < 0.1f, "fm6 fixed 100 Hz: %.3f Hz", (double)f);

    /* op 2 (ratio 1) modulating op 1: harmonics appear, the fundamental stays */
    fm();
    setp(FM6_OP(1, FM6_LEVEL), 85.0f);
    midi3(0x90, 57, 100);
    render(L, R, n);
    {
        const int N = 32768;
        float *db = malloc(sizeof(float) * (size_t)(N / 2 + 1));
        spectrum_db(L + 4800, N, db);
        const int k1 = (int)(220.0f * N / SR + 0.5f), k3 = (int)(660.0f * N / SR + 0.5f);
        float h1 = -300.0f, h3 = -300.0f;
        for (int d = -3; d <= 3; d++)
            h1 = fmaxf(h1, db[k1 + d]), h3 = fmaxf(h3, db[k3 + d]);
        CHECK(h1 - h3 < 30.0f, "fm6 modulation: 3rd harmonic %.1f dB below the fundamental (no FM?)", (double)(h1 - h3));
        free(db);
    }

    /* every algorithm at full levels with max feedback and fast LFO: bounded, finite, deterministic */
    for (int a = 1; a <= 32; a++) {
        for (int run = 0; run < 2; run++) {
            fm();
            setp(FM6_P_ALGORITHM, (float)a);
            setp(FM6_P_FEEDBACK, 7.0f);
            setp(FM6_P_LFO_PMD, 99.0f);
            setp(FM6_P_LFO_AMD, 99.0f);
            for (int k = 0; k < FM6_OPS; k++) {
                setp(FM6_OP(k, FM6_LEVEL), 99.0f);
                setp(FM6_OP(k, FM6_COARSE), (float)(k + 1));
                setp(FM6_OP(k, FM6_AMP_SENS), 3.0f);
            }
            for (int c = 0; c < 6; c++)
                midi3(0x90, (uint8_t)(36 + 7 * c), 127);
            render(run ? L2 : L, run ? R2 : R, 24000);
        }
        CHECK(all_finite_bounded(L, 24000, 1.0f), "algorithm %d: NaN or > 0 dBFS", a);
        CHECK(rms(L, 24000) > 1e-3f, "algorithm %d: silent", a);
        CHECK(memcmp(L, L2, sizeof(float) * 24000) == 0, "algorithm %d: not deterministic", a);
    }

    /* envelope: release rate 70 is much faster than release rate 40 */
    float t[2];
    for (int r = 0; r < 2; r++) {
        fm();
        setp(FM6_OP(0, FM6_R4), r ? 40.0f : 70.0f);
        midi3(0x90, 69, 100);
        render(L, R, 4800);
        midi3(0x80, 69, 0);
        render(L, R, n);
        t[r] = len40(L, n);
    }
    printf("  fm6 release to -40 dB: rate 70 %.3f s, rate 40 %.3f s\n", (double)t[0], (double)t[1]);
    CHECK(t[1] > 4.0f * t[0], "fm6 release rates: %.3f s vs %.3f s", (double)t[0], (double)t[1]);

    /* sysex: build a 32-voice bulk dump, parse it back */
    {
        uint8_t msg[4104];
        msg[0] = 0xF0, msg[1] = 0x43, msg[2] = 0x00, msg[3] = 0x09, msg[4] = 0x20, msg[5] = 0x00;
        uint8_t *d = msg + 6;
        memset(d, 0, 4096);
        for (int v = 0; v < 32; v++) {
            uint8_t *b = d + 128 * v;
            for (int o = 0; o < 6; o++) {
                uint8_t *s = b + 17 * o;
                s[0] = 90, s[1] = 50, s[2] = 30, s[3] = 60, s[4] = 99, s[5] = 80, s[6] = 70, s[7] = 0;
                s[8] = 39, s[11] = 1 | (2 << 2), s[12] = 3 | (9 << 3), s[13] = 2 | (5 << 2);
                s[14] = (uint8_t)(o == 5 ? 99 : 60 + o), s[15] = (uint8_t)(0 | (2 << 1)), s[16] = 25;
            }
            b[110] = (uint8_t)(v % 32); /* algorithm */
            b[111] = 5 | (1 << 3);       /* feedback 5, osc sync */
            b[116] = 1 | (4 << 1) | (3 << 4);
            b[117] = 24;
            memcpy(b + 118, "TEST VOICE", 10);
        }
        unsigned sum = 0;
        for (int i = 0; i < 4096; i++)
            sum += d[i];
        msg[4102] = (uint8_t)((-(int)sum) & 0x7F);
        msg[4103] = 0xF7;
        static fm6_sysex_voice_t voices[32];
        char err[128], warn[256];
        const int n_v = fm6_sysex_parse(msg, sizeof msg, voices, 32, err, sizeof err, warn, sizeof warn);
        CHECK(n_v == 32, "sysex: %d voices parsed", n_v);
        CHECK(warn[0] == 0, "sysex: unexpected warning: %s", warn);
        if (n_v == 32) {
            const fm6_sysex_voice_t *v = &voices[7];
            CHECK(!strncmp(v->name, "TEST VOICE", 10), "sysex: name '%s'", v->name);
            CHECK(v->p[FM6_P_ALGORITHM] == 8.0f, "sysex: algorithm %g", (double)v->p[FM6_P_ALGORITHM]);
            CHECK(v->p[FM6_P_FEEDBACK] == 5.0f && v->p[FM6_P_OSC_SYNC] == 1.0f, "sysex: feedback/sync");
            CHECK(v->p[FM6_OP(0, FM6_LEVEL)] == 99.0f, "sysex: op 1 level %g (data starts with op 6)",
                  (double)v->p[FM6_OP(0, FM6_LEVEL)]);
            CHECK(v->p[FM6_OP(5, FM6_LEVEL)] == 60.0f, "sysex: op 6 level %g", (double)v->p[FM6_OP(5, FM6_LEVEL)]);
            CHECK(v->p[FM6_OP(2, FM6_DETUNE)] == 9.0f && v->p[FM6_OP(2, FM6_RATE_SCALE)] == 3.0f, "sysex: packed byte 12");
            CHECK(v->p[FM6_OP(2, FM6_VEL_SENS)] == 5.0f && v->p[FM6_OP(2, FM6_AMP_SENS)] == 2.0f, "sysex: packed byte 13");
            CHECK(v->p[FM6_OP(2, FM6_LCURVE)] == 1.0f && v->p[FM6_OP(2, FM6_RCURVE)] == 2.0f, "sysex: curves");
            CHECK(v->p[FM6_OP(2, FM6_COARSE)] == 2.0f && v->p[FM6_OP(2, FM6_FINE)] == 25.0f, "sysex: frequency");
            CHECK(v->p[FM6_P_LFO_WAVE] == 4.0f && v->p[FM6_P_PMS] == 3.0f && v->p[FM6_P_LFO_SYNC] == 1.0f, "sysex: LFO byte");
        }
        msg[4102] ^= 1; /* corrupt the checksum: still parsed, with a warning */
        fm6_sysex_parse(msg, sizeof msg, voices, 32, err, sizeof err, warn, sizeof warn);
        CHECK(warn[0] != 0, "sysex: bad checksum not reported");
    }

    free(L);
    free(R);
    free(L2);
    free(R2);
}
