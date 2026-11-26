/* test_clap_host.c — a minimal CLAP host that loads the built plugin (dlopen) and checks it: one descriptor, a
 * plugin that plays any engine and one plugin per engine (each lists only its own parameters, makes sound, and
 * refuses the state of another engine); a second instance playing other notes at the same time without disturbing the first, parameters listed with
 * stable ids; notes and a parameter change in the middle of a block render bit for bit like the engine called
 * directly through dsp/synth.h (this executable links its own copy); the state is a patch file that saves and
 * loads back, also while active; the sampler plugin finds the demo kits of its bundle, loads one chosen during
 * playback on the main thread (request_callback, on_main_thread), plays it, and keeps it in its state.
 * Exit code 0 = pass. */
#include <dlfcn.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

#include "engines/engine.h"
#include "engines/va.h"
#include "global_params.h"
#include "synth.h"

static int failures;
#define EXPECT(cond, ...)                                                                                              \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

/* ---- host side ---- */
static const void *host_ext(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_nop(const clap_host_t *h) { (void)h; }
static int callback_requested;
static void host_callback(const clap_host_t *h) { (void)h; callback_requested = 1; }
static const clap_host_t host = {CLAP_VERSION_INIT, NULL, "test host", "synth", "", "0", host_ext, host_nop, host_nop, host_callback};

typedef struct {
    clap_input_events_t in;
    const clap_event_header_t *ev[8];
    uint32_t n;
} events_t;
static uint32_t ev_size(const clap_input_events_t *l) { return ((const events_t *)l->ctx)->n; }
static const clap_event_header_t *ev_get(const clap_input_events_t *l, uint32_t i) { return ((const events_t *)l->ctx)->ev[i]; }
static bool out_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }
static const clap_output_events_t out_events = {NULL, out_push};

static clap_event_note_t note(uint32_t time, uint16_t type, int key, double vel)
{
    clap_event_note_t e = {{sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, type, 0}, -1, 0, 0, (int16_t)key, vel};
    return e;
}
static clap_event_param_value_t pvalue(uint32_t time, clap_id id, double v)
{
    clap_event_param_value_t e = {{sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0}, id, NULL, -1, -1, -1, -1, v};
    return e;
}

typedef struct {
    char *buf;
    size_t len, pos;
} mem_t;
static int64_t mem_write(const clap_ostream_t *s, const void *b, uint64_t n)
{
    mem_t *m = s->ctx;
    m->buf = realloc(m->buf, m->len + n + 1);
    memcpy(m->buf + m->len, b, n);
    m->len += n;
    m->buf[m->len] = 0;
    return (int64_t)n;
}
static int64_t mem_read(const clap_istream_t *s, void *b, uint64_t n)
{
    mem_t *m = s->ctx;
    const size_t k = m->len - m->pos < n ? m->len - m->pos : n;
    memcpy(b, m->buf + m->pos, k);
    m->pos += k;
    return (int64_t)k;
}

#define SR 48000.0
#define BLOCK 256
#define BLOCKS 300 /* 1.6 s */
static float pl_L[BLOCKS * BLOCK], pl_R[BLOCKS * BLOCK], ref_L[BLOCKS * BLOCK], ref_R[BLOCKS * BLOCK];

