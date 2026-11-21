/* test_synth.c — end-to-end tests through the public API (synth.h): pitch, bounds under a MIDI
 * storm, bit-exact determinism, independence from the host callback size, click-free voice stealing. */
#include <math.h>
#include <stdlib.h>

#include <string.h>

#include "engines/engine.h"
#include "engines/sine.h"
#include "engines/va.h"
#include "global_params.h"
#include "mod/params.h"
#include "synth.h"
#include "synth_debug.h"
#include "test.h"

#define SR 48000.0f

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, 0);
}

static void render(float *L, float *R, int n, int chunk)
{
    for (int i = 0; i < n; i += chunk)
        synth_render(synth_default(), L + i, R + i, (n - i < chunk) ? n - i : chunk);
}

static void set_va(int id, float physical) { synth_set_param(synth_default(), id, param_to_norm(&va_params[id], physical)); }

/* Random but reproducible MIDI storm on the va engine. */
static void storm(float *L, float *R, int n, int chunk)
{
    uint32_t r = 2024u;
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
    set_va(VA_P_RESO, 25.0f);
    set_va(VA_P_NOISE_LEVEL, 0.5f);
    set_va(VA_P_VOLUME, 6.0f);
    synth_set_param(synth_default(), SYNTH_PARAM_MASTER_DB, 1.0f); /* +6 dB: push the soft clipper */
    for (int i = 0; i < n; i += 480) {
        for (int k = 0; k < 4; k++) {
            const uint32_t x = dsp_rand_u32(&r);
            const uint8_t note = (uint8_t)(24 + (x >> 8) % 80), val = (uint8_t)((x >> 16) & 0x7F);
            switch (x % 6) {
            case 0: case 1: midi3(0x90, note, (uint8_t)(val | 1)); break;
            case 2: midi3(0x80, note, 0); break;
            case 3: midi3(0xB0, (uint8_t)(74 + (x >> 24) % 4), val); break;
            case 4: midi3(0xE0, val, (uint8_t)((x >> 24) & 0x7F)); break;
            default: { /* any parameter but the levels: the storm tests stability, it must not mute itself */
                const int id = (int)((x >> 4) % VA_P_COUNT);
                if (id != VA_P_VOLUME && id != VA_P_OSC1_LEVEL && id != VA_P_OSC2_LEVEL)
                    synth_set_param(synth_default(), id, dsp_rand_unipolar(&r));
                break;
            }
            }
        }
        render(L + i, R + i, (n - i < 480) ? n - i : 480, chunk);
    }
}

/* does a parameter called name (or starting with it, when it ends in '_') exist in the engine or the globals? */
static int param_exists(const engine_ops_t *e, const char *name, int len)
{
    const int prefix = name[len - 1] == '_';
    for (int i = 0; i < (e ? e->param_count : 0) + SYNTH_GLOBAL_COUNT; i++) {
        const param_desc_t *d = e && i < e->param_count ? &e->params[i] : synth_global_param(i - (e ? e->param_count : 0));
        if (d && (prefix ? strncmp(d->name, name, (size_t)len) == 0
                         : (int)strlen(d->name) == len && strncmp(d->name, name, (size_t)len) == 0))
            return 1;
    }
    return 0;
}

static int stage_exists(const engine_stage_t *st, int n, const char *id, int len)
{
    for (int k = 0; k < n; k++)
        if ((int)strlen(st[k].id) == len && strncmp(st[k].id, id, (size_t)len) == 0)
            return 1;
    return 0;
}

/* every word of a space-separated list passes ok() */
#define EACH_WORD(list, ok, what)                                                                         \
    for (const char *w = (list); w && *w;) {                                                              \
        while (*w == ' ')                                                                                 \
            w++;                                                                                          \
        int len = 0;                                                                                      \
        while (w[len] && w[len] != ' ')                                                                   \
            len++;                                                                                        \
        if (len)                                                                                          \
            CHECK(ok, "%s stage %s: %s '%.*s' unknown", name, st[k].id, what, len, w);                     \
        w += len;                                                                                         \
    }

