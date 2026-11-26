/* synth_clap.c — CLAP plugin host of the engine (dsp/synth.h), a thin layer like synth_play and the web worklet:
 * notes and MIDI go to synth_midi with their sample offset, parameter events split the block where they fall,
 * the plugin state is a patch file (docs/patch_format.md, host/patch.c), parameters come from the descriptor
 * tables. Reference: the CLAP 1.2 headers (third_party/clap, MIT).
 *
 * One file, several plugins: "OASynth" plays any engine (it has an "engine" parameter), and there is one plugin per
 * engine ("OASynth va", "OASynth fm6"...) that plays that engine only and lists only its parameters. Same code, same
 * parameter ids, same state format (a patch of another engine is refused by a single-engine plugin).
 *
 * Parameters (stable ids, so automation survives an engine change): "engine" (ENGINE_ID); every parameter of
 * every engine, id = engine * 1000 + index, each engine keeping its own values; the global ones (master,
 * effects, tempo) and the 8 macros, id = SHARED_BASE + synth id. The other shared parameters (modulation
 * shapes and routes, parts) are not exposed but are kept and saved with the state.
 *
 * Threads: activate / deactivate / state run on the main thread while the audio thread is idle, or through a
 * hand-over: a state loaded while active waits in `pending` (flag HO_READY) until the start of the next
 * process(), which re-initializes the engine there (a one-off load spike, like any preset change); meanwhile
 * the main thread reads the values from `pending`. Every synth_* call on an instance is
 * therefore made by one thread at a time. Each plugin instance owns an engine instance (synth_create, memory
 * allocated here, on the main thread), so several plugins run side by side.
 *
 * Sample kits (the sampler engine): the "kit" parameter (KIT_ID, an enumeration: none, then the kits found when
 * the plugin is loaded) chooses one. Kits are .oidk files or directories of NN_name.wav files, in the bundle
 * (Contents/Resources/kits: the demo kits) and in ~/Library/Application Support/synth/kits (yours, which win).
 * A kit is read from the disk on the main thread only: a change from the audio thread asks the host for a main
 * thread callback, which loads it (host/kitfile.c, kept until the plugin is unloaded) and hands it over
 * (kit_ready); the audio thread gives it to the synth at the start of its next process. The state names the
 * kit ("kit": "synth_analog"). */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

#include "catalog.h"
#include "engines/engine.h"
#include "global_params.h"
#include "kitfile.h"
#include "mod/partmod.h"
#include "patch.h"
#include "synth.h"

#define ENGINE_ID 99999u
#define KIT_ID 99998u
#define MAX_KITS 64
#define SHARED_BASE 100000u
#define MAX_SHARED 512

typedef struct {
    int engine;
    float eng[SYNTH_ENGINE_COUNT][256]; /* physical values of every engine's parameters */
    float shared[MAX_SHARED];           /* physical values of the shared parameters (catalog_shared order) */
    int kit;                            /* index in kit_names, 0 = none */
} values_t;

/* the kits found by entry_init: kit_names[0] = "none" */
static char kit_names[1 + MAX_KITS][KITFILE_NAME];
static int kit_count_all = 1;
static int plays_kits(int fixed) { return fixed < 0 || engine_registry[fixed]->kit != NULL; }

typedef struct {
    clap_plugin_t plugin;
    synth_t *s;   /* this plugin's engine instance, in mem */
    int fixed;    /* the engine of a single-engine plugin, or -1 for the plugin that plays any engine */
    void *mem;
    const clap_host_t *host;
    const clap_host_params_t *host_params;
    double sr;
    int active;
    values_t v;               /* written by the audio thread while active, by the main thread otherwise */
    values_t pending;         /* a loaded state waiting for the audio thread */
    atomic_int pending_ready; /* HO_IDLE, HO_READY (pending is valid), HO_TAKING (the audio thread is copying it) */
    /* sample kit: the one the synth plays (audio thread while active), the next one loaded by the main thread */
    const uint8_t *kit_cur, *kit_next;
    size_t kit_cur_bytes, kit_next_bytes;
    atomic_int kit_ready;     /* HO_IDLE, HO_READY (kit_next is valid), HO_TAKING (the audio thread is copying it) */
    atomic_int kit_want;      /* kit index the audio thread asked for, -1 none (read by on_main_thread) */
    int kit_loaded;           /* index of the kit last handed over (main thread) */
} plug_t;


enum { HO_IDLE = 0, HO_READY, HO_TAKING };

/* the values as the main thread should see them: a state not yet taken by the audio thread comes first */
static const values_t *main_view(const plug_t *p)
{
    return atomic_load_explicit(&((plug_t *)p)->pending_ready, memory_order_acquire) == HO_READY ? &p->pending : &p->v;
}