/* the plugin's apply_all with default values, through the engine directly */
static void ref_init(void)
{
    synth_init(synth_default(), (float)SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_VA);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_clap_host path/to/synth.clap[/Contents/MacOS/synth]\n");
        return 2;
    }
    char path[4096];
    snprintf(path, sizeof path, "%s", argv[1]);
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { /* a macOS bundle: the binary is inside */
        const char *base = strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1]; /* OASynth.clap -> OASynth */
        snprintf(path, sizeof path, "%s/Contents/MacOS/%.*s", argv[1], (int)strcspn(base, "."), base);
        lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    }
    if (!lib) {
        printf("FAIL: cannot load %s: %s\n", argv[1], dlerror());
        return 1;
    }
    const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
    EXPECT(entry && entry->init(argv[1]), "clap_entry");
    if (!entry)
        return 1;
    const clap_plugin_factory_t *f = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    EXPECT(f && f->get_plugin_count(f) == 1 + SYNTH_ENGINE_COUNT, "one plugin for all engines + one per engine (%u)",
           f ? f->get_plugin_count(f) : 0);
    EXPECT(!strcmp(f->get_plugin_descriptor(f, 0)->id, "synth.engine"), "plugin 0 keeps its id");
    const clap_plugin_descriptor_t *d = f->get_plugin_descriptor(f, 0);
    const clap_plugin_t *pl = f->create_plugin(f, &host, d->id);
    EXPECT(pl && pl->init(pl), "create");
    const clap_plugin_t *pl2 = f->create_plugin(f, &host, d->id);
    EXPECT(pl2 && pl2->init(pl2) && pl2->activate(pl2, SR, 1, 4096) && pl2->start_processing(pl2), "second instance");

    const clap_plugin_params_t *params = pl->get_extension(pl, CLAP_EXT_PARAMS);
    const clap_plugin_state_t *state = pl->get_extension(pl, CLAP_EXT_STATE);
    EXPECT(params && state && pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS) && pl->get_extension(pl, CLAP_EXT_NOTE_PORTS),
           "extensions");
    const uint32_t np = params->count(pl);
    EXPECT(np > 500, "parameter count %u", np);
    const clap_id cutoff = 1000u * SYNTH_ENGINE_VA + VA_P_CUTOFF;
    clap_param_info_t info;
    int found = 0;
    for (uint32_t i = 0; i < np; i++)
        if (params->get_info(pl, i, &info) && info.id == cutoff)
            found = !strcmp(info.name, "cutoff") && !strcmp(info.module, "va");
    EXPECT(found, "va cutoff listed with id %u", cutoff);

    /* render: note on at frame 10 of block 0, cutoff -> 800 Hz at frame 100 of block 20, note off at block 150 */
    EXPECT(pl->activate(pl, SR, 1, 4096) && pl->start_processing(pl), "activate");
    ref_init();
    const double cutoff_v = 800.0;
    float peak2 = 0.0f;
    const float cutoff_n = param_to_norm(&engine_registry[SYNTH_ENGINE_VA]->params[VA_P_CUTOFF], (float)cutoff_v);
    for (int b = 0; b < BLOCKS; b++) {
        events_t ev = {{NULL, ev_size, ev_get}, {0}, 0};
        ev.in.ctx = &ev;
        clap_event_note_t on = note(10, CLAP_EVENT_NOTE_ON, 57, 100.0 / 127.0), off = note(0, CLAP_EVENT_NOTE_OFF, 57, 0);
        clap_event_param_value_t pv = pvalue(100, cutoff, cutoff_v);
        if (b == 0)
            ev.ev[ev.n++] = &on.header;
        if (b == 20)
            ev.ev[ev.n++] = &pv.header;
        if (b == 150)
            ev.ev[ev.n++] = &off.header;
        float *outs[2] = {pl_L + b * BLOCK, pl_R + b * BLOCK};
        clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
        const clap_process_t pr = {(int64_t)b * BLOCK, BLOCK, NULL, NULL, &ab, 0, 1, &ev.in, &out_events};
        EXPECT(pl->process(pl, &pr) == CLAP_PROCESS_CONTINUE, "process");
        if (pl2) { /* the second instance plays another note in between, into its own buffer */
            static float L2[BLOCK], R2[BLOCK];
            float *outs2[2] = {L2, R2};
            clap_audio_buffer_t ab2 = {outs2, NULL, 2, 0, 0};
            events_t ev2 = {{NULL, ev_size, ev_get}, {0}, 0};
            ev2.in.ctx = &ev2;
            clap_event_note_t on2 = note(0, CLAP_EVENT_NOTE_ON, 69, 0.8);
            if (b == 3)
                ev2.ev[ev2.n++] = &on2.header;
            const clap_process_t pr2 = {(int64_t)b * BLOCK, BLOCK, NULL, NULL, &ab2, 0, 1, &ev2.in, &out_events};
            pl2->process(pl2, &pr2);
            for (int i = 0; i < BLOCK; i++)
                peak2 = fmaxf(peak2, fabsf(L2[i]));
        }
        /* reference, same calls */
        float *L = ref_L + b * BLOCK, *R = ref_R + b * BLOCK;
        if (b == 0) {
            const uint8_t m[3] = {0x90, 57, 100};
            synth_midi(synth_default(), m, 3, 10);
        }
        if (b == 150) {
            const uint8_t m[3] = {0x80, 57, 0};
            synth_midi(synth_default(), m, 3, 0);
        }
        if (b == 20) {
            synth_render(synth_default(), L, R, 100);
            synth_set_param(synth_default(), VA_P_CUTOFF, cutoff_n);
            synth_render(synth_default(), L + 100, R + 100, BLOCK - 100);
        } else {
            synth_render(synth_default(), L, R, BLOCK);
        }
    }
    float peak = 0.0f;
    for (int i = 0; i < BLOCKS * BLOCK; i++)
        peak = fmaxf(peak, fabsf(pl_L[i]));
    EXPECT(peak > 1e-3f, "plugin output silent");
    EXPECT(!pl2 || peak2 > 1e-3f, "second instance silent");
    EXPECT(!memcmp(pl_L, ref_L, sizeof pl_L) && !memcmp(pl_R, ref_R, sizeof pl_R),
           "plugin output differs from the engine called directly");
    double v;
    EXPECT(params->get_value(pl, cutoff, &v) && v == cutoff_v, "cutoff value %g", v);

    /* state: a patch file; load it back while active (handed to the audio thread at the next process) */
    mem_t m = {0};
    clap_ostream_t os = {&m, mem_write};
    EXPECT(state->save(pl, &os) && m.buf && strstr(m.buf, "\"engine\": \"va\"") && strstr(m.buf, "\"cutoff\": 800"),
           "state save: %s", m.buf ? m.buf : "(none)");
    clap_event_param_value_t reset = pvalue(0, cutoff, 2000.0);
    events_t ev = {{NULL, ev_size, ev_get}, {&reset.header}, 1};
    ev.in.ctx = &ev;
    params->flush(pl, &ev.in, &out_events);
    EXPECT(params->get_value(pl, cutoff, &v) && v == 2000.0, "flush");
    clap_istream_t is = {&m, mem_read};
    EXPECT(state->load(pl, &is), "state load");
    EXPECT(params->get_value(pl, cutoff, &v) && fabs(v - cutoff_v) < 1e-3,
           "a state loaded while active is visible at once, before the next process (%g)", v);
    m.pos = 0;
    EXPECT(state->load(pl, &is), "a second load before the audio thread took the first");
    {
        mem_t m2 = {0};
        clap_ostream_t os2 = {&m2, mem_write};
        EXPECT(state->save(pl, &os2) && m2.buf && strstr(m2.buf, "\"cutoff\": 800"), "save before process sees the loaded state");
        free(m2.buf);
    }
    {
        float *outs[2] = {pl_L, pl_R};
        clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
        events_t none = {{NULL, ev_size, ev_get}, {0}, 0};
        none.in.ctx = &none;
        const clap_process_t pr = {0, BLOCK, NULL, NULL, &ab, 0, 1, &none.in, &out_events};
        pl->process(pl, &pr);
    }
    EXPECT(params->get_value(pl, cutoff, &v) && fabs(v - cutoff_v) < 1e-3, "cutoff after load %g", v);
    /* the state gives every value back to the bit, also under a locale that writes "0,5" (a DAW may set one) */
    {
        const double odd = (double)1234.56789f;
        const int fr = setlocale(LC_NUMERIC, "fr_FR.UTF-8") || setlocale(LC_NUMERIC, "fr_FR");
        clap_event_param_value_t ov = pvalue(0, cutoff, odd), back = pvalue(0, cutoff, 2000.0);
        events_t e1 = {{NULL, ev_size, ev_get}, {&ov.header}, 1};
        e1.in.ctx = &e1;
        params->flush(pl, &e1.in, &out_events);
        mem_t m3 = {0};
        clap_ostream_t os3 = {&m3, mem_write};
        EXPECT(state->save(pl, &os3) && m3.buf && strstr(m3.buf, "\"cutoff\": 1234.56787"), "state number: %s", m3.buf ? m3.buf : "");
        events_t e2 = {{NULL, ev_size, ev_get}, {&back.header}, 1};
        e2.in.ctx = &e2;
        params->flush(pl, &e2.in, &out_events);
        clap_istream_t is3 = {&m3, mem_read};
        EXPECT(state->load(pl, &is3) && params->get_value(pl, cutoff, &v) && v == odd,
               "state round trip not exact%s: %.9g instead of %.9g", fr ? " (French locale)" : "", v, odd);
        free(m3.buf);
        setlocale(LC_NUMERIC, "C");
        float *outs[2] = {pl_L, pl_R}; /* the audio thread takes the loaded state */
        clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
        events_t none = {{NULL, ev_size, ev_get}, {0}, 0};
        none.in.ctx = &none;
        const clap_process_t pr = {0, BLOCK, NULL, NULL, &ab, 0, 1, &none.in, &out_events};
        pl->process(pl, &pr);
        if (!fr)
            printf("note: no French locale here, the locale part of the state test did not run\n");
    }
    /* out-of-range and NaN values from the host are clamped or ignored */
    {
        clap_event_param_value_t big = pvalue(0, cutoff, 1e9), nan = pvalue(0, cutoff, (double)NAN);
        events_t e2 = {{NULL, ev_size, ev_get}, {&big.header, &nan.header}, 2};
        e2.in.ctx = &e2;
        params->flush(pl, &e2.in, &out_events);
        EXPECT(params->get_value(pl, cutoff, &v) && v == 20000.0, "clamped to the maximum, NaN ignored (%g)", v);
    }
    /* a state loaded while active and never processed must not come back after a later one:
     * load A (800 Hz) while active, deactivate, load B (default cutoff), activate, process -> B */
    m.pos = 0;
    EXPECT(state->load(pl, &is), "load A while active");
    pl->stop_processing(pl);
    pl->deactivate(pl);
    {
        static const char b_state[] = "{\"version\": 2, \"engine\": \"va\", \"name\": \"b\", \"filter\": {\"cutoff\": 300}}";
        mem_t mb = {(char *)b_state, sizeof b_state - 1, 0};
        clap_istream_t isb = {&mb, mem_read};
        EXPECT(state->load(pl, &isb), "load B while inactive");
        EXPECT(pl->activate(pl, SR, 1, 4096) && pl->start_processing(pl), "reactivate");
        float *outs[2] = {pl_L, pl_R};
        clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
        events_t none = {{NULL, ev_size, ev_get}, {0}, 0};
        none.in.ctx = &none;
        const clap_process_t pr = {0, BLOCK, NULL, NULL, &ab, 0, 1, &none.in, &out_events};
        pl->process(pl, &pr);
        EXPECT(params->get_value(pl, cutoff, &v) && fabs(v - 300.0) < 1e-3, "the later state wins (%g)", v);
    }
    free(m.buf);

    pl->stop_processing(pl);
    pl->deactivate(pl);
    pl->destroy(pl);
    if (pl2) {
        pl2->stop_processing(pl2);
        pl2->deactivate(pl2);
        pl2->destroy(pl2);
    }
    const clap_plugin_t *again = f->create_plugin(f, &host, d->id);
    EXPECT(again != NULL, "a new instance after destroy");
    if (again)
        again->destroy(again);

    /* the single-engine plugins: plugin 1 + e plays engine e */
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        const engine_ops_t *en = engine_registry[e];
        const clap_plugin_descriptor_t *de = f->get_plugin_descriptor(f, (uint32_t)(1 + e));
        char want[64];
        snprintf(want, sizeof want, "synth.%s", en->name);
        EXPECT(de && !strcmp(de->id, want), "plugin %d id: %s", 1 + e, de ? de->id : "(none)");
        const clap_plugin_t *q = de ? f->create_plugin(f, &host, de->id) : NULL;
        EXPECT(q && q->init(q), "%s: create", want);
        if (!q)
            continue;
        const clap_plugin_params_t *qp = q->get_extension(q, CLAP_EXT_PARAMS);
        const clap_plugin_state_t *qs = q->get_extension(q, CLAP_EXT_STATE);
        /* only its own parameters and the shared ones: no "engine" parameter, no other engine's module */
        const uint32_t nq = qp->count(q);
        int own = 0, foreign = 0;
        for (uint32_t i = 0; i < nq; i++) {
            clap_param_info_t pi;
            if (!qp->get_info(q, i, &pi))
                continue;
            if (!strcmp(pi.module, en->name))
                own++;
            else if (strcmp(pi.module, "global") != 0 || !strcmp(pi.name, "engine"))
                foreign++;
        }
        const int want_own = en->param_count + (en->kit ? 1 : 0); /* + "kit" for the engines that play samples */
        EXPECT(own == want_own && foreign == 0 && nq < np, "%s: %d own parameters (%d expected), %d foreign",
               want, own, want_own, foreign);
        /* it makes sound with a note */
        EXPECT(q->activate(q, SR, 1, 4096) && q->start_processing(q), "%s: activate", want);
        float pk = 0.0f;
        for (int b = 0; b < 40; b++) {
            events_t ev1 = {{NULL, ev_size, ev_get}, {0}, 0};
            ev1.in.ctx = &ev1;
            clap_event_note_t on1 = note(0, CLAP_EVENT_NOTE_ON, 48, 0.9);
            if (b == 0)
                ev1.ev[ev1.n++] = &on1.header;
            float *outs[2] = {pl_L, pl_R};
            clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
            const clap_process_t pr = {(int64_t)b * BLOCK, BLOCK, NULL, NULL, &ab, 0, 1, &ev1.in, &out_events};
            q->process(q, &pr);
            for (int i = 0; i < BLOCK; i++)
                pk = fmaxf(pk, fabsf(pl_L[i]));
        }
        /* engines that play samples are silent until given a kit, which the plugin cannot load yet */
        EXPECT(en->kit ? pk == 0.0f : pk > 1e-4f && pk <= 1.0f, "%s: peak %g", want, (double)pk);
        /* its state names its engine, and the state of another engine is refused */
        mem_t ms = {0};
        clap_ostream_t oss = {&ms, mem_write};
        char tag[64];
        snprintf(tag, sizeof tag, "\"engine\": \"%s\"", en->name);
        EXPECT(qs->save(q, &oss) && ms.buf && strstr(ms.buf, tag), "%s: state does not name its engine", want);
        clap_istream_t iss = {&ms, mem_read};
        EXPECT(qs->load(q, &iss), "%s: its own state does not load back", want);
        free(ms.buf);
        char other[128];
        snprintf(other, sizeof other, "{\"version\": 2, \"engine\": \"%s\", \"name\": \"x\"}",
                 engine_registry[(e + 1) % SYNTH_ENGINE_COUNT]->name);
        mem_t mo = {other, strlen(other), 0};
        clap_istream_t iso = {&mo, mem_read};
        EXPECT(!qs->load(q, &iso), "%s accepted the state of another engine", want);
        q->stop_processing(q);
        q->deactivate(q);
        q->destroy(q);
    }
    /* the sampler plugin and the demo kits of its bundle */
    {
        const clap_plugin_descriptor_t *de = f->get_plugin_descriptor(f, (uint32_t)(1 + SYNTH_ENGINE_SAMPLER));
        const clap_plugin_t *q = de ? f->create_plugin(f, &host, de->id) : NULL;
        EXPECT(q && q->init(q), "sampler: create");
        if (q) {
            const clap_plugin_params_t *qp = q->get_extension(q, CLAP_EXT_PARAMS);
            const clap_plugin_state_t *qs = q->get_extension(q, CLAP_EXT_STATE);
            const clap_id KIT = 99998u;
            double analog = -1.0;
            char txt[64];
            EXPECT(qp->text_to_value(q, KIT, "synth_analog", &analog) && analog >= 1.0, "sampler: no synth_analog kit");
            EXPECT(qp->value_to_text(q, KIT, 0.0, txt, sizeof txt) && !strcmp(txt, "none"), "sampler: kit 0 is %s", txt);
            /* chosen while playing: the plugin asks for the main thread, loads it there, plays it after */
            EXPECT(q->activate(q, SR, 1, 4096) && q->start_processing(q), "sampler: activate");
            float pk = 0.0f;
            for (int b = 0; b < 40; b++) {
                events_t ev1 = {{NULL, ev_size, ev_get}, {0}, 0};
                ev1.in.ctx = &ev1;
                clap_event_param_value_t kv = pvalue(0, KIT, analog);
                clap_event_note_t on1 = note(0, CLAP_EVENT_NOTE_ON, 36, 0.9);
                if (b == 0)
                    ev1.ev[ev1.n++] = &kv.header;
                if (b == 2)
                    ev1.ev[ev1.n++] = &on1.header;
                if (b == 1) {
                    EXPECT(callback_requested, "sampler: no main thread callback requested for the kit");
                    callback_requested = 0;
                    q->on_main_thread(q);
                }
                float *outs[2] = {pl_L, pl_R};
                clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
                const clap_process_t pr = {(int64_t)b * BLOCK, BLOCK, NULL, NULL, &ab, 0, 1, &ev1.in, &out_events};
                q->process(q, &pr);
                for (int i = 0; b >= 2 && i < BLOCK; i++)
                    pk = fmaxf(pk, fabsf(pl_L[i]));
            }
            EXPECT(pk > 0.05f && pk <= 1.0f, "sampler: the kit does not play (peak %g)", (double)pk);
            /* the state names the kit; a fresh instance loads it back and plays it */
            mem_t ms = {0};
            clap_ostream_t oss = {&ms, mem_write};
            EXPECT(qs->save(q, &oss) && ms.buf && strstr(ms.buf, "\"kit\": \"synth_analog\""), "sampler: state without its kit");
            q->stop_processing(q);
            q->deactivate(q);
            q->destroy(q);
            const clap_plugin_t *r = f->create_plugin(f, &host, de->id);
            EXPECT(r && r->init(r), "sampler: second instance");
            if (r && ms.buf) {
                const clap_plugin_state_t *rs = r->get_extension(r, CLAP_EXT_STATE);
                const clap_plugin_params_t *rp = r->get_extension(r, CLAP_EXT_PARAMS);
                clap_istream_t iss = {&ms, mem_read};
                double kv = -1.0;
                EXPECT(rs->load(r, &iss) && rp->get_value(r, KIT, &kv) && kv == analog, "sampler: kit not restored (%g)", kv);
                EXPECT(r->activate(r, SR, 1, 4096) && r->start_processing(r), "sampler: activate again");
                float pk2 = 0.0f;
                for (int b = 0; b < 20; b++) {
                    events_t ev1 = {{NULL, ev_size, ev_get}, {0}, 0};
                    ev1.in.ctx = &ev1;
                    clap_event_note_t on1 = note(0, CLAP_EVENT_NOTE_ON, 38, 0.9);
                    if (b == 0)
                        ev1.ev[ev1.n++] = &on1.header;
                    float *outs[2] = {pl_L, pl_R};
                    clap_audio_buffer_t ab = {outs, NULL, 2, 0, 0};
                    const clap_process_t pr = {(int64_t)b * BLOCK, BLOCK, NULL, NULL, &ab, 0, 1, &ev1.in, &out_events};
                    r->process(r, &pr);
                    for (int i = 0; i < BLOCK; i++)
                        pk2 = fmaxf(pk2, fabsf(pl_L[i]));
                }
                EXPECT(pk2 > 0.05f, "sampler: restored kit does not play (peak %g)", (double)pk2);
                r->stop_processing(r);
                r->deactivate(r);
            }
            if (r)
                r->destroy(r);
            free(ms.buf);
        }
    }

    entry->deinit();
    printf("%s: %u parameters, peak %.1f dBFS, %d failure(s)\n", failures ? "FAILED" : "ok", np,
           20.0 * log10((double)peak + 1e-12), failures);
    return failures ? 1 : 0;
}