/* signal-chain descriptions (engine.h) match the parameters and each other */
static void check_stages(const char *name, const engine_ops_t *e, const engine_stage_t *st, int n)
{
    for (int k = 0; k < n; k++) {
        EACH_WORD(st[k].params, param_exists(e, w, len), "parameter")
        EACH_WORD(st[k].inputs, stage_exists(st, n, w, len) || (len == 6 && !strncmp(w, "engine", 6)), "input")
        EACH_WORD(st[k].targets, stage_exists(st, n, w, len), "target")
        if (st[k].enable)
            CHECK(param_exists(e, st[k].enable, (int)strlen(st[k].enable)), "%s stage %s: enable '%s' unknown", name,
                  st[k].id, st[k].enable);
    }
    /* every engine parameter belongs to a stage (a UI groups them by stage) */
    for (int i = 0; e && n && i < e->param_count; i++) {
        int found = 0;
        for (int k = 0; k < n && !found; k++)
            for (const char *w = st[k].params; *w && !found;) {
                while (*w == ' ')
                    w++;
                int len = 0;
                while (w[len] && w[len] != ' ')
                    len++;
                found = len && (w[len - 1] == '_' ? !strncmp(e->params[i].name, w, (size_t)len)
                                                  : (int)strlen(e->params[i].name) == len && !strncmp(e->params[i].name, w, (size_t)len));
                w += len;
            }
        CHECK(found, "%s: parameter %s belongs to no stage", name, e->params[i].name);
    }
}

/* a full event queue (a burst of parameters) still takes the note-off: no note left hanging */
static void queue_full_note_off(void)
{
    static float qL[48000], qR[48000];
    synth_init(synth_default(), 48000.0f);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
    midi3(0x90, 60, 100);
    render(qL, qR, 4800, 256);
    for (int i = 0; i < 2000; i++) /* more than the queue holds */
        synth_set_param(synth_default(), SINE_P_SUSTAIN, 0.7f);
    midi3(0x80, 60, 0);
    render(qL, qR, 48000, 256);
    CHECK(rms(qL + 43200, 4800) < 1e-4f, "note-off dropped by a full queue (rms %g)", (double)rms(qL + 43200, 4800));
}

/* a NaN inside an engine (a numerical bug) in any build: the part is reset and plays again, the effects and
 * the output stay finite (Release builds: Debug ones stop at the first NaN on purpose, dsp_guard_block) */
#if !defined(SYNTH_NAN_GUARD)
static void nan_recovery(void)
{
    static float qL[48000], qR[48000];
    synth_t *s = synth_default();
    synth_init(s, 48000.0f);
    synth_set_engine(s, SYNTH_ENGINE_SINE);
    synth_set_param(s, SYNTH_PARAM_REVERB_MIX, 0.5f); /* a feedback path a NaN would poison for good */
    midi3(0x90, 60, 100);
    render(qL, qR, 4800, 256);
    sine_engine_t *e = synth_debug_engine_state(s, 0);
    for (int i = 0; i < SINE_VOICES; i++)
        e->v[i].osc.phase = NAN;
    render(qL, qR, 9600, 256);
    CHECK(all_finite_bounded(qL, 9600, 1.0f) && all_finite_bounded(qR, 9600, 1.0f), "NaN reached the output");
    CHECK(synth_debug_nan_count(s) >= 1, "NaN not detected");
    midi3(0x90, 64, 100);
    render(qL, qR, 9600, 256);
    CHECK(all_finite_bounded(qL, 9600, 1.0f) && rms(qL + 4800, 4800) > 0.01f, "the part did not play again (rms %g)",
          (double)rms(qL + 4800, 4800));
    CHECK(fabsf(estimate_freq(qL + 4800, 4800, 48000.0f) - 329.63f) < 2.0f, "new note out of tune after the reset");
}
#endif