/* ---------------- parameter table ---------------- */

static int shared_count(void) { return catalog_shared_count() < MAX_SHARED ? catalog_shared_count() : MAX_SHARED; }

static int exposed_shared(const param_desc_t *d, int sid)
{
    /* globals and the 8 macro knobs (not their CC numbers, macro<k>_cc) */
    return sid < SYNTH_PART_PARAM || (strncmp(d->name, "macro", 5) == 0 && !strchr(d->name, '_'));
}

/* index -> id for a plugin (fixed: its engine, or -1 = every engine); also used to count (*ok = 0 past the end) */
static clap_id param_id_at(uint32_t index, int fixed, int *ok)
{
    *ok = 1;
    if (fixed < 0) {
        if (index == 0)
            return ENGINE_ID;
        index--;
    }
    if (plays_kits(fixed)) {
        if (index == 0)
            return KIT_ID;
        index--;
    }
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        if (fixed >= 0 && e != fixed)
            continue;
        if (index < (uint32_t)engine_registry[e]->param_count)
            return (clap_id)(e * 1000 + (int)index);
        index -= (uint32_t)engine_registry[e]->param_count;
    }
    for (int k = 0; k < shared_count(); k++) {
        int sid;
        const param_desc_t *d = catalog_shared(k, &sid);
        if (!exposed_shared(d, sid))
            continue;
        if (index == 0)
            return SHARED_BASE + (clap_id)sid;
        index--;
    }
    *ok = 0;
    return 0;
}

/* descriptor of an id; *e = its engine (or -1), *slot = index into eng[e] or shared[] */
static const param_desc_t *param_of(clap_id id, int *e, int *slot)
{
    *e = -1;
    if (id >= SHARED_BASE) {
        for (int k = 0; k < shared_count(); k++) {
            int sid;
            const param_desc_t *d = catalog_shared(k, &sid);
            if ((clap_id)sid == id - SHARED_BASE) {
                *slot = k;
                return d;
            }
        }
        return NULL;
    }
    const int en = (int)(id / 1000u), k = (int)(id % 1000u);
    if (id == ENGINE_ID || en >= SYNTH_ENGINE_COUNT || k >= engine_registry[en]->param_count)
        return NULL;
    *e = en;
    *slot = k;
    return &engine_registry[en]->params[k];
}

static void values_default(values_t *v, int engine)
{
    v->engine = engine;
    v->kit = 0;
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++)
        for (int k = 0; k < engine_registry[e]->param_count && k < 256; k++)
            v->eng[e][k] = engine_registry[e]->params[k].def;
    for (int k = 0; k < shared_count(); k++) {
        int sid;
        v->shared[k] = catalog_shared(k, &sid)->def;
    }
}

/* the engine's own values over its defaults (what synth_set_engine leaves) */
static void apply_engine(plug_t *p, int e)
{
    const engine_ops_t *en = engine_registry[e];
    for (int k = 0; k < en->param_count; k++)
        if (p->v.eng[e][k] != en->params[k].def)
            synth_set_param(p->s, k, param_to_norm(&en->params[k], p->v.eng[e][k]));
}

/* the whole sound into the synth, as synth_render does for a patch: fresh init, engine, then only the values
 * that differ from the defaults (synth_init sets some shared ones differently from their descriptor, e.g. part 0
 * listens to every channel), so the plugin plays bit for bit like the WAV renders */
static void apply_all(plug_t *p)
{
    synth_init(p->s, (float)p->sr);
    synth_set_engine(p->s, (synth_engine_t)p->v.engine);
    apply_engine(p, p->v.engine);
    for (int k = 0; k < shared_count(); k++) {
        int sid;
        const param_desc_t *d = catalog_shared(k, &sid);
        if (p->v.shared[k] != d->def)
            synth_set_param(p->s, sid, param_to_norm(d, p->v.shared[k]));
    }
    synth_part_kit(p->s, 0, p->kit_cur, p->kit_cur_bytes); /* synth_init removed it */
}

/* main thread: loads kit k (0 = none) and hands it to the audio thread, or to the synth itself while inactive */
static void kit_load(plug_t *p, int k)
{
    size_t bytes = 0;
    const uint8_t *kit = k > 0 && k < kit_count_all ? kitfile_find(kit_names[k], &bytes) : NULL;
    p->kit_loaded = k;
    if (!p->active) {
        atomic_store_explicit(&p->kit_ready, HO_IDLE, memory_order_relaxed);
        p->kit_cur = kit;
        p->kit_cur_bytes = bytes;
        synth_part_kit(p->s, 0, kit, bytes);
        return;
    }
    /* take back a kit the audio thread has not taken yet (replaced); while it is copying one (two words), wait */
    for (int ready = HO_READY;;) {
        if (atomic_compare_exchange_strong_explicit(&p->kit_ready, &ready, HO_IDLE, memory_order_acq_rel,
                                                    memory_order_acquire) || ready == HO_IDLE)
            break;
        ready = HO_READY;
    }
    p->kit_next = kit;
    p->kit_next_bytes = bytes;
    atomic_store_explicit(&p->kit_ready, HO_READY, memory_order_release);
}

