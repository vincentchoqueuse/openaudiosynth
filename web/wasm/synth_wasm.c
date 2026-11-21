/* synth_wasm.c — WebAssembly bindings of the public API (dsp/synth.h) for the browser AudioWorklet:
 * render buffers, MIDI, parameters, the descriptor catalog as JSON, patch loading (host/patch.c)
 * and a sample-accurate player for patch auditions and the step sequencer (one phrase per row, replaced without
 * restarting the loop). Everything runs on the worklet thread, as the
 * API requires. Allocation happens only in the loaders (catalog, patch, audition), never in w_render. */
#include <emscripten.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "core/kit.h"
#include "global_params.h"
#include "demos.h"
#include "json.h"
#include "patch.h"
#include "song.h"
#include "synth.h"

#define W_MAX_FRAMES 1024 /* render quantum of Web Audio is 128; room for larger ones */

static float g_L[W_MAX_FRAMES], g_R[W_MAX_FRAMES];
static float g_sr = 48000.0f;
static int g_engine = SYNTH_ENGINE_VA;
static char g_err[2048], g_warn[2048];
static patch_t g_patch;
static int g_have_patch;

/* audition player: events sorted by time, frame counter since start */
static seq_event_t *g_ev;
static int g_ev_n, g_ev_next, g_loop;
static long g_pos, g_len;
static uint8_t g_on[128]; /* notes the player has started and not yet released */

/* ---------------- render, MIDI, parameters ---------------- */

EMSCRIPTEN_KEEPALIVE float *w_buf_l(void) { return g_L; }
EMSCRIPTEN_KEEPALIVE float *w_buf_r(void) { return g_R; }
EMSCRIPTEN_KEEPALIVE int w_max_frames(void) { return W_MAX_FRAMES; }
EMSCRIPTEN_KEEPALIVE const char *w_error(void) { return g_err; }
EMSCRIPTEN_KEEPALIVE const char *w_warning(void) { return g_warn; }
EMSCRIPTEN_KEEPALIVE int w_engine(void) { return g_engine; }

EMSCRIPTEN_KEEPALIVE void w_init(float sample_rate)
{
    g_sr = sample_rate;
    synth_init(synth_default(), sample_rate);
    g_engine = SYNTH_ENGINE_VA;
}

EMSCRIPTEN_KEEPALIVE void w_set_engine(int e)
{
    if (e < 0 || e >= SYNTH_ENGINE_COUNT)
        return;
    g_engine = e;
    synth_set_engine(synth_default(), (synth_engine_t)e);
}

EMSCRIPTEN_KEEPALIVE void w_set_param(int id, float norm) { synth_set_param(synth_default(), id, norm); }
/* A parameter of one part (the song player's tracks: level, mute), normalized like w_set_param. */
EMSCRIPTEN_KEEPALIVE void w_part_param(int part, int id, float norm) { synth_part_param(synth_default(), part, id, norm); }

EMSCRIPTEN_KEEPALIVE void w_midi(int status, int d1, int d2, int len)
{
    const uint8_t m[3] = {(uint8_t)status, (uint8_t)d1, (uint8_t)d2};
    synth_midi(synth_default(), m, len, 0);
}

static void play_events(int n)
{
    if (!g_ev)
        return;
    for (;;) {
        while (g_ev_next < g_ev_n) {
            const seq_event_t *e = &g_ev[g_ev_next];
            const long f = (long)(e->t * g_sr);
            if (f >= g_pos + n)
                break;
            synth_midi(synth_default(), e->msg, e->len, f > g_pos ? (int)(f - g_pos) : 0);
            if ((e->msg[0] & 0xF0) == 0x90 && e->msg[2])
                g_on[e->msg[1] & 127] = 1;
            else if ((e->msg[0] & 0xF0) == 0x80 || (e->msg[0] & 0xF0) == 0x90)
                g_on[e->msg[1] & 127] = 0;
            g_ev_next++;
        }
        if (g_ev_next < g_ev_n || g_pos + n < g_len)
            break;
        if (!g_loop) { /* phrase over (every note-off sent): stop the player */
            free(g_ev);
            g_ev = NULL;
            return;
        }
        g_pos -= g_len; /* loop: restart the phrase, events of the new turn may fall in this quantum */
        g_ev_next = 0;
    }
    g_pos += n;
}

