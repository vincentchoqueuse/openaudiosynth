/* params.c — normalized <-> physical conversions according to the descriptor's curve. */
#include "params.h"
#include "../core/fastmath.h"

const char *const param_onoff_labels[] = {"off", "on", NULL};
const char *const param_oversample_labels[] = {"1x", "2x", "4x", NULL};

float param_from_norm(const param_desc_t *d, float norm)
{
    const float n = dsp_clampf(norm, 0.0f, 1.0f);
    switch (d->curve) {
    case PARAM_EXP:
        return d->min * fm_exp2(n * fm_log2(d->max / d->min));
    case PARAM_INT: {
        const float v = d->min + n * (d->max - d->min);
        return (float)(int32_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
    }
    case PARAM_LIN:
    case PARAM_DB:
    default:
        return d->min + n * (d->max - d->min);
    }
}

float param_to_norm(const param_desc_t *d, float value)
{
    const float v = dsp_clampf(value, d->min, d->max);
    if (d->max == d->min)
        return 0.0f;
    if (d->curve == PARAM_EXP)
        return fm_log2(v / d->min) / fm_log2(d->max / d->min);
    return (v - d->min) / (d->max - d->min);
}

/* Small strcmp: dsp/ does not depend on the C library. */
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int param_find(const param_desc_t *table, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (same(table[i].name, name))
            return i;
    return -1;
}

int param_label(const param_desc_t *d, const char *label)
{
    for (int i = 0; d->labels && d->labels[i]; i++)
        if (same(d->labels[i], label))
            return i;
    return -1;
}

int param_find_role(const param_desc_t *table, int count, const char *role)
{
    for (int i = 0; i < count; i++)
        if (table[i].role && same(table[i].role, role))
            return i;
    return -1;
}