/* audio thread (or main thread while inactive): a parameter changes */
static void set_value(plug_t *p, clap_id id, double value)
{
    if (id == KIT_ID) {
        const int k = (int)(value + 0.5);
        if (!plays_kits(p->fixed) || k < 0 || k >= kit_count_all || k == p->v.kit)
            return;
        p->v.kit = k;
        if (p->active) { /* audio thread: the disk is read on the main thread */
            atomic_store_explicit(&p->kit_want, k, memory_order_release);
            p->host->request_callback(p->host);
        } else {
            kit_load(p, k);
        }
        return;
    }
    if (id == ENGINE_ID) {
        const int e = (int)(value + 0.5);
        if (p->fixed >= 0 || e < 0 || e >= SYNTH_ENGINE_COUNT || e == p->v.engine)
            return;
        p->v.engine = e;
        /* the routes lose their destination (an index into the previous engine's parameters), as in the synth */
        for (int k = 0; k < shared_count(); k++) {
            int sid;
            catalog_shared(k, &sid);
            for (int r = 0; r < PM_ROUTES; r++)
                if (sid == SYNTH_MOD_PARAM + PM_ROUTE(r, PM_R_DST))
                    p->v.shared[k] = 0.0f;
            for (int r = 0; r < PM_MAPS; r++)
                if (sid == SYNTH_MOD_PARAM + PM_MAP(r, PM_M_DST))
                    p->v.shared[k] = 0.0f;
        }
        if (p->active) { /* the engine starts with its own values */
            synth_set_engine(p->s, (synth_engine_t)e);
            apply_engine(p, e);
        }
        return;
    }
    int e, slot;
    const param_desc_t *d = param_of(id, &e, &slot);
    if (!d || !(value - value == 0.0) || (p->fixed >= 0 && e >= 0 && e != p->fixed))
        return; /* unknown id, NaN or infinite, or a parameter of an engine this plugin does not play */
    const float v = dsp_clampf((float)value, d->min, d->max);
    if (e >= 0) {
        p->v.eng[e][slot] = v;
        if (p->active && e == p->v.engine)
            synth_set_param(p->s, slot, param_to_norm(d, v));
    } else {
        p->v.shared[slot] = v;
        if (p->active)
            synth_set_param(p->s, (int)(id - SHARED_BASE), param_to_norm(d, v));
    }
}

/* ---------------- plugin ---------------- */

static bool plug_init(const clap_plugin_t *pl)
{
    plug_t *p = pl->plugin_data;
    p->host_params = p->host->get_extension(p->host, CLAP_EXT_PARAMS);
    values_default(&p->v, p->fixed >= 0 ? p->fixed : SYNTH_ENGINE_VA);
    atomic_init(&p->kit_ready, HO_IDLE);
    atomic_init(&p->kit_want, -1);
    return true;
}

static void plug_destroy(const clap_plugin_t *pl)
{
    plug_t *p = pl->plugin_data;
    free(p->mem);
    free(p);
}

static bool plug_activate(const clap_plugin_t *pl, double sr, uint32_t min_frames, uint32_t max_frames)
{
    (void)min_frames;
    (void)max_frames;
    plug_t *p = pl->plugin_data;
    p->sr = sr;
    if (atomic_exchange_explicit(&p->kit_ready, HO_IDLE, memory_order_acq_rel) == HO_READY) { /* handed over meanwhile */
        p->kit_cur = p->kit_next;
        p->kit_cur_bytes = p->kit_next_bytes;
    }
    p->active = 1;
    apply_all(p);
    return true;
}

static void plug_deactivate(const clap_plugin_t *pl)
{
    plug_t *p = pl->plugin_data;
    p->active = 0;
    /* the audio thread is idle: a state it never took becomes the current one (the next activate applies it) */
    if (atomic_exchange_explicit(&p->pending_ready, HO_IDLE, memory_order_acq_rel) == HO_READY)
        p->v = p->pending;
}
static bool plug_start(const clap_plugin_t *pl) { (void)pl; return true; }
static void plug_stop(const clap_plugin_t *pl) { (void)pl; }

