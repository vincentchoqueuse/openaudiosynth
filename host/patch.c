/* patch.c — JSON patch loading, validation against the descriptor tables, and audition phrases. */
#include "patch.h"

#include "mod/partmod.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

/* Appends formatted text to buf (bounded). */
static void appendf(char *buf, size_t len, const char *fmt, ...)
{
    const size_t used = strlen(buf);
    if (used + 1 >= len)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, len - used, fmt, ap);
    va_end(ap);
}

/* sets (or overrides) one parameter of the patch */
static int patch_put(patch_t *p, int id, const param_desc_t *d, float v, char *err, size_t errlen)
{
    int slot = p->n;
    for (int i = 0; i < p->n; i++)
        if (p->ids[i] == id)
            slot = i; /* override an earlier value */
    if (slot == p->n && p->n >= PATCH_MAX_PARAMS) {
        snprintf(err, errlen, "too many parameters");
        return -1;
    }
    p->ids[slot] = id;
    p->desc[slot] = d;
    p->values[slot] = v;
    if (slot == p->n)
        p->n++;
    return 0;
}

/* "unknown setting": what the module of a key holds (the roles under its first part), or the modules there are */
static void list_module(const engine_ops_t *e, const char *key, char *err, size_t errlen)
{
    const char *dot = strrchr(key, '.');
    const size_t n = dot ? (size_t)(dot - key) : 0;
    snprintf(err, errlen, "unknown %s setting \"%s\"", e->name, key);
    int any = 0;
    for (int i = 0; i < e->param_count + catalog_shared_count(); i++) {
        int id;
        const param_desc_t *d = i < e->param_count ? &e->params[i] : catalog_shared(i - e->param_count, &id);
        if (d && d->role && n && !strncmp(d->role, key, n) && d->role[n] == '.') {
            appendf(err, errlen, "%s %s", any ? "" : "; this module has:", d->role + n + 1);
            any = 1;
        }
    }
    if (!any)
        appendf(err, errlen, " (settings are grouped by module: \"filter\": {\"cutoff\": 800}; see docs/catalog.md)");
}

/* One setting: its key (a parameter's role, "filter.cutoff", or for the patch's own lookups its name) and its value
 * (a number in its units, or one of its labels). */
static int set_one(patch_t *p, const char *key, const json_t *m, char *err, size_t errlen, char *warn, size_t warnlen)
{
    catalog_param_t cp;
    const int found = catalog_lookup(p->engine, key, &cp);
    if (found == -2) {
        snprintf(err, errlen, "role \"%s\" matches several %s parameters; use one of their names:", key,
                 p->engine->name);
        for (int i = 0; i < p->engine->param_count; i++)
            if (p->engine->params[i].role && !strcmp(p->engine->params[i].role, key))
                appendf(err, errlen, " %s", p->engine->params[i].name);
        return -1;
    }
    if (found != 0) {
        list_module(p->engine, key, err, errlen);
        return -1;
    }
    const param_desc_t *d = cp.desc;
    float v;
    if (m->type == JSON_NUM) {
        v = (float)m->num;
    } else if (m->type == JSON_STR && cp.id >= SYNTH_MOD_PARAM &&
               (!strncmp(d->name, "route", 5) || !strncmp(d->name, "map", 3)) && strstr(d->name, "_destination")) {
        /* route or macro target destination: an engine parameter, by name or role, that moves in real time */
        catalog_param_t dst;
        if (!strcmp(m->str, "none")) {
            v = 0.0f;
        } else if (catalog_lookup(p->engine, m->str, &dst) == 0 &&
                   (dst.id < SYNTH_PARAM_GLOBAL || pm_global_target(dst.id))) {
            if (!dst.desc->realtime) {
                snprintf(err, errlen, "\"%s\": %s is not a realtime parameter of %s (cannot be modulated)", key,
                         m->str, p->engine->name);
                return -1;
            }
            v = (float)(dst.id + 1);
        } else {
            snprintf(err, errlen, "\"%s\": \"%s\" is not a %s parameter", key, m->str, p->engine->name);
            return -1;
        }
    } else if (m->type == JSON_STR) {
        const int found = param_label(d, m->str);
        if (found < 0) {
            snprintf(err, errlen, "parameter \"%s\": unknown value \"%s\"", key, m->str);
            if (d->labels) {
                appendf(err, errlen, "; valid:");
                for (int i = 0; d->labels[i]; i++)
                    appendf(err, errlen, " %s", d->labels[i]);
            }
            return -1;
        }
        v = (float)found;
    } else {
        snprintf(err, errlen, "parameter \"%s\": number or label expected", key);
        return -1;
    }
    if (v < d->min || v > d->max) {
        if (warn)
            appendf(warn, warnlen, "%s (range %g..%g %s, clamped)\n", key, (double)d->min, (double)d->max,
                    d->unit);
        v = v < d->min ? d->min : d->max;
    }
    return patch_put(p, cp.id, d, v, err, errlen);
}

