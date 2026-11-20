/* modmatrix.h — modulation matrix evaluated at control rate (CLAUDE.md, "Modulation"): slots
 * {src, dst, amount, via}; each adds amount x src (x via, if any) to its destination. Sources and
 * destinations are engine-defined indices; index 0 means "none" for both (via 0 = weight 1).
 * The engine scales each destination into its own domain (semitones, octaves, normalized...). */
#ifndef MODMATRIX_H
#define MODMATRIX_H

#include "../core/dsp.h"

typedef struct {
    int src, dst, via;
    float amount; /* -1..1 */
} mod_slot_t;

/* src: values of the n_src sources (src[0] ignored); dst: n_dst outputs, overwritten (dst[0] unused). */
static inline void modmatrix_eval(const mod_slot_t *slots, int n_slots, const float *src, int n_src, float *dst, int n_dst)
{
    for (int d = 0; d < n_dst; d++)
        dst[d] = 0.0f;
    for (int k = 0; k < n_slots; k++) {
        const mod_slot_t *s = &slots[k];
        if (s->src <= 0 || s->src >= n_src || s->dst <= 0 || s->dst >= n_dst || s->amount == 0.0f)
            continue;
        const float w = (s->via > 0 && s->via < n_src) ? src[s->via] : 1.0f;
        dst[s->dst] += s->amount * src[s->src] * w;
    }
}

#endif /* MODMATRIX_H */