static void plug_reset(const clap_plugin_t *pl)
{
    plug_t *p = pl->plugin_data;
    const uint8_t off[3] = {0xB0, 120, 0}; /* all sound off */
    synth_midi(p->s, off, 3, 0);
}

static void midi_from_note(plug_t *p, const clap_event_header_t *h, uint32_t offset)
{
    const clap_event_note_t *e = (const clap_event_note_t *)h;
    if (e->key == -1 && h->type != CLAP_EVENT_NOTE_ON) { /* wildcard: every key, of one channel or of all */
        const uint8_t cc = h->type == CLAP_EVENT_NOTE_CHOKE ? 120 : 123; /* all sound off / all notes off */
        for (int c = 0; c < 16; c++)
            if (e->channel == -1 || e->channel == c) {
                const uint8_t m[3] = {(uint8_t)(0xB0 | c), cc, 0};
                synth_midi(p->s, m, 3, (int)offset);
            }
        return;
    }
    if (e->key < 0 || e->key > 127)
        return;
    const int ch = e->channel >= 0 && e->channel < 16 ? e->channel : 0;
    int vel = (int)(e->velocity * 127.0 + 0.5);
    vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
    const uint8_t m[3] = {(uint8_t)((h->type == CLAP_EVENT_NOTE_ON ? 0x90 : 0x80) | ch), (uint8_t)e->key,
                          (uint8_t)(h->type == CLAP_EVENT_NOTE_ON ? vel : 0)};
    synth_midi(p->s, m, 3, (int)offset);
}

/* one input event; offset = its position from the start of the next synth_render */
static void handle_event(plug_t *p, const clap_event_header_t *h, uint32_t offset)
{
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;
    switch (h->type) {
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: /* one key: treated as a note-off (the release still plays) */
        midi_from_note(p, h, offset);
        break;
    case CLAP_EVENT_MIDI: {
        const clap_event_midi_t *e = (const clap_event_midi_t *)h;
        const uint8_t s = e->data[0];
        const int len = s >= 0xF8 ? 1 : (s & 0xF0) == 0xC0 || (s & 0xF0) == 0xD0 ? 2 : 3;
        synth_midi(p->s, e->data, len, (int)offset);
        break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        const clap_event_param_value_t *e = (const clap_event_param_value_t *)h;
        set_value(p, e->param_id, e->value);
        break;
    }
    default:
        break;
    }
}

static clap_process_status plug_process(const clap_plugin_t *pl, const clap_process_t *pr)
{
    plug_t *p = pl->plugin_data;
    int kready = HO_READY; /* a kit loaded by the main thread: take it */
    if (atomic_compare_exchange_strong_explicit(&p->kit_ready, &kready, HO_TAKING, memory_order_acq_rel,
                                                memory_order_acquire)) {
        p->kit_cur = p->kit_next;
        p->kit_cur_bytes = p->kit_next_bytes;
        atomic_store_explicit(&p->kit_ready, HO_IDLE, memory_order_release);
        synth_part_kit(p->s, 0, p->kit_cur, p->kit_cur_bytes);
    }
    int ready = HO_READY; /* a state loaded meanwhile: take it (the main thread keeps off while HO_TAKING) */
    if (atomic_compare_exchange_strong_explicit(&p->pending_ready, &ready, HO_TAKING, memory_order_acq_rel,
                                                memory_order_acquire)) {
        p->v = p->pending;
        apply_all(p);
        atomic_store_explicit(&p->pending_ready, HO_IDLE, memory_order_release);
    }
    const uint32_t n = pr->frames_count;
    if (pr->audio_outputs_count < 1 || pr->audio_outputs[0].channel_count < 2 || !pr->audio_outputs[0].data32)
        return CLAP_PROCESS_ERROR;
    float *L = pr->audio_outputs[0].data32[0], *R = pr->audio_outputs[0].data32[1];
    const uint32_t nev = pr->in_events->size(pr->in_events);
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < nev; i++) {
        const clap_event_header_t *h = pr->in_events->get(pr->in_events, i);
        uint32_t t = h->time < n ? h->time : n;
        if (t < cursor)
            t = cursor;
        if (h->type == CLAP_EVENT_PARAM_VALUE && t > cursor) { /* a parameter changes here: render up to it */
            synth_render(p->s, L + cursor, R + cursor, (int)(t - cursor));
            cursor = t;
        }
        handle_event(p, h, t - cursor);
    }
    if (n > cursor)
        synth_render(p->s, L + cursor, R + cursor, (int)(n - cursor));
    return CLAP_PROCESS_CONTINUE;
}

/* ---------------- extensions: ports ---------------- */