EMSCRIPTEN_KEEPALIVE void w_render(int n)
{
    if (n <= 0)
        return;
    if (n > W_MAX_FRAMES)
        n = W_MAX_FRAMES;
    play_events(n);
    synth_render(synth_default(), g_L, g_R, n);
}

/* ---------------- descriptors ---------------- */

static const param_desc_t *desc_of(int id)
{
    const engine_ops_t *e = engine_registry[g_engine];
    if (id >= 0 && id < e->param_count)
        return &e->params[id];
    for (int k = 0; k < catalog_shared_count(); k++) {
        int sid;
        const param_desc_t *d = catalog_shared(k, &sid);
        if (sid == id)
            return d;
    }
    return NULL;
}

/* Physical value <-> normalized value of parameter id of the current engine (or a shared one). */
EMSCRIPTEN_KEEPALIVE float w_to_norm(int id, float value)
{
    const param_desc_t *d = desc_of(id);
    return d ? param_to_norm(d, value) : 0.0f;
}
EMSCRIPTEN_KEEPALIVE float w_from_norm(int id, float norm)
{
    const param_desc_t *d = desc_of(id);
    return d ? param_from_norm(d, norm) : 0.0f;
}

/* ---------------- catalog as JSON ---------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} sbuf_t;

static void put(sbuf_t *b, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        const int w = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (w < 0)
            return;
        if (b->len + (size_t)w < b->cap) {
            b->len += (size_t)w;
            return;
        }
        b->cap = b->cap * 2 + (size_t)w;
        b->buf = realloc(b->buf, b->cap);
    }
}

static void put_str(sbuf_t *b, const char *s)
{
    if (!s) {
        put(b, "null");
        return;
    }
    put(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            put(b, "\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            put(b, " ");
        else
            put(b, "%c", *s);
    }
    put(b, "\"");
}

static void put_param(sbuf_t *b, int id, const param_desc_t *d)
{
    static const char *const curves[] = {"lin", "exp", "db", "int"};
    put(b, "{\"id\":%d,\"name\":", id);
    put_str(b, d->name);
    put(b, ",\"min\":%.9g,\"max\":%.9g,\"def\":%.9g,\"curve\":\"%s\",\"realtime\":%s,\"unit\":", (double)d->min,
        (double)d->max, (double)d->def, d->curve < 4 ? curves[d->curve] : "lin", d->realtime ? "true" : "false");
    put_str(b, d->unit);
    put(b, ",\"role\":");
    put_str(b, d->role);
    put(b, ",\"help\":");
    put_str(b, d->help);
    put(b, ",\"labels\":");
    if (d->labels) {
        put(b, "[");
        for (int i = 0; d->labels[i]; i++) {
            put(b, i ? "," : "");
            put_str(b, d->labels[i]);
        }
        put(b, "]");
    } else {
        put(b, "null");
    }
    put(b, "}");
}

static void put_stages(sbuf_t *b, const engine_stage_t *st, int n)
{
    put(b, "[");
    for (int k = 0; k < n; k++) {
        const engine_stage_t *s = &st[k];
        put(b, "%s{\"id\":", k ? "," : "");
        put_str(b, s->id);
        put(b, ",\"label\":");
        put_str(b, s->label);
        put(b, ",\"kind\":");
        put_str(b, s->kind);
        put(b, ",\"params\":");
        put_str(b, s->params);
        put(b, ",\"inputs\":");
        put_str(b, s->inputs ? s->inputs : "");
        put(b, ",\"targets\":");
        put_str(b, s->targets ? s->targets : "");
        put(b, ",\"enable\":");
        put_str(b, s->enable);
        put(b, ",\"off\":%.9g,\"on\":%.9g}", (double)s->off, (double)s->on);
    }
    put(b, "]");
}

/* {"engines":[{"id","name","description","voices","params":[...],"stages":[...]}], "shared":[...], "fx":[...]};
 * ids are the synth_set_param ids; stages describe each signal chain (engine.h). Built once, owned by the module. */