void suite_synth(void)
{
    queue_full_note_off();
#if !defined(SYNTH_NAN_GUARD)
    nan_recovery();
#endif
    for (int i = 0; i < SYNTH_ENGINE_COUNT; i++)
        check_stages(engine_registry[i]->name, engine_registry[i], engine_registry[i]->stages,
                     engine_registry[i]->stages ? engine_registry[i]->stage_count : 0);
    check_stages("fx", NULL, synth_fx_stages, synth_fx_stage_count);

    const int n = 48000 * 2;
    float *L = malloc(sizeof(float) * (size_t)n), *R = malloc(sizeof(float) * (size_t)n);
    float *L2 = malloc(sizeof(float) * (size_t)n), *R2 = malloc(sizeof(float) * (size_t)n);

    /* Silence after init */
    synth_init(synth_default(), SR);
    render(L, R, 4800, 64);
    CHECK(max_abs(L, 4800) == 0.0f && max_abs(R, 4800) == 0.0f, "init is not silent");

    /* Pitch, sine engine: A4 = 440 Hz */
    synth_init(synth_default(), SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
    midi3(0x90, 69, 100);
    render(L, R, n, 128);
    float f = estimate_freq(L + 4800, n - 4800, SR);
    CHECK(fabsf(f - 440.0f) < 0.05f, "sine A4: %.4f Hz", (double)f);
    CHECK(rms(L + 24000, 24000) > 0.03f, "sine A4: level too low (%.4f)", (double)rms(L + 24000, 24000));

    /* Pitch, va engine (sine osc, no analog imperfections, osc2 off): C5 = 523.25 Hz, and pitch bend +2 st */
    synth_init(synth_default(), SR);
    set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
    set_va(VA_P_OSC2_LEVEL, 0.0f);
    set_va(VA_P_DRIFT, 0.0f);
    set_va(VA_P_TOLERANCE, 0.0f);
    set_va(VA_P_JITTER, 0.0f);
    set_va(VA_P_CUTOFF, 20000.0f);
    midi3(0x90, 72, 127);
    render(L, R, n, 256);
    f = estimate_freq(L + 4800, n - 4800, SR);
    CHECK(fabsf(f - 523.2511f) < 0.05f, "va C5: %.4f Hz", (double)f);
    midi3(0xE0, 0x7F, 0x7F); /* max bend = +8191/8192 x 2 st */
    render(L, R, n, 256);
    f = estimate_freq(L + 4800, n - 4800, SR);
    const float expect = 523.2511f * powf(2.0f, 2.0f * 8191.0f / 8192.0f / 12.0f);
    CHECK(fabsf(f - expect) < 0.1f, "va bend: %.3f Hz instead of %.3f", (double)f, (double)expect);

    /* MIDI storm: no NaN, never above 0 dBFS (soft clipper) */
    storm(L, R, n, 128);
    CHECK(all_finite_bounded(L, n, 1.0f) && all_finite_bounded(R, n, 1.0f), "storm: NaN or |x| = %.3f",
          (double)fmaxf(max_abs(L, n), max_abs(R, n)));
    CHECK(rms(L, n) > 0.01f, "storm: almost silent (%.4f)", (double)rms(L, n));

    /* Determinism: same input -> same output, bit for bit */
    storm(L2, R2, n, 128);
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0 && memcmp(R, R2, sizeof(float) * (size_t)n) == 0,
          "storm: not deterministic");

    /* Host callback size does not change the output (all events queued before rendering) */
    for (int pass = 0; pass < 2; pass++) {
        synth_init(synth_default(), SR);
        set_va(VA_P_DRIFT, 5.0f);
        midi3(0x90, 60, 100);
        midi3(0x90, 64, 80);
        midi3(0x90, 67, 90);
        render(pass ? L2 : L, pass ? R2 : R, n, pass ? 37 : 512);
    }
    CHECK(memcmp(L, L2, sizeof(float) * (size_t)n) == 0, "output depends on the callback size");

    /* Voice stealing without clicks: 8 low held notes, a 9th steals one. With 65 Hz sines the
     * natural sample-to-sample step is < 0.01; a hard cut would jump by ~0.1. */
    {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
        synth_set_param(synth_default(), SINE_P_SUSTAIN, 1.0f);
        for (int k = 0; k < SINE_VOICES; k++)
            midi3(0x90, (uint8_t)(36 + k), 127); /* distinct notes: every voice is taken */
        render(L, R, 9600, 64);
        midi3(0x90, (uint8_t)(36 + SINE_VOICES), 127); /* one more: a voice must be stolen */
        render(L, R, n, 64);
        float maxd = 0.0f;
        for (int i = 1; i < n; i++)
            maxd = fmaxf(maxd, fabsf(L[i] - L[i - 1]));
        CHECK(maxd < 0.03f, "voice stealing: jump of %.4f", (double)maxd);
    }

    /* Engine switch while notes are sounding: no jump */
    {
        synth_init(synth_default(), SR);
        set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
        set_va(VA_P_OSC2_LEVEL, 0.0f);
        midi3(0x90, 36, 127);
        render(L, R, 4800, 64);
        synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
        midi3(0x90, 36, 127);
        render(L + 4800, R + 4800, n - 4800, 64);
        float maxd = 0.0f;
        for (int i = 1; i < n; i++)
            maxd = fmaxf(maxd, fabsf(L[i] - L[i - 1]));
        CHECK(maxd < 0.03f, "engine switch: jump of %.4f", (double)maxd);
        CHECK(rms(L + n / 2, n / 2) > 0.01f, "no sound after engine switch");
    }

    /* Sustain pedal: note held after note-off, released when the pedal goes up */
    {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
        midi3(0xB0, 64, 127);
        midi3(0x90, 69, 100);
        render(L, R, 4800, 64);
        midi3(0x80, 69, 0);
        render(L, R, 48000, 64);
        CHECK(rms(L + 38400, 9600) > 0.02f, "sustain: note stopped under the pedal");
        midi3(0xB0, 64, 0);
        render(L, R, 48000, 64);
        CHECK(rms(L + 38400, 9600) < 1e-4f, "sustain: note still sounding after pedal up");
    }

    /* Pitch envelope: starts an octave up, settles on the note */
    {
        synth_init(synth_default(), SR);
        set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
        set_va(VA_P_OSC2_LEVEL, 0.0f);
        set_va(VA_P_DRIFT, 0.0f);
        set_va(VA_P_TOLERANCE, 0.0f);
        set_va(VA_P_JITTER, 0.0f);
        set_va(VA_P_CUTOFF, 20000.0f);
        set_va(VA_P_PENV_AMT, 12.0f);
        set_va(VA_P_PENV_DECAY, 0.1f); /* e^-10 left after 1 s */
        midi3(0x90, 57, 127); /* A3 = 220 Hz */
        render(L, R, n, 64);
        const float f_early = estimate_freq(L + 240, 1200, SR), f_late = estimate_freq(L + 48000, 24000, SR);
        CHECK(f_early > 350.0f && f_early < 445.0f, "pitch env: %.1f Hz at the attack (expected ~390)", (double)f_early);
        CHECK(fabsf(f_late - 220.0f) < 0.1f, "pitch env: %.2f Hz after settling", (double)f_late);
    }

    /* ---- monophonic play ---- */
    {
        const int b = 4800; /* 100 ms */
        /* legato, no glide: last-note priority, then back to the key still held */
        synth_init(synth_default(), SR);
        set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
        set_va(VA_P_OSC2_LEVEL, 0.0f);
        set_va(VA_P_DRIFT, 0.0f);
        set_va(VA_P_TOLERANCE, 0.0f);
        set_va(VA_P_JITTER, 0.0f);
        set_va(VA_P_CUTOFF, 20000.0f);
        set_va(VA_P_VOICE_MODE, 2.0f);
        midi3(0x90, 57, 100); /* A3 */
        render(L, R, 2 * b, 64);
        midi3(0x90, 60, 100); /* C4 while A3 held */
        render(L, R, 4 * b, 64);
        const float f_top = estimate_freq(L + b, 3 * b, SR);
        midi3(0x80, 60, 0); /* release C4: back to A3 */
        render(L, R, 4 * b, 64);
        const float f_back = estimate_freq(L + b, 3 * b, SR);
        midi3(0x80, 57, 0);
        render(L, R, n, 64);
        CHECK(fabsf(f_top - 261.63f) < 0.3f, "legato: %.2f Hz instead of C4", (double)f_top);
        CHECK(fabsf(f_back - 220.0f) < 0.3f, "legato: back to %.2f Hz instead of A3", (double)f_back);
        CHECK(rms(L + n - b, b) < 1e-4f, "mono voice does not stop after the last key");

        /* legato does not retrigger the envelope, mono does */
        float lvl[2];
        for (int mode = 1; mode <= 2; mode++) {
            synth_init(synth_default(), SR);
            set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
            set_va(VA_P_OSC2_LEVEL, 0.0f);
            set_va(VA_P_CUTOFF, 20000.0f);
            set_va(VA_P_AMP_D, 0.1f);
            set_va(VA_P_AMP_S, 0.3f);
            set_va(VA_P_VOICE_MODE, (float)mode);
            midi3(0x90, 57, 127);
            render(L, R, 10 * b, 64);
            const float before = rms(L + 8 * b, b);
            midi3(0x90, 60, 127);
            render(L, R, b / 2, 64);
            lvl[mode - 1] = rms(L, b / 2) / before;
        }
        CHECK(lvl[1] < 1.3f, "legato retriggers the envelope (level x%.2f)", (double)lvl[1]);
        CHECK(lvl[0] > 1.5f, "mono does not retrigger the envelope (level x%.2f)", (double)lvl[0]);

        /* glide: halfway after a short time, on target after a few glide times */
        synth_init(synth_default(), SR);
        set_va(VA_P_OSC1_WAVE, (float)OSC_SINE);
        set_va(VA_P_OSC2_LEVEL, 0.0f);
        set_va(VA_P_DRIFT, 0.0f);
        set_va(VA_P_TOLERANCE, 0.0f);
        set_va(VA_P_JITTER, 0.0f);
        set_va(VA_P_CUTOFF, 20000.0f);
        set_va(VA_P_VOICE_MODE, 2.0f);
        set_va(VA_P_GLIDE, 0.3f);
        midi3(0x90, 57, 100);
        render(L, R, 4 * b, 64);
        midi3(0x90, 69, 100); /* A4: an octave up, gliding */
        render(L, R, 12 * b, 64);
        const float f_mid = estimate_freq(L + 480, 1440, SR), f_end = estimate_freq(L + 8 * b, 3 * b, SR);
        CHECK(f_mid > 250.0f && f_mid < 420.0f, "glide: %.1f Hz 10-40 ms after the change", (double)f_mid);
        CHECK(fabsf(f_end - 440.0f) < 0.5f, "glide: ends at %.2f Hz instead of 440", (double)f_end);
    }

    /* changing the voice mode while a chord is held leaves no stuck note; re-sending the mode forgets nothing */
    for (int to_mono = 0; to_mono < 2; to_mono++) {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
        set_va(VA_P_VOICE_MODE, to_mono ? 0.0f : 1.0f);
        render(L, R, 4800, 64);
        midi3(0x90, 60, 100), midi3(0x90, 64, 100), midi3(0x90, 67, 100);
        render(L, R, 4800, 64);
        set_va(VA_P_VOICE_MODE, to_mono ? 1.0f : 0.0f); /* poly -> mono, then mono -> poly */
        render(L, R, 4800, 64);
        set_va(VA_P_VOICE_MODE, to_mono ? 1.0f : 0.0f); /* the same mode again */
        midi3(0x80, 60, 0), midi3(0x80, 64, 0), midi3(0x80, 67, 0);
        for (int k = 0; k < 3; k++) /* 3 s; the last second stays in L */
            render(L, R, 48000, 64);
        CHECK(rms(L, 48000) < 1e-5f, "voice mode change (%s): a note is stuck (rms %.2e)",
              to_mono ? "poly to mono" : "mono to poly", (double)rms(L, 48000));
    }
    {   /* a held mono note survives the same mode being sent again (a patch re-applied) */
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
        set_va(VA_P_VOICE_MODE, 1.0f);
        render(L, R, 4800, 64);
        midi3(0x90, 60, 100);
        render(L, R, 4800, 64);
        set_va(VA_P_VOICE_MODE, 1.0f);
        render(L, R, 4800, 64);
        midi3(0x80, 60, 0);
        for (int k = 0; k < 3; k++) /* 3 s; the last second stays in L */
            render(L, R, 48000, 64);
        CHECK(rms(L, 48000) < 1e-5f, "mode re-sent while a note is held: stuck (rms %.2e)",
              (double)rms(L, 48000));
    }

    /* NaN and unknown ids are ignored */
    {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
        synth_set_param(synth_default(), VA_P_CUTOFF, NAN);
        synth_set_param(synth_default(), SYNTH_PARAM_TUNING, NAN);
        synth_set_param(synth_default(), 65536 + VA_P_OSC1_LEVEL, 0.0f); /* would alias osc1_level if truncated */
        synth_set_param(synth_default(), -1, 0.5f);
        midi3(0x90, 57, 100);
        render(L, R, 9600, 64);
        CHECK(all_finite_bounded(L, 9600, 1.0f) && rms(L + 4800, 4800) > 1e-3f, "NaN / unknown parameter ids not ignored");
        synth_init(synth_default(), SR);
        midi3(0x90, 57, 100);
        render(L2, R2, 9600, 64);
        CHECK(memcmp(L, L2, sizeof(float) * 9600) == 0, "ignored parameter values changed the sound");
    }

    /* a whole sound pushed at once fits the event queue: the last of 600 parameter events is applied */
    {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
        for (int k = 0; k < 600; k++)
            set_va(VA_P_CUTOFF, 2000.0f);
        set_va(VA_P_OSC1_LEVEL, 0.0f);
        set_va(VA_P_OSC2_LEVEL, 0.0f);
        midi3(0x90, 57, 100);
        render(L, R, 9600, 64);
        CHECK(rms(L + 4800, 4800) < 1e-6f, "events dropped: both oscillators should be silent (rms %.2e)",
              (double)rms(L + 4800, 4800));
    }

    /* the sample offset of an event is honoured (resolution: one block) */
    {
        synth_init(synth_default(), SR);
        synth_set_engine(synth_default(), SYNTH_ENGINE_SINE);
        const uint8_t on[3] = {0x90, 69, 127};
        synth_midi(synth_default(), on, 3, 4800);
        render(L, R, 9600, 256);
        int first = 0;
        while (first < 9600 && L[first] == 0.0f)
            first++;
        CHECK(first >= 4800 - SYNTH_BLOCK && first <= 4800 + SYNTH_BLOCK, "note at offset 4800 starts at %d", first);
    }

    /* instances: created in caller memory, independent, identical to the default one */
    {
        const size_t bytes = (synth_size() + 15u) & ~(size_t)15u;
        void *ma = aligned_alloc(16, bytes), *mb = aligned_alloc(16, bytes);
        CHECK(synth_create((char *)ma + 4, bytes - 4, SR) == NULL, "synth_create accepted misaligned memory");
        CHECK(synth_create(ma, synth_size() - 1, SR) == NULL, "synth_create accepted memory that is too small");
        memset(ma, 0xA5, bytes); /* instances must not rely on zeroed memory */
        memset(mb, 0x5A, bytes);
        synth_t *a = synth_create(ma, bytes, SR), *b = synth_create(mb, bytes, SR);
        CHECK(a && b, "synth_create failed");
        if (a && b) {
            const uint8_t na[3] = {0x90, 57, 100}, nb[3] = {0x90, 76, 120};
            synth_set_engine(b, SYNTH_ENGINE_FM6);
            synth_set_param(b, SYNTH_PARAM_REVERB_MIX, 0.8f);
            synth_midi(a, na, 3, 0);
            synth_midi(b, nb, 3, 0);
            for (int i = 0; i < 24000; i += 64) { /* interleaved renders */
                synth_render(a, L + i, R + i, 64);
                synth_render(b, L2 + i, R2 + i, 64);
            }
            CHECK(rms(L2, 24000) > 1e-4f, "second instance silent");
            synth_init(synth_default(), SR);
            midi3(0x90, 57, 100);
            render(L2, R2, 24000, 64);
            CHECK(memcmp(L, L2, sizeof(float) * 24000) == 0 && memcmp(R, R2, sizeof(float) * 24000) == 0,
                  "an instance next to another differs from the default instance alone");
        }
        free(ma);
        free(mb);
    }

    free(L);
    free(R);
    free(L2);
    free(R2);
}