/* the keys of a patch that are not settings */
static int reserved(const char *k)
{
    static const char *const R[] = {"version", "engine", "name", "description", "kit", "macros", "audition", "clip",
                                    NULL};
    for (int i = 0; R[i]; i++)
        if (!strcmp(k, R[i]))
            return 1;
    return 0;
}

/* Settings as a tree of modules: {"filter": {"cutoff": 800}, "fx": {"delay": {"mix": 0.2}}}; a leaf's path is its
 * parameter's role ("filter.cutoff", "fx.delay.mix"). At the top of a patch, its own keys are skipped. */
static int set_tree(patch_t *p, const json_t *obj, char *path, size_t len, int top, char *err, size_t errlen,
                    char *warn, size_t warnlen)
{
    const size_t at = strlen(path);
    for (const json_t *m = obj->child; m; m = m->next) {
        if (top && reserved(m->key))
            continue;
        if (snprintf(path + at, len - at, "%s%s", at ? "." : "", m->key) >= (int)(len - at)) {
            snprintf(err, errlen, "setting path too long: %s", path);
            return -1;
        }
        const int rc = m->type == JSON_OBJ ? set_tree(p, m, path, len, 0, err, errlen, warn, warnlen)
                                           : set_one(p, path, m, err, errlen, warn, warnlen);
        path[at] = 0;
        if (rc != 0)
            return -1;
    }
    return 0;
}

int patch_set_params(patch_t *p, const json_t *settings, char *err, size_t errlen, char *warn, size_t warnlen)
{
    if (!settings)
        return 0;
    if (settings->type != JSON_OBJ) {
        snprintf(err, errlen, "settings must be an object of modules ({\"filter\": {\"cutoff\": 800}})");
        return -1;
    }
    char path[128] = "";
    return set_tree(p, settings, path, sizeof path, 0, err, errlen, warn, warnlen);
}

/* ---- the "macros" section: named macros, each moving targets between two values ---- */

static int put_named(patch_t *p, const char *name, float v, char *err, size_t errlen)
{
    catalog_param_t cp;
    if (catalog_lookup(p->engine, name, &cp) != 0) {
        snprintf(err, errlen, "internal: no parameter %s", name);
        return -1;
    }
    return patch_put(p, cp.id, cp.desc, v, err, errlen);
}

/* a target's "from" or "to": a number in the destination's units, or one of its labels */
static int target_value(const json_t *j, const param_desc_t *d, float *v)
{
    if (j && j->type == JSON_NUM) {
        *v = dsp_clampf((float)j->num, d->min, d->max);
        return 0;
    }
    if (j && j->type == JSON_STR && d->labels) {
        const int k = param_label(d, j->str);
        if (k >= 0) {
            *v = d->min + (float)k;
            return 0;
        }
    }
    return -1;
}

/* "macros": [{"name": "Brightness", "value": 0.3, "targets": [{"param": "cutoff", "from": 300, "to": 6000}, ...]}]
 * Macro k of the array is the part's macro k + 1; the targets fill the macro target slots (map1...) in order. */