static uint32_t audio_ports_count(const clap_plugin_t *pl, bool is_input) { (void)pl; return is_input ? 0 : 1; }
static bool audio_ports_get(const clap_plugin_t *pl, uint32_t index, bool is_input, clap_audio_port_info_t *info)
{
    (void)pl;
    if (is_input || index > 0)
        return false;
    memset(info, 0, sizeof *info);
    info->id = 0;
    snprintf(info->name, sizeof info->name, "Main out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t ext_audio_ports = {audio_ports_count, audio_ports_get};

static uint32_t note_ports_count(const clap_plugin_t *pl, bool is_input) { (void)pl; return is_input ? 1 : 0; }
static bool note_ports_get(const clap_plugin_t *pl, uint32_t index, bool is_input, clap_note_port_info_t *info)
{
    (void)pl;
    if (!is_input || index > 0)
        return false;
    memset(info, 0, sizeof *info);
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    snprintf(info->name, sizeof info->name, "Notes in");
    return true;
}
static const clap_plugin_note_ports_t ext_note_ports = {note_ports_count, note_ports_get};

/* ---------------- extension: params ---------------- */

static uint32_t params_count(const clap_plugin_t *pl)
{
    const plug_t *p = pl->plugin_data;
    uint32_t n = 0;
    int ok;
    while (param_id_at(n, p->fixed, &ok), ok)
        n++;
    return n;
}

static bool params_info(const clap_plugin_t *pl, uint32_t index, clap_param_info_t *info)
{
    int ok, e, slot;
    const clap_id id = param_id_at(index, ((const plug_t *)pl->plugin_data)->fixed, &ok);
    if (!ok)
        return false;
    memset(info, 0, sizeof *info);
    info->id = id;
    if (id == ENGINE_ID) {
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name, sizeof info->name, "engine");
        info->min_value = 0.0;
        info->max_value = SYNTH_ENGINE_COUNT - 1;
        info->default_value = SYNTH_ENGINE_VA;
        return true;
    }
    if (id == KIT_ID) {
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
        snprintf(info->name, sizeof info->name, "kit");
        snprintf(info->module, sizeof info->module, "sampler");
        info->min_value = 0.0;
        info->max_value = kit_count_all - 1;
        info->default_value = 0.0;
        return true;
    }
    const param_desc_t *d = param_of(id, &e, &slot);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d->curve == PARAM_INT || d->labels ? CLAP_PARAM_IS_STEPPED : 0) |
                  (d->labels ? CLAP_PARAM_IS_ENUM : 0);
    snprintf(info->name, sizeof info->name, "%s", d->name);
    snprintf(info->module, sizeof info->module, "%s", e >= 0 ? engine_registry[e]->name : "global");
    info->min_value = (double)d->min;
    info->max_value = (double)d->max;
    info->default_value = (double)d->def;
    return true;
}

static bool params_value(const clap_plugin_t *pl, clap_id id, double *out)
{
    const values_t *v = main_view(pl->plugin_data);
    if (id == ENGINE_ID) {
        *out = v->engine;
        return true;
    }
    if (id == KIT_ID) {
        *out = v->kit;
        return true;
    }
    int e, slot;
    if (!param_of(id, &e, &slot))
        return false;
    *out = (double)(e >= 0 ? v->eng[e][slot] : v->shared[slot]);
    return true;
}

static bool params_to_text(const clap_plugin_t *pl, clap_id id, double value, char *buf, uint32_t cap)
{
    (void)pl;
    if (id == ENGINE_ID) {
        const int e = (int)(value + 0.5);
        snprintf(buf, cap, "%s", e >= 0 && e < SYNTH_ENGINE_COUNT ? engine_registry[e]->name : "?");
        return true;
    }
    if (id == KIT_ID) {
        const int k = (int)(value + 0.5);
        snprintf(buf, cap, "%s", k >= 0 && k < kit_count_all ? kit_names[k] : "?");
        return true;
    }
    int e, slot;
    const param_desc_t *d = param_of(id, &e, &slot);
    if (!d)
        return false;
    const int i = (int)(value - (double)d->min + 0.5);
    if (d->labels) {
        int n = 0;
        while (d->labels[n])
            n++;
        snprintf(buf, cap, "%s", i >= 0 && i < n ? d->labels[i] : "?");
    } else {
        snprintf(buf, cap, "%.4g%s%s", value, d->unit && d->unit[0] ? " " : "", d->unit ? d->unit : "");
    }
    return true;
}

