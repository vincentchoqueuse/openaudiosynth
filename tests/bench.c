/* bench.c — host benchmark: cost of each engine in ns per voice per sample, at full polyphony,
 * plus the equivalent load at 48 kHz. Run with `synth_tests --bench` (Release build). */
#include <stdlib.h>
#include <time.h>

#include "engines/va.h"
#include "engines/micro.h"
#include "engines/modal.h"
#include "engines/drums.h"
#include "engines/wt.h"
#include "global_params.h"
#include "mod/params.h"
#include "synth.h"
#include "test.h"

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* os: the engine's oversample parameter (-1 = leave the default; 0, 1, 2 = 1x, 2x, 4x) */
static void bench_engine_os(const char *name, synth_engine_t eng, int voices, int supersaw, int fx, int os)
{
    const float sr = 48000.0f;
    const int seconds = 20, chunk = 256, n = (int)sr * seconds;
    float L[256], R[256];
    synth_init(synth_default(), sr);
    synth_set_engine(synth_default(), eng);
    if (eng == SYNTH_ENGINE_VA) {
        synth_set_param(synth_default(), VA_P_AMP_S, 1.0f);
        synth_set_param(synth_default(), VA_P_NOISE_LEVEL, param_to_norm(&va_params[VA_P_NOISE_LEVEL], 0.2f));
        synth_set_param(synth_default(), VA_P_LFO_CUTOFF, param_to_norm(&va_params[VA_P_LFO_CUTOFF], 1.0f));
        if (supersaw) { /* unison: 2 x `supersaw` saws per voice, spread in stereo (width 0.5), with motion */
            synth_set_param(synth_default(), VA_P_OSC1_UNISON, param_to_norm(&va_params[VA_P_OSC1_UNISON], (float)supersaw));
            synth_set_param(synth_default(), VA_P_OSC2_UNISON, param_to_norm(&va_params[VA_P_OSC2_UNISON], (float)supersaw));
            synth_set_param(synth_default(), VA_P_UNI_MOTION, param_to_norm(&va_params[VA_P_UNI_MOTION], 8.0f));
        }
    } else if (eng == SYNTH_ENGINE_MODAL) {
        synth_set_param(synth_default(), MODAL_P_MODES, 1.0f); /* 16 modes */
        synth_set_param(synth_default(), MODAL_P_DECAY, 1.0f);  /* 30 s: voices stay active */
        synth_set_param(synth_default(), MODAL_P_MODEL, 0.0f);  /* harmonic: all 16 modes below Nyquist */
        synth_set_param(synth_default(), MODAL_P_BOW, 0.5f);    /* worst case: strike + bow */
    } else if (eng == SYNTH_ENGINE_WT) { /* supersaw flag = worst case: 2 x 8 unison, sub, ladder */
        synth_set_param(synth_default(), WT_OSC(1, WT_O_LEVEL), 1.0f);
        if (supersaw) {
            synth_set_param(synth_default(), WT_OSC(0, WT_O_UNISON), 1.0f);
            synth_set_param(synth_default(), WT_OSC(1, WT_O_UNISON), 1.0f);
            synth_set_param(synth_default(), WT_P_SUB, 1.0f);
            synth_set_param(synth_default(), WT_P_FILTER_TYPE, 1.0f);
        }
    } else if (eng == SYNTH_ENGINE_MICRO) { /* worst case: held, both sines, sub, drive, free gate, pitch envelope */
        const struct { int id; float v; } set[] = {
            {MICRO_P_SUSTAIN, 1.0f}, {MICRO_P_OSC2_LEVEL, 0.0f}, {MICRO_P_SUB_LEVEL, 0.0f}, {MICRO_P_DRIVE, 12.0f},
            {MICRO_P_GATE_MODE, 2.0f}, {MICRO_P_PITCH_AMOUNT, 24.0f}, {MICRO_P_PITCH_TIME, 4.0f},
            {MICRO_P_CLICK_MODE, 2.0f}};
        for (unsigned k = 0; k < sizeof set / sizeof set[0]; k++)
            synth_set_param(synth_default(), set[k].id, param_to_norm(&micro_params[set[k].id], set[k].v));
    } else { /* parameter 2 at its top, as the bench has always done for the other engines */
        synth_set_param(synth_default(), 2, 1.0f);
    }
    if (fx) { /* whole effect bus on */
        synth_set_param(synth_default(), SYNTH_PARAM_CHORUS_MODE, 1.0f);
        synth_set_param(synth_default(), SYNTH_PARAM_CHORUS_MIX, 0.5f);
        synth_set_param(synth_default(), SYNTH_PARAM_DELAY_MIX, 0.3f);
        synth_set_param(synth_default(), SYNTH_PARAM_REVERB_MIX, 0.4f);
    }
    if (fx > 1) { /* and the phaser, the flanger, the master saturation (soft) and limiter */
        synth_set_param(synth_default(), SYNTH_PARAM_PHASER_MIX, 1.0f);
        synth_set_param(synth_default(), SYNTH_PARAM_PHASER_STAGES, 1.0f);
        synth_set_param(synth_default(), SYNTH_PARAM_FLANGER_MIX, 1.0f);
        synth_set_param(synth_default(), SYNTH_PARAM_SATURATION, 1.0f / 3.0f);
        synth_set_param(synth_default(), SYNTH_PARAM_LIMITER, 1.0f);
    }
    if (os >= 0) {
        const engine_ops_t *ops = engine_registry[eng];
        const int id = param_find(ops->params, ops->param_count, "oversample");
        if (id >= 0)
            synth_set_param(synth_default(), id, param_to_norm(&ops->params[id], (float)os));
    }
    for (int k = 0; k < voices; k++) {
        /* drums: one hit of every instrument, long decays so they all ring through the bench */
        const int note = eng == SYNTH_ENGINE_DRUMS ? drums_instrument_notes[k] : 48 + 3 * k;
        if (eng == SYNTH_ENGINE_DRUMS)
            synth_set_param(synth_default(), DRUMS_P(k, DRUMS_DECAY), 1.0f);
        const uint8_t m[3] = {0x90, (uint8_t)note, 100};
        synth_midi(synth_default(), m, 3, 0);
    }
    synth_render(synth_default(), L, R, chunk); /* warm-up */
    const double t0 = now_s();
    for (int i = 0; i < n; i += chunk)
        synth_render(synth_default(), L, R, chunk);
    const double dt = now_s() - t0;
    const double ns = 1e9 * dt / ((double)n * (double)voices);
    printf("  %-6s %2d voices: %6.2f ns/voice/sample, load %.2f %% of real time\n", name, voices, ns,
           100.0 * dt / (double)seconds);
}