static int patch_set_macros(patch_t *p, const json_t *macros, char *err, size_t errlen)
{
    for (int k = 0; k < PATCH_MACROS; k++)
        p->macro_name[k][0] = 0;
    if (!macros)
        return 0;
    if (macros->type != JSON_ARR) {
        snprintf(err, errlen, "\"macros\" must be an array of up to %d macros", PATCH_MACROS);
        return -1;
    }
    int k = 0, slot = 0;
    char key[32];
    for (const json_t *mac = macros->child; mac; mac = mac->next, k++) {
        if (k >= PATCH_MACROS || mac->type != JSON_OBJ) {
            snprintf(err, errlen, "\"macros\": up to %d objects {name, value, targets}", PATCH_MACROS);
            return -1;
        }
        const json_t *name = json_get(mac, "name"), *value = json_get(mac, "value"), *targets = json_get(mac, "targets");
        const json_t *cc = json_get(mac, "cc");
        if (cc) {
            if (cc->type != JSON_NUM || cc->num < 0.0 || cc->num > 119.0 || cc->num == 1.0 || cc->num == 64.0) {
                snprintf(err, errlen, "macro %d: \"cc\" is a MIDI CC 2..119 other than 64 (0 = none)", k + 1);
                return -1;
            }
            snprintf(key, sizeof key, "macro%d_cc", k + 1);
            if (put_named(p, key, (float)cc->num, err, errlen) != 0)
                return -1;
        }
        if (name && name->type == JSON_STR)
            snprintf(p->macro_name[k], PATCH_MACRO_NAME, "%s", name->str);
        if (value) {
            if (value->type != JSON_NUM || value->num < 0.0 || value->num > 1.0) {
                snprintf(err, errlen, "macro %d: \"value\" is 0..1", k + 1);
                return -1;
            }
            snprintf(key, sizeof key, "macro%d", k + 1);
            if (put_named(p, key, (float)value->num, err, errlen) != 0)
                return -1;
        }
        if (targets && targets->type != JSON_ARR) {
            snprintf(err, errlen, "macro %d: \"targets\" must be an array", k + 1);
            return -1;
        }
        for (const json_t *t = targets ? targets->child : NULL; t; t = t->next) {
            const json_t *param = json_get(t, "param");
            catalog_param_t dst;
            if (!param || param->type != JSON_STR || catalog_lookup(p->engine, param->str, &dst) != 0 ||
                dst.id >= PM_MAP_DST_MAX) {
                snprintf(err, errlen, "macro %d: target \"%s\" is not a %s parameter, the master or an effect", k + 1,
                         param && param->type == JSON_STR ? param->str : "?", p->engine->name);
                return -1;
            }
            if (!dst.desc->realtime) {
                snprintf(err, errlen, "macro %d: %s cannot move live (not a realtime parameter)", k + 1, param->str);
                return -1;
            }
            float from, to;
            if (target_value(json_get(t, "from"), dst.desc, &from) != 0 ||
                target_value(json_get(t, "to"), dst.desc, &to) != 0) {
                snprintf(err, errlen, "macro %d, %s: \"from\" and \"to\" in %s (%g..%g)", k + 1, param->str,
                         dst.desc->unit && dst.desc->unit[0] ? dst.desc->unit : "its units", (double)dst.desc->min,
                         (double)dst.desc->max);
                return -1;
            }
            if (slot >= PM_MAPS) {
                snprintf(err, errlen, "\"macros\": more than %d targets in all", PM_MAPS);
                return -1;
            }
            slot++;
            const float f = param_to_norm(dst.desc, from), g = param_to_norm(dst.desc, to);
            snprintf(key, sizeof key, "map%d_macro", slot);
            if (put_named(p, key, (float)(k + 1), err, errlen) != 0)
                return -1;
            snprintf(key, sizeof key, "map%d_destination", slot);
            if (put_named(p, key, (float)(dst.id + 1), err, errlen) != 0)
                return -1;
            snprintf(key, sizeof key, "map%d_from", slot);
            if (put_named(p, key, f, err, errlen) != 0)
                return -1;
            snprintf(key, sizeof key, "map%d_to", slot);
            if (put_named(p, key, g, err, errlen) != 0)
                return -1;
        }
    }
    return 0;
}