static bool params_from_text(const clap_plugin_t *pl, clap_id id, const char *text, double *out)
{
    (void)pl;
    if (id == ENGINE_ID) {
        const engine_ops_t *en = engine_find(text);
        if (!en)
            return false;
        *out = engine_id(en);
        return true;
    }
    if (id == KIT_ID) {
        for (int k = 0; k < kit_count_all; k++)
            if (!strcmp(text, kit_names[k])) {
                *out = k;
                return true;
            }
        return false;
    }
    int e, slot;
    const param_desc_t *d = param_of(id, &e, &slot);
    if (!d)
        return false;
    const int k = d->labels ? param_label(d, text) : -1;
    if (k >= 0) {
        *out = (double)d->min + k;
        return true;
    }
    char *end;
    *out = strtod(text, &end);
    return end != text;
}

static void params_flush(const clap_plugin_t *pl, const clap_input_events_t *in, const clap_output_events_t *out)
{
    (void)out;
    plug_t *p = pl->plugin_data;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; i++) {
        const clap_event_header_t *h = in->get(in, i);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
            set_value(p, ((const clap_event_param_value_t *)h)->param_id, ((const clap_event_param_value_t *)h)->value);
    }
}

static const clap_plugin_params_t ext_params = {params_count, params_info, params_value, params_to_text,
                                               params_from_text, params_flush};

/* ---------------- extension: state = a patch file ---------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} text_t;

static void add(text_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void add(text_t *t, const char *fmt, ...)
{
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        const int w = vsnprintf(t->buf ? t->buf + t->len : NULL, t->buf ? t->cap - t->len : 0, fmt, ap);
        va_end(ap);
        if (w < 0)
            return;
        if (t->buf && t->len + (size_t)w < t->cap) {
            t->len += (size_t)w;
            return;
        }
        t->cap = (t->cap + (size_t)w + 1) * 2;
        char *nb = realloc(t->buf, t->cap);
        if (!nb)
            return;
        t->buf = nb;
    }
}

/* value as written in a patch: a label for enumerations, a parameter name for route destinations */
/* a value as JSON text: a route's or a macro target's destination by role, a label, or a number */
static void value_text(char out[64], const engine_ops_t *en, const param_desc_t *d, float v)
{
    if ((strncmp(d->name, "route", 5) == 0 || strncmp(d->name, "map", 3) == 0) && strstr(d->name, "_destination")) {
        const int k = (int)(v + 0.5f) - 1;
        const param_desc_t *g = k >= SYNTH_PARAM_GLOBAL ? synth_global_param(k - SYNTH_PARAM_GLOBAL) : NULL;
        if (k >= 0 && k < en->param_count)
            snprintf(out, 64, "\"%s\"", en->params[k].role);
        else if (g) /* a macro target on the master or an effect */
            snprintf(out, 64, "\"%s\"", g->role);
        else
            snprintf(out, 64, "0");
    } else if (d->labels) {
        int n = 0;
        while (d->labels[n])
            n++;
        snprintf(out, 64, "\"%s\"", d->labels[dsp_clampi((int)(v - d->min + 0.5f), 0, n - 1)]);
    } else { /* 9 significant digits give the float back exactly; '.' whatever the host's locale */
        snprintf(out, 64, "%.9g", (double)v);
        for (char *c = out; *c; c++)
            if (*c == ',')
                *c = '.';
    }
}

static void put_text(void *ctx, const char *text) { add(ctx, "%s", text); }

static bool state_save(const clap_plugin_t *pl, const clap_ostream_t *stream)
{
    /* a snapshot: the audio thread may change values (automation) while this runs; the patch format holds one
     * engine, so the other engines' values are not saved (they come back at their defaults) */
    values_t *v = malloc(sizeof *v);
    if (!v)
        return false;
    *v = *main_view(pl->plugin_data);
    v->engine = dsp_clampi(v->engine, 0, SYNTH_ENGINE_COUNT - 1);
    const engine_ops_t *en = engine_registry[v->engine];
    text_t t = {0};
    add(&t, "{\n  \"version\": %d,\n  \"engine\": \"%s\",\n  \"name\": \"clap_state\"", PATCH_VERSION, en->name);
    if (v->kit > 0 && v->kit < kit_count_all)
        add(&t, ",\n  \"kit\": \"%s\"", kit_names[v->kit]);
    patch_setting_t *set = malloc(sizeof *set * PATCH_MAX_PARAMS);
    int n = 0;
    for (int k = 0; set && k < en->param_count && n < PATCH_MAX_PARAMS; k++) {
        const param_desc_t *d = &en->params[k];
        if (v->eng[v->engine][k] == d->def)
            continue;
        set[n].role = d->role;
        value_text(set[n++].value, en, d, v->eng[v->engine][k]);
    }
    for (int k = 0; set && k < shared_count() && n < PATCH_MAX_PARAMS; k++) {
        int sid;
        const param_desc_t *d = catalog_shared(k, &sid);
        if (v->shared[k] == d->def)
            continue;
        set[n].role = d->role;
        value_text(set[n++].value, en, d, v->shared[k]);
    }
    if (set && n) {
        add(&t, ",\n");
        patch_write_settings(put_text, &t, set, n, 2);
    }
    free(set);
    add(&t, "\n}\n");
    free(v);
    if (!t.buf)
        return false;
    for (size_t done = 0; done < t.len;) {
        const int64_t w = stream->write(stream, t.buf + done, t.len - done);
        if (w <= 0) {
            free(t.buf);
            return false;
        }
        done += (size_t)w;
    }
    free(t.buf);
    return true;
}