EMSCRIPTEN_KEEPALIVE const char *w_catalog(void)
{
    static sbuf_t b;
    if (b.buf)
        return b.buf;
    b.cap = 1 << 16;
    b.buf = malloc(b.cap);
    b.buf[0] = 0;
    put(&b, "{\"block\":%d,\"engines\":[", SYNTH_BLOCK);
    for (int i = 0; i < SYNTH_ENGINE_COUNT; i++) {
        const engine_ops_t *e = engine_registry[i];
        put(&b, "%s{\"id\":%d,\"name\":", i ? "," : "", i);
        put_str(&b, e->name);
        put(&b, ",\"description\":");
        put_str(&b, e->description);
        put(&b, ",\"voices\":%d,\"params\":[", e->voices);
        for (int k = 0; k < e->param_count; k++) {
            put(&b, k ? "," : "");
            put_param(&b, k, &e->params[k]);
        }
        put(&b, "],\"stages\":");
        put_stages(&b, e->stages, e->stages ? e->stage_count : 0);
        put(&b, "}");
    }
    put(&b, "],\"shared\":[");
    for (int k = 0; k < catalog_shared_count(); k++) {
        int id;
        const param_desc_t *d = catalog_shared(k, &id);
        put(&b, k ? "," : "");
        put_param(&b, id, d);
    }
    put(&b, "],\"fx\":");
    put_stages(&b, synth_fx_stages, synth_fx_stage_count);
    put(&b, "}");
    return b.buf;
}

/* ---------------- patches ---------------- */

static uint8_t *g_kit; /* the sample kit part 0 plays (w_alloc'ed by the worklet, w_kit), kept across patch loads */
static int g_kit_bytes;

EMSCRIPTEN_KEEPALIVE char *w_alloc(int n) { return malloc((size_t)n); }
EMSCRIPTEN_KEEPALIVE void w_free(void *p) { free(p); }

/* Loads a patch (JSON text) exactly like synth_render does: fresh synth_init, then the engine and the
 * patch parameters; a running player keeps its place. Returns 0, or -1 (message in w_error). Clamped values are
 * reported in w_warning. */
EMSCRIPTEN_KEEPALIVE int w_patch(const char *json)
{
    if (patch_parse(json, &g_patch, g_err, sizeof g_err, g_warn, sizeof g_warn) != 0) {
        g_have_patch = 0;
        return -1;
    }
    /* the player goes on (a sequence keeps looping under the new sound): synth_init silences every note, and
     * the next ones start with the new sound */
    memset(g_on, 0, sizeof g_on); /* nothing left to release */
    g_have_patch = 1;
    synth_init(synth_default(), g_sr);
    synth_part_kit(synth_default(), 0, g_kit, (size_t)g_kit_bytes); /* synth_init removed it */
    w_set_engine(engine_id(g_patch.engine));
    for (int i = 0; i < g_patch.n; i++)
        synth_set_param(synth_default(), g_patch.ids[i], param_to_norm(g_patch.desc[i], g_patch.values[i]));
    return 0;
}

/* ---------------- sample kits ---------------- */


/* Gives the synth a kit (dsp/core/kit.h) the worklet copied into memory from w_alloc; the previous one is
 * freed once the synth no longer reads it. Returns its number of samples, -1 if invalid (then not kept). */
EMSCRIPTEN_KEEPALIVE int w_kit(uint8_t *kit, int bytes)
{
    const int n = kit ? kit_count(kit, (size_t)bytes) : 0;
    if (n < 0) {
        free(kit);
        return -1;
    }
    synth_part_kit(synth_default(), 0, kit, kit ? (size_t)bytes : 0);
    free(g_kit);
    g_kit = kit;
    g_kit_bytes = kit ? bytes : 0;
    return n;
}

/* The kit the last patch names ("" none): the page fetches it and hands it over with w_kit. */
EMSCRIPTEN_KEEPALIVE const char *w_patch_kit(void) { return g_have_patch ? g_patch.kit : ""; }

/* Parameters set by the last patch: count, then id and physical value of entry i. */
EMSCRIPTEN_KEEPALIVE int w_patch_count(void) { return g_have_patch ? g_patch.n : 0; }
EMSCRIPTEN_KEEPALIVE int w_patch_id(int i) { return g_patch.ids[i]; }
EMSCRIPTEN_KEEPALIVE float w_patch_value(int i) { return g_patch.values[i]; }
EMSCRIPTEN_KEEPALIVE const char *w_patch_name(void) { return g_patch.name; }