static int patch_from_json(json_t *root, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen);
static int patch_read(const json_t *root, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen);

int patch_from_object(const json_t *obj, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen)
{
    memset(p, 0, sizeof *p);
    if (warn && warnlen)
        warn[0] = 0;
    return patch_read(obj, p, err, errlen, warn, warnlen);
}

int patch_load(const char *path, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen)
{
    memset(p, 0, sizeof *p);
    if (warn && warnlen)
        warn[0] = 0;
    json_t *root = json_parse_file(path, err, errlen);
    return root ? patch_from_json(root, p, err, errlen, warn, warnlen) : -1;
}

int patch_parse(const char *text, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen)
{
    memset(p, 0, sizeof *p);
    if (warn && warnlen)
        warn[0] = 0;
    json_t *root = json_parse(text, err, errlen);
    return root ? patch_from_json(root, p, err, errlen, warn, warnlen) : -1;
}

/* Validates a parsed patch into p and frees root. */
static int patch_from_json(json_t *root, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen)
{
    const int rc = patch_read(root, p, err, errlen, warn, warnlen);
    json_free(root);
    return rc;
}

/* The optional "clip" section: {bpm, length (beats), loop, notes: [{pitch, start, length, velocity}]}, the format
 * of the web sequencer's clips. Its tempo replaces the audition's. */
static int patch_read_clip(const json_t *c, patch_t *p, char *err, size_t errlen)
{
    p->clip_n = 0;
    p->clip_beats = 0.0f;
    if (!c)
        return 0;
    const json_t *len = json_get(c, "length"), *bpm = json_get(c, "bpm"), *notes = json_get(c, "notes");
    if (c->type != JSON_OBJ || !len || len->type != JSON_NUM || !(len->num > 0.0 && len->num <= 1024.0) || !notes ||
        notes->type != JSON_ARR) {
        snprintf(err, errlen, "\"clip\": {length (beats, 0..1024), notes: [{pitch, start, length, velocity}]}");
        return -1;
    }
    if (bpm) {
        if (bpm->type != JSON_NUM || !(bpm->num > 10.0 && bpm->num < 400.0)) {
            snprintf(err, errlen, "\"clip.bpm\": 10..400");
            return -1;
        }
        p->bpm = (float)bpm->num;
    }
    p->clip_beats = (float)len->num;
    int i = 0;
    for (const json_t *n = notes->child; n; n = n->next, i++) {
        if (i >= PATCH_MAX_CLIP) {
            snprintf(err, errlen, "\"clip\": %d notes at most", PATCH_MAX_CLIP);
            return -1;
        }
        const json_t *pi = json_get(n, "pitch"), *st = json_get(n, "start"), *ln = json_get(n, "length");
        const json_t *ve = json_get(n, "velocity");
        if (!pi || pi->type != JSON_NUM || pi->num < 0.0 || pi->num > 127.0 || pi->num != (double)(int)pi->num ||
            !st || st->type != JSON_NUM || !(st->num >= 0.0 && st->num <= 1024.0) || !ln || ln->type != JSON_NUM ||
            !(ln->num > 0.0 && ln->num <= 1024.0) ||
            (ve && (ve->type != JSON_NUM || ve->num < 1.0 || ve->num > 127.0))) {
            snprintf(err, errlen, "clip.notes[%d]: pitch 0..127, start >= 0 and length > 0 (beats), velocity 1..127", i);
            return -1;
        }
        p->clip[i][0] = (float)pi->num;
        p->clip[i][1] = (float)st->num;
        p->clip[i][2] = (float)ln->num;
        p->clip[i][3] = ve ? (float)ve->num : 100.0f;
    }
    p->clip_n = i;
    return 0;
}