static bool state_load(const clap_plugin_t *pl, const clap_istream_t *stream)
{
    plug_t *p = pl->plugin_data;
    text_t t = {0};
    char chunk[4096];
    for (;;) {
        const int64_t r = stream->read(stream, chunk, sizeof chunk);
        if (r < 0) {
            free(t.buf);
            return false;
        }
        if (r == 0)
            break;
        add(&t, "%.*s", (int)r, chunk);
    }
    if (!t.buf)
        return false;
    patch_t *patch = malloc(sizeof *patch);
    char err[1024], warn[1024];
    const int rc = patch ? patch_parse(t.buf, patch, err, sizeof err, warn, sizeof warn) : -1;
    free(t.buf);
    if (rc != 0 || (p->fixed >= 0 && engine_id(patch->engine) != p->fixed)) { /* not a patch of this instrument */
        free(patch);
        return false;
    }
    /* take back a state the audio thread has not taken yet (it is replaced); while it is copying one (a few
     * microseconds), wait for it */
    for (int ready = HO_READY; p->active;) {
        if (atomic_compare_exchange_strong_explicit(&p->pending_ready, &ready, HO_IDLE, memory_order_acq_rel,
                                                    memory_order_acquire) || ready == HO_IDLE)
            break;
        ready = HO_READY;
    }
    values_t *v = p->active ? &p->pending : &p->v;
    values_default(v, engine_id(patch->engine));
    for (int i = 0; i < patch->n; i++) {
        const int id = patch->ids[i];
        if (id < SYNTH_PARAM_GLOBAL) {
            if (id < 256)
                v->eng[v->engine][id] = patch->values[i];
            continue;
        }
        for (int k = 0; k < shared_count(); k++) {
            int sid;
            catalog_shared(k, &sid);
            if (sid == id) {
                v->shared[k] = patch->values[i];
                break;
            }
        }
    }
    int kit = 0; /* the kit it names, if this plugin found it */
    for (int k = 1; k < kit_count_all && patch->kit[0]; k++)
        if (!strcmp(patch->kit, kit_names[k]))
            kit = k;
    v->kit = kit;
    free(patch);
    if (kit != p->kit_loaded)
        kit_load(p, kit); /* main thread: the audio thread takes it at its next process, before the state */
    atomic_store_explicit(&p->pending_ready, p->active ? HO_READY : HO_IDLE, memory_order_release);
    if (p->host_params)
        p->host_params->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

static const clap_plugin_state_t ext_state = {state_save, state_load};

static const void *plug_extension(const clap_plugin_t *pl, const char *id)
{
    (void)pl;
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
        return &ext_audio_ports;
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
        return &ext_note_ports;
    if (!strcmp(id, CLAP_EXT_PARAMS))
        return &ext_params;
    if (!strcmp(id, CLAP_EXT_STATE))
        return &ext_state;
    return NULL;
}

/* the audio thread asked for a kit (set_value): load it here */
static void plug_on_main_thread(const clap_plugin_t *pl)
{
    plug_t *p = pl->plugin_data;
    const int k = atomic_exchange_explicit(&p->kit_want, -1, memory_order_acq_rel);
    if (k >= 0 && k != p->kit_loaded)
        kit_load(p, k);
}

/* ---------------- factory and entry ---------------- */

/* Plugin 0 plays any engine; plugin 1 + e plays engine e only. Their descriptors are built once by entry_init. */
#define PLUGIN_COUNT (1 + SYNTH_ENGINE_COUNT)

static const char *const features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                       CLAP_PLUGIN_FEATURE_STEREO, NULL};
static const char *const drum_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
                                            CLAP_PLUGIN_FEATURE_STEREO, NULL};
static clap_plugin_descriptor_t descriptors[PLUGIN_COUNT];
static char plugin_ids[PLUGIN_COUNT][48], plugin_names[PLUGIN_COUNT][48];