static void bench_engine(const char *name, synth_engine_t eng, int voices, int supersaw, int fx)
{
    bench_engine_os(name, eng, voices, supersaw, fx, -1);
}

void run_bench(void)
{
    printf("bench (host, 48 kHz, block %d)\n", SYNTH_BLOCK);
    bench_engine("va", SYNTH_ENGINE_VA, VA_VOICES, 0, 0);
    bench_engine("va-u7", SYNTH_ENGINE_VA, VA_VOICES, 7, 0);
    bench_engine("va-u16", SYNTH_ENGINE_VA, VA_VOICES, 16, 0);
    bench_engine("modal", SYNTH_ENGINE_MODAL, MODAL_VOICES, 0, 0);
    bench_engine("va+fx", SYNTH_ENGINE_VA, VA_VOICES, 0, 1);
    bench_engine("va+all fx", SYNTH_ENGINE_VA, VA_VOICES, 0, 2);
    bench_engine("drums", SYNTH_ENGINE_DRUMS, DR_COUNT, 0, 0);
    bench_engine("mono", SYNTH_ENGINE_MONO, 1, 0, 0);
    bench_engine("fm6", SYNTH_ENGINE_FM6, 16, 0, 0);
    bench_engine("wt", SYNTH_ENGINE_WT, 8, 0, 0);
    bench_engine("wt-u8", SYNTH_ENGINE_WT, 8, 1, 0);
    bench_engine("drone", SYNTH_ENGINE_DRONE, 8, 0, 0);
    bench_engine("micro", SYNTH_ENGINE_MICRO, 8, 0, 0);
    /* the oversample setting of the nonlinear filters */
    bench_engine_os("mono-1x", SYNTH_ENGINE_MONO, 1, 0, 0, 0);
    bench_engine_os("mono-4x", SYNTH_ENGINE_MONO, 1, 0, 0, 2);
    bench_engine_os("wtu8-4x", SYNTH_ENGINE_WT, 8, 1, 0, 2);
}