static int cmp_ev(const void *a, const void *b)
{
    const seq_event_t *x = a, *y = b;
    return x->t < y->t ? -1 : x->t > y->t ? 1 : x->order - y->order;
}

/* Releases the notes the player left sounding. */
static void release_player_notes(void)
{
    for (int k = 0; k < 128; k++)
        if (g_on[k]) {
            w_midi(0x80, k, 0, 3);
            g_on[k] = 0;
        }
}

#define W_MIN_PHRASE_S 0.01f   /* shorter phrases cannot loop (the player would spin) */
#define W_MAX_PHRASE_S 3600.0f /* keeps frame counts well inside 32 bits */

/* Hands a built sequence (len seconds) to the player, which takes ownership of its events; the notes the old
 * one left sounding are released. keep_pos: a running loop goes on from the same position (no restart).
 * When looping, events past the end (a slide or humanized note-off of the last step) wrap to the start of the
 * next turn, so the loop turns exactly at len. Returns len, or -1 (message in w_error). */
static float player_start(seq_t *q, float len, int loop, int keep_pos)
{
    if (!(len >= W_MIN_PHRASE_S && len <= W_MAX_PHRASE_S)) {
        snprintf(g_err, sizeof g_err, "phrase length %.3g s: %.2g..%.0f s expected", (double)len,
                 (double)W_MIN_PHRASE_S, (double)W_MAX_PHRASE_S);
        seq_free(q);
        return -1.0f;
    }
    if (loop)
        for (int i = 0; i < q->n; i++)
            while (q->ev[i].t >= len)
                q->ev[i].t -= len;
    qsort(q->ev, (size_t)q->n, sizeof q->ev[0], cmp_ev);
    const long new_len = (long)(len * g_sr);
    long pos = keep_pos && g_ev && g_loop && loop ? g_pos % new_len : 0;
    if (pos < 0)
        pos = 0;
    release_player_notes();
    free(g_ev);
    g_ev = q->ev;
    g_ev_n = q->n;
    g_ev_next = 0;
    while (g_ev_next < g_ev_n && (long)(g_ev[g_ev_next].t * g_sr) < pos)
        g_ev_next++;
    g_pos = pos;
    g_len = new_len;
    g_loop = loop;
    return len;
}

/* Plays the audition phrase of the last patch (or notes, same syntax, if not empty). loop != 0
 * repeats it. Returns its length in seconds, or -1 (message in w_error). */
EMSCRIPTEN_KEEPALIVE float w_audition(const char *notes, int loop)
{
    if (!g_have_patch) {
        snprintf(g_err, sizeof g_err, "no patch loaded");
        return -1.0f;
    }
    seq_t s;
    seq_init(&s);
    const float len = patch_audition(&g_patch, notes && notes[0] ? notes : NULL, &s, g_err, sizeof g_err);
    if (len < 0.0f) {
        seq_free(&s);
        return -1.0f;
    }
    return player_start(&s, len, loop, 0);
}

/* Step sequencer: rows separated by '\n', each a phrase in the audition syntax (so every row has its own ties),
 * played at bpm with step beats per token and gate. If the player is already looping, the new sequence takes
 * over at the same position (no restart): only the notes in flight are released. Returns the length in
 * seconds, or -1 (message in w_error). Uses the loaded patch's velocity, else 100. */
EMSCRIPTEN_KEEPALIVE float w_sequence(const char *rows, float bpm, float step, float gate, float swing, int loop)
{
    static patch_t p; /* large: not on the stack */
    if (g_have_patch)
        p = g_patch;
    else {
        memset(&p, 0, sizeof p);
        p.velocity = 100;
    }
    p.bpm = bpm < 20.0f ? 20.0f : bpm > 400.0f ? 400.0f : bpm;
    p.step_beats = step >= 1.0f / 64.0f && step <= 64.0f ? step : 0.25f;
    p.gate = gate > 0.02f && gate <= 1.0f ? gate : 0.8f;
    p.swing = swing >= 0.0f && swing <= 0.5f ? swing : 0.0f;
    p.humanize_vel = p.humanize_ms = 0.0f;
    seq_t s;
    seq_init(&s);
    float len = 0.0f;
    char line[2048];
    for (const char *q = rows; *q;) {
        const char *e = strchr(q, '\n');
        const size_t n = e ? (size_t)(e - q) : strlen(q);
        if (n >= sizeof line) {
            snprintf(g_err, sizeof g_err, "sequence row too long (%d characters at most)", (int)sizeof line - 1);
            seq_free(&s);
            return -1.0f;
        }
        if (n > 0) {
            memcpy(line, q, n);
            line[n] = 0;
            const float l = patch_audition(&p, line, &s, g_err, sizeof g_err);
            if (l < 0.0f) {
                seq_free(&s);
                return -1.0f;
            }
            len = l > len ? l : len;
        }
        q += n + (e ? 1 : 0);
    }
    if (len <= 0.0f) { /* empty grid: silence; the player stops (the next sequence starts from its beginning) */
        seq_free(&s);
        release_player_notes();
        free(g_ev);
        g_ev = NULL;
        return 0.0f;
    }
    return player_start(&s, len, loop, 1);
}

