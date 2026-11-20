/* catalog.c — name resolution over the engine registry and the global parameters (see catalog.h). */
#include "catalog.h"

#include <stdio.h>
#include <string.h>

const engine_ops_t *catalog_engine(const char *name) { return engine_find(name); }

/* The parameters shared by every engine, as one list: global (master, effects, tempo), part (mixer strip),
 * modulation (macros, LFOs, MSEGs, routes). Returns the descriptor of entry k and its synth id. */
int catalog_shared_count(void) { return SYNTH_GLOBAL_COUNT + SYNTH_PART_COUNT + SYNTH_MOD_COUNT; }

const param_desc_t *catalog_shared(int k, int *id)
{
    if (k < SYNTH_GLOBAL_COUNT) {
        *id = SYNTH_PARAM_GLOBAL + k;
        return synth_global_param(k);
    }
    k -= SYNTH_GLOBAL_COUNT;
    if (k < SYNTH_PART_COUNT) {
        *id = SYNTH_PART_PARAM + k;
        return synth_part_param_info(k);
    }
    k -= SYNTH_PART_COUNT;
    *id = SYNTH_MOD_PARAM + k;
    return synth_mod_param_info(k);
}

/* By role ("filter.cutoff"): engine parameters first, then global ones. Returns 0, -1 (none) or
 * -2 (ambiguous: several engine parameters share the role, e.g. osc1/osc2 detune). */
static int lookup_role(const engine_ops_t *e, const char *role, catalog_param_t *out)
{
    int found = -1, n = 0;
    for (int i = 0; i < e->param_count; i++)
        if (e->params[i].role && !strcmp(e->params[i].role, role))
            found = i, n++;
    if (n > 1)
        return -2;
    if (n == 1) {
        out->id = found;
        out->desc = &e->params[found];
        return 0;
    }
    for (int k = 0; k < catalog_shared_count(); k++) {
        int id;
        const param_desc_t *d = catalog_shared(k, &id);
        if (d && d->role && !strcmp(d->role, role)) {
            out->id = id;
            out->desc = d;
            return 0;
        }
    }
    return -1;
}

int catalog_lookup(const engine_ops_t *e, const char *name, catalog_param_t *out)
{
    if (strchr(name, '.'))
        return lookup_role(e, name, out);
    const int i = param_find(e->params, e->param_count, name);
    if (i >= 0) {
        out->id = i;
        out->desc = &e->params[i];
        return 0;
    }
    for (int k = 0; k < catalog_shared_count(); k++) {
        int id;
        const param_desc_t *d = catalog_shared(k, &id);
        if (d && !strcmp(d->name, name)) {
            out->id = id;
            out->desc = d;
            return 0;
        }
    }
    return -1;
}

void catalog_list_names(const engine_ops_t *e, char *buf, size_t len)
{
    size_t used = 0;
    buf[0] = 0;
    for (int i = 0; i < e->param_count + catalog_shared_count() && used < len; i++) {
        int id;
        const param_desc_t *d = i < e->param_count ? &e->params[i] : catalog_shared(i - e->param_count, &id);
        const int w = snprintf(buf + used, len - used, "%s%s", used ? " " : "", d->name);
        if (w < 0)
            break;
        used += (size_t)w;
    }
}

/* FNV-1a, fed field by field */
static uint32_t fnv(uint32_t h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t fnv_desc(uint32_t h, int id, const param_desc_t *d)
{
    const int32_t i = id;
    h = fnv(h, &i, sizeof i);
    h = fnv(h, d->name, strlen(d->name) + 1);
    h = fnv(h, &d->min, sizeof d->min);
    h = fnv(h, &d->max, sizeof d->max);
    return fnv(h, &d->curve, sizeof d->curve);
}

uint32_t catalog_hash(void)
{
    uint32_t h = 2166136261u;
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        const engine_ops_t *ops = engine_registry[e];
        h = fnv(h, ops->name, strlen(ops->name) + 1);
        for (int k = 0; k < ops->param_count; k++)
            h = fnv_desc(h, k, &ops->params[k]);
    }
    for (int k = 0; k < catalog_shared_count(); k++) {
        int id;
        const param_desc_t *d = catalog_shared(k, &id);
        h = fnv_desc(h, id, d);
    }
    return h;
}