static void build_descriptors(void)
{
    for (int k = 0; k < PLUGIN_COUNT; k++) {
        const engine_ops_t *en = k ? engine_registry[k - 1] : NULL;
        /* "synth.engine" is the id the all-engine plugin has had from the start: projects keep loading */
        snprintf(plugin_ids[k], sizeof plugin_ids[k], "synth.%s", en ? en->name : "engine");
        snprintf(plugin_names[k], sizeof plugin_names[k], en ? "OASynth %s" : "OASynth", en ? en->name : "");
        descriptors[k] = (clap_plugin_descriptor_t){
            .clap_version = CLAP_VERSION_INIT,
            .id = plugin_ids[k],
            .name = plugin_names[k],
            .vendor = "Choc",
            .url = "https://github.com/vincentchoqueuse/openaudiosynth",
            .manual_url = "https://github.com/vincentchoqueuse/openaudiosynth/blob/main/plugin/README.md",
            .support_url = "https://github.com/vincentchoqueuse/openaudiosynth/issues",
            .version = "0.1.0",
            .description = en ? en->description
                              : "Every engine in one plugin (va, mono, poly, wt, fm6, modal, drums, sampler, "
                                "drone, micro), chosen by the engine parameter; the state is a patch file",
            .features = (const char **)(en && (!strcmp(en->name, "drums") || !strcmp(en->name, "sampler")) ? drum_features : features),
        };
    }
}

static uint32_t factory_count(const clap_plugin_factory_t *f) { (void)f; return PLUGIN_COUNT; }
static const clap_plugin_descriptor_t *factory_descriptor(const clap_plugin_factory_t *f, uint32_t i)
{
    (void)f;
    return i < PLUGIN_COUNT ? &descriptors[i] : NULL;
}

static const clap_plugin_t *factory_create(const clap_plugin_factory_t *f, const clap_host_t *host, const char *id)
{
    (void)f;
    int k = 0;
    while (k < PLUGIN_COUNT && strcmp(id, descriptors[k].id) != 0)
        k++;
    if (!clap_version_is_compatible(host->clap_version) || k == PLUGIN_COUNT)
        return NULL;
    plug_t *p = calloc(1, sizeof *p);
    const size_t bytes = (synth_size() + 15u) & ~(size_t)15u;
    if (p)
        p->mem = aligned_alloc(16, bytes);
    if (!p || !p->mem || !(p->s = synth_create(p->mem, bytes, 48000.0f))) { /* also builds the shared tables */
        if (p)
            free(p->mem);
        free(p);
        return NULL;
    }
    p->host = host;
    p->sr = 48000.0;
    p->fixed = k - 1;
    p->plugin = (clap_plugin_t){
        .desc = &descriptors[k],
        .plugin_data = p,
        .init = plug_init,
        .destroy = plug_destroy,
        .activate = plug_activate,
        .deactivate = plug_deactivate,
        .start_processing = plug_start,
        .stop_processing = plug_stop,
        .reset = plug_reset,
        .process = plug_process,
        .get_extension = plug_extension,
        .on_main_thread = plug_on_main_thread,
    };
    return &p->plugin;
}

static const clap_plugin_factory_t factory = {factory_count, factory_descriptor, factory_create};

/* the kits: in the bundle (the demo kits, Contents/Resources/kits), then the user's, which win */
static void find_kits(const char *path)
{
    char dir[2048];
    /* CLAP hands the bundle's path; a host may hand its binary's (bundle/Contents/MacOS/synth) */
    const char *inside = path ? strstr(path, "/Contents/MacOS/") : NULL;
    snprintf(dir, sizeof dir, "%.*s/Contents/Resources/kits", inside ? (int)(inside - path) : (int)strlen(path ? path : "."),
             path ? path : ".");
    kitfile_add_search_dir(dir);
    const char *slash = path ? strrchr(path, '/') : NULL; /* not a bundle (Linux, Windows): kits/ next to it */
    snprintf(dir, sizeof dir, "%.*s/kits", slash ? (int)(slash - path) : 1, slash ? path : ".");
    kitfile_add_search_dir(dir);
    const char *home = getenv("HOME");
    if (home) {
        snprintf(dir, sizeof dir, "%s/Library/Application Support/synth/kits", home);
        kitfile_add_search_dir(dir);
    }
    snprintf(kit_names[0], KITFILE_NAME, "none");
    kit_count_all = 1 + kitfile_list(kit_names + 1, MAX_KITS);
}

static bool entry_init(const char *path)
{
    build_descriptors();
    find_kits(path);
    return true;
}
static void entry_deinit(void) {}
static const void *entry_factory(const char *id) { return strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? NULL : &factory; }

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, entry_factory};