/* Player position in frames since the start of the phrase, or -1 when stopped (for the sequencer playhead). */
EMSCRIPTEN_KEEPALIVE double w_player_pos(void) { return g_ev ? (double)g_pos : -1.0; }

/* A clip of notes, as MIDI: 4 floats per note (pitch 0..127, start and length in beats, velocity 1..127), at bpm,
 * over length_beats (the loop when loop != 0). Each note ends 2 ms early, so that a note starting where another of
 * the same pitch ends (or the loop turns) is never cut by that note-off. A running loop goes on from the same
 * position (edits take over in place). No note: silence, the player stops. Returns the length in seconds, or -1. */
EMSCRIPTEN_KEEPALIVE float w_notes(const float *data, int count, float bpm, float length_beats, int loop)
{
    if (!(bpm >= 20.0f && bpm <= 400.0f) || !(length_beats > 0.0f) || count < 0) {
        snprintf(g_err, sizeof g_err, "clip: bpm 20..400 and a length above 0 beats expected");
        return -1.0f;
    }
    const float spb = 60.0f / bpm, len = length_beats * spb;
    seq_t s;
    seq_init(&s);
    for (int i = 0; i < count; i++) {
        const float *n = data + 4 * i;
        const int pitch = (int)n[0], vel = (int)n[3];
        if (pitch < 0 || pitch > 127 || !(n[1] >= 0.0f) || !(n[2] > 0.0f) || n[1] >= length_beats)
            continue; /* outside the clip or malformed: not played */
        const float dur = n[2] * spb - 0.002f;
        seq_note(&s, n[1] * spb, pitch, vel < 1 ? 1 : vel > 127 ? 127 : vel, dur > 0.001f ? dur : 0.001f);
    }
    if (s.n == 0) {
        seq_free(&s);
        release_player_notes();
        free(g_ev);
        g_ev = NULL;
        return 0.0f;
    }
    return player_start(&s, len, loop, 1);
}

/* ---------------- songs, played live ---------------- */

static void set_global(int id, float physical)
{
    synth_set_param(synth_default(), id, param_to_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), physical));
}
static void set_part(int part, int id, float physical)
{
    synth_part_param(synth_default(), part, id, param_to_norm(synth_part_param_info(id - SYNTH_PART_PARAM), physical));
}
static double jnum(const json_t *o, const char *key, double def)
{
    const json_t *v = o ? json_get(o, key) : NULL;
    return v && v->type == JSON_NUM ? v->num : def;
}

/* Plays a song (songs/ format, its patches inline: the page fetches them) live: track k on part k, listening to
 * channel k + 1, with its patch's engine and parameters, gain, pan and reverb send (the part's effect send, the
 * song's reverb as the bus reverb). A fresh synth_init first, like a patch. Patches' effect settings, a track's
 * pump and the master fades and normalization of the offline render are left out (reported in w_warning); tracks
 * beyond SYNTH_PARTS too. Returns the length in seconds, or -1 (message in w_error). */