/* Validates a patch object into p (root stays owned by the caller). */
static int patch_read(const json_t *root, patch_t *p, char *err, size_t errlen, char *warn, size_t warnlen)
{
    int rc = -1;
    const json_t *ver = json_get(root, "version"), *eng = json_get(root, "engine");
    const json_t *name = json_get(root, "name");
    if (root->type != JSON_OBJ) {
        snprintf(err, errlen, "top level must be an object");
        goto out;
    }
    if (!ver || ver->type != JSON_NUM || (int)ver->num != PATCH_VERSION) {
        snprintf(err, errlen, "\"version\": %d is required", PATCH_VERSION);
        goto out;
    }
    if (!eng || eng->type != JSON_STR || !(p->engine = catalog_engine(eng->str))) {
        snprintf(err, errlen, "\"engine\" must be one of:");
        for (int i = 0; i < SYNTH_ENGINE_COUNT; i++)
            appendf(err, errlen, " %s", engine_registry[i]->name);
        goto out;
    }
    snprintf(p->name, sizeof p->name, "%s", name && name->type == JSON_STR ? name->str : "untitled");
    const json_t *kit = json_get(root, "kit");
    p->kit[0] = 0;
    if (kit) {
        int ok = kit->type == JSON_STR && kit->str[0] && strlen(kit->str) < sizeof p->kit;
        for (const char *c = ok ? kit->str : ""; *c; c++)
            ok &= (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
        if (!ok) {
            snprintf(err, errlen, "\"kit\" must be the name of a kit (a-z, 0-9, _)");
            goto out;
        }
        snprintf(p->kit, sizeof p->kit, "%s", kit->str);
    }
    if (json_get(root, "params")) {
        snprintf(err, errlen, "\"params\": a version %d patch groups its settings by module at the top level "
                 "({\"filter\": {\"cutoff\": 800}}; docs/patch_format.md)", PATCH_VERSION);
        goto out;
    }
    char path[128] = "";
    if (set_tree(p, root, path, sizeof path, 1, err, errlen, warn, warnlen) != 0)
        goto out;
    if (patch_set_macros(p, json_get(root, "macros"), err, errlen) != 0)
        goto out;

    /* audition defaults: one bar of sixteenths on C3 */
    snprintf(p->notes, sizeof p->notes, "C3 _ _ _ E3 _ _ _ G3 _ _ _ C4 _ _ _");
    p->bpm = 120.0f;
    p->step_beats = 0.25f;
    p->gate = 0.8f;
    p->velocity = 100;
    p->humanize_vel = 0.0f;
    p->humanize_ms = 0.0f;
    p->swing = 0.0f;
    const json_t *aud = json_get(root, "audition");
    if (aud) {
        const json_t *x;
        if ((x = json_get(aud, "notes")) && x->type == JSON_STR) {
            if (strlen(x->str) >= sizeof p->notes) {
                snprintf(err, errlen, "audition notes too long (%d characters at most)", (int)sizeof p->notes - 1);
                goto out;
            }
            snprintf(p->notes, sizeof p->notes, "%s", x->str);
        }
        if ((x = json_get(aud, "bpm")) && x->type == JSON_NUM && x->num > 10.0 && x->num < 400.0)
            p->bpm = (float)x->num;
        if ((x = json_get(aud, "step")) && x->type == JSON_NUM && x->num >= 1.0 / 64.0 && x->num <= 64.0)
            p->step_beats = (float)x->num;
        if ((x = json_get(aud, "gate")) && x->type == JSON_NUM && x->num > 0.0 && x->num <= 1.0)
            p->gate = (float)x->num;
        if ((x = json_get(aud, "humanize_velocity")) && x->type == JSON_NUM && x->num >= 0.0)
            p->humanize_vel = (float)x->num;
        if ((x = json_get(aud, "humanize_ms")) && x->type == JSON_NUM && x->num >= 0.0)
            p->humanize_ms = (float)x->num;
        if ((x = json_get(aud, "swing")) && x->type == JSON_NUM && x->num >= 0.0 && x->num <= 0.5)
            p->swing = (float)x->num;
        if ((x = json_get(aud, "velocity")) && x->type == JSON_NUM && x->num >= 1.0 && x->num <= 127.0)
            p->velocity = (int)x->num;
    }
    if (patch_read_clip(json_get(root, "clip"), p, err, errlen) != 0)
        goto out;
    rc = 0;
out:
    return rc;
}

static int by_role(const void *a, const void *b)
{
    return strcmp(((const patch_setting_t *)a)->role, ((const patch_setting_t *)b)->role);
}

/* the parts of a role, split at its dots: up to 8 parts of 63 characters */
static int split_role(const char *role, char part[8][64])
{
    int m = 0;
    for (const char *c = role; *c && m < 8; m++) {
        const char *e = strchr(c, '.');
        const size_t l = e ? (size_t)(e - c) : strlen(c);
        snprintf(part[m], 64, "%.*s", (int)l, c);
        c += l + (e ? 1 : 0);
    }
    return m;
}

static void put_indent(void (*put)(void *, const char *), void *ctx, int n)
{
    static const char sp[] = "                                ";
    put(ctx, sp + (sizeof sp - 1 - (size_t)dsp_clampi(n, 0, (int)sizeof sp - 1)));
}

void patch_write_settings(void (*put)(void *ctx, const char *text), void *ctx, patch_setting_t *s, int n, int indent)
{
    qsort(s, (size_t)n, sizeof *s, by_role);
    char open[8][64], part[8][64], buf[128];
    int depth = 0, first[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    for (int i = 0; i < n; i++) {
        const int m = split_role(s[i].role, part);
        int c = 0;
        while (c < depth && c < m - 1 && !strcmp(open[c], part[c]))
            c++;
        for (; depth > c; depth--) { /* close the modules this setting is not in */
            put(ctx, "\n");
            put_indent(put, ctx, indent + 2 * (depth - 1));
            put(ctx, "}");
        }
        for (int l = depth; l < m; l++) { /* open its modules, then the setting */
            put(ctx, first[l] ? (l ? "\n" : "") : ",\n");
            first[l] = 0;
            put_indent(put, ctx, indent + 2 * l);
            snprintf(buf, sizeof buf, "\"%s\": %s", part[l], l < m - 1 ? "{" : "");
            put(ctx, buf);
            if (l < m - 1) {
                snprintf(open[l], 64, "%s", part[l]);
                first[l + 1] = 1;
                depth = l + 1;
            }
        }
        put(ctx, s[i].value);
    }
    for (; depth > 0; depth--) {
        put(ctx, "\n");
        put_indent(put, ctx, indent + 2 * (depth - 1));
        put(ctx, "}");
    }
}

const uint8_t *(*patch_kit_resolver)(const char *name, size_t *bytes);

void patch_apply(const patch_t *p, seq_t *s)
{
    seq_engine(s, 0.0f, (synth_engine_t)engine_id(p->engine));
    size_t bytes = 0;
    const uint8_t *kit = p->kit[0] && patch_kit_resolver ? patch_kit_resolver(p->kit, &bytes) : NULL;
    if (kit)
        seq_kit(s, 0.0f, kit, bytes);
    for (int i = 0; i < p->n; i++)
        seq_param_norm(s, 0.0f, p->ids[i], param_to_norm(p->desc[i], p->values[i]));
}

/* Parses "C3", "Eb2", "F#4", "c-1". Returns the MIDI note or -1. */
int patch_parse_note(const char *t, int len)
{
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A B C D E F G */
    if (len < 2)
        return -1;
    const int c = toupper((unsigned char)t[0]);
    if (c < 'A' || c > 'G')
        return -1;
    int n = base[c - 'A'], i = 1;
    if (t[i] == '#') {
        n++;
        i++;
    } else if (t[i] == 'b') {
        n--;
        i++;
    }
    char *end;
    char buf[8] = {0};
    if (len - i <= 0 || len - i >= (int)sizeof buf)
        return -1;
    memcpy(buf, t + i, (size_t)(len - i));
    const long oct = strtol(buf, &end, 10);
    if (*end || end == buf)
        return -1;
    n += 12 * ((int)oct + 1); /* C4 = 60 */
    return n >= 0 && n <= 127 ? n : -1;
}

float patch_audition(const patch_t *p, const char *notes, seq_t *s, char *err, size_t errlen)
{
    if (!notes && p->clip_n > 0) { /* the clip: its notes, each ending 2 ms early (a repeated pitch is never cut) */
        const float spb = 60.0f / p->bpm;
        for (int i = 0; i < p->clip_n; i++) {
            const float *n = p->clip[i];
            if (n[1] >= p->clip_beats)
                continue; /* past the end: not played */
            const float dur = n[2] * spb - 0.002f;
            seq_note(s, n[1] * spb, (int)n[0], (int)n[3], dur > 0.001f ? dur : 0.001f);
        }
        return p->clip_beats * spb;
    }
    const char *src = notes ? notes : p->notes;
    const float step_s = 60.0f / p->bpm * p->step_beats;
    /* collect steps first so ties can extend the previous one */
    enum { MAXN = 12 };
    int step = 0;
    int cur_notes[MAXN], cur_vels[MAXN], cur_slide[MAXN], cur_n = 0;
    float cur_start = 0.0f, cur_len = 0.0f;
    const char *q = src;
    for (;;) {
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '|')
            q++;
        const char *tok = q;
        while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '|')
            q++;
        const int tl = (int)(q - tok);
        const int end = tl == 0;
        if (!end && tl == 1 && tok[0] == '_') { /* tie */
            cur_len += step_s;
            step++;
            continue;
        }
        /* flush the pending step */
        for (int i = 0; i < cur_n; i++) {
            /* humanize: xorshift seeded by step and chord position, so renders are reproducible */
            uint32_t r = 0x9E3779B9u ^ ((uint32_t)step * 2654435761u) ^ ((uint32_t)i * 40503u);
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            const float u1 = (float)(r & 0xFFFF) / 65535.0f, u2 = (float)(r >> 16) / 65535.0f;
            const int vel = cur_vels[i] + (int)((u1 * 2.0f - 1.0f) * p->humanize_vel);
            const float dt = u2 * p->humanize_ms * 0.001f;
            /* '~' (slide): hold into the next step so the next note overlaps -> legato glide */
            const float len = cur_slide[i] ? cur_len + 0.3f * step_s : cur_len - step_s * (1.0f - p->gate);
            seq_note(s, cur_start + dt, cur_notes[i], vel < 1 ? 1 : vel > 127 ? 127 : vel, len);
        }
        cur_n = 0;
        if (end)
            break;
        cur_start = (float)step * step_s + ((step & 1) ? p->swing * step_s : 0.0f);
        cur_len = step_s;
        step++;
        if (tl == 1 && tok[0] == '-')
            continue; /* rest */
        for (int a = 0; a < tl;) { /* chord members separated by '+', each with its own accent */
            int b = a;
            while (b < tl && tok[b] != '+')
                b++;
            int len = b - a, vel = p->velocity, slide = 0;
            for (int k = 0; k < 2 && len > 0; k++) { /* suffixes in any order: accent/ghost and slide */
                const char c = tok[a + len - 1];
                if (c == '!' || c == '?') {
                    vel = c == '!' ? 127 : 50;
                    len--;
                } else if (c == '~') {
                    slide = 1;
                    len--;
                }
            }
            const int note = patch_parse_note(tok + a, len);
            if (note < 0 || cur_n >= MAXN) {
                snprintf(err, errlen, "bad note \"%.*s\" (expected e.g. C3, Eb2!, F#4?, G2~, C3+E3, -, _)", tl, tok);
                return -1.0f;
            }
            cur_notes[cur_n] = note;
            cur_slide[cur_n] = slide;
            cur_vels[cur_n++] = vel;
            a = b + 1;
        }
    }
    return (float)step * step_s;
}