EMSCRIPTEN_KEEPALIVE float w_song(const char *text, int loop)
{
    json_t *root = json_parse(text, g_err, sizeof g_err);
    if (!root)
        return -1.0f;
    song_info_t info;
    if (song_info(root, &info, g_err, sizeof g_err) != 0) {
        json_free(root);
        return -1.0f;
    }
    g_warn[0] = 0;
    free(g_ev);
    g_ev = NULL;
    memset(g_on, 0, sizeof g_on);
    synth_t *syn = synth_default();
    synth_init(syn, g_sr);
    seq_t all;
    seq_init(&all);
    static patch_t p; /* large: not on the stack */
    int ti = 0, pumped = 0;
    for (const json_t *t = json_get(root, "tracks")->child; t; t = t->next, ti++) {
        if (ti >= SYNTH_PARTS) {
            const size_t u = strlen(g_warn);
            snprintf(g_warn + u, sizeof g_warn - u, "only the first %d tracks play live\n", SYNTH_PARTS);
            break;
        }
        char *phrase;
        const size_t u = strlen(g_warn);
        if (song_track(t, ti, &info, &p, &phrase, g_err, sizeof g_err, g_warn + u, sizeof g_warn - u) != 0) {
            seq_free(&all);
            json_free(root);
            return -1.0f;
        }
        synth_part_engine(syn, ti, (synth_engine_t)engine_id(p.engine));
        for (int i = 0; i < p.n; i++) {
            const int id = p.ids[i];
            const float norm = param_to_norm(p.desc[i], p.values[i]);
            if (id >= SYNTH_PARAM_TUNING && id < SYNTH_PARAM_GLOBAL_END)
                synth_set_param(syn, id, norm); /* tuning and concert pitch: the whole synth */
            else if (id < SYNTH_PARAM_GLOBAL || id >= SYNTH_PART_PARAM)
                synth_part_param(syn, ti, id, norm); /* engine, part and modulation parameters */
        }
        set_part(ti, SYNTH_PART_CHANNEL, (float)(ti + 1));
        set_part(ti, SYNTH_PART_LEVEL, (float)jnum(t, "gain_db", 0.0));
        set_part(ti, SYNTH_PART_PAN, (float)jnum(t, "pan", 0.0));
        set_part(ti, SYNTH_PART_FX_SEND, dsp_clampf((float)jnum(t, "reverb_send", 0.3), 0.0f, 1.0f));
        pumped |= json_get(t, "pump") != NULL;

        seq_t one;
        seq_init(&one);
        const float len = patch_audition(&p, phrase, &one, g_err, sizeof g_err);
        free(phrase);
        if (len < 0.0f) {
            seq_free(&one);
            seq_free(&all);
            json_free(root);
            return -1.0f;
        }
        for (int i = 0; i < one.n; i++)
            if (one.ev[i].kind == SEQ_MIDI && one.ev[i].msg[0] >= 0x80 && one.ev[i].msg[0] < 0xF0)
                seq_midi(&all, one.ev[i].t, (uint8_t)((one.ev[i].msg[0] & 0xF0) | ti), one.ev[i].msg[1],
                         one.ev[i].msg[2]);
        seq_free(&one);
    }
    if (pumped) {
        const size_t u = strlen(g_warn);
        snprintf(g_warn + u, sizeof g_warn - u, "the pump (sidechain) of the offline render is not played live\n");
    }
    /* the song's reverb: a part sending s to the bus at reverb mix m adds s m reverb(mono): the offline send */
    const json_t *rv = json_get(root, "reverb");
    set_global(SYNTH_PARAM_TEMPO, (float)info.bpm);
    set_global(SYNTH_PARAM_REVERB_MIX, dsp_clampf(fm_db2lin((float)jnum(rv, "return_db", 0.0)), 0.0f, 1.0f));
    set_global(SYNTH_PARAM_REVERB_DECAY, (float)jnum(rv, "decay", 0.7));
    set_global(SYNTH_PARAM_REVERB_DAMPING, (float)jnum(rv, "damping_hz", 5000.0));
    set_global(SYNTH_PARAM_REVERB_PREDELAY, (float)jnum(rv, "predelay_s", 0.03));
    json_free(root);
    return player_start(&all, (float)info.length_s, loop, 0);
}

/* Stops the player and releases every note (CC 123 on all channels). */
EMSCRIPTEN_KEEPALIVE void w_stop(void)
{
    free(g_ev);
    g_ev = NULL;
    memset(g_on, 0, sizeof g_on);
    for (int ch = 0; ch < 16; ch++)
        w_midi(0xB0 | ch, 123, 0, 3);
}
