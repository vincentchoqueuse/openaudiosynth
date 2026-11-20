/* voice_alloc.c — voice allocation (see voice_alloc.h).
 * All loops are bounded by VALLOC_MAX. */
#include "voice_alloc.h"

void valloc_init(valloc_t *a, int n_voices)
{
    a->n = dsp_clampi(n_voices, 1, VALLOC_MAX);
    a->clock = 0;
    a->sustain = 0;
    for (int i = 0; i < VALLOC_MAX; i++) {
        a->s[i].note = -1;
        a->s[i].state = VSLOT_FREE;
        a->s[i].age = 0;
    }
}

/* Oldest voice in the given state, or -1. */
static int oldest_in(const valloc_t *a, int state)
{
    int best = -1;
    for (int i = 0; i < a->n; i++)
        if (a->s[i].state == state && (best < 0 || a->s[i].age < a->s[best].age))
            best = i;
    return best;
}

int valloc_note_on(valloc_t *a, int note, const float *levels, int *steal)
{
    int v = -1;
    *steal = 0;
    for (int i = 0; i < a->n && v < 0; i++)
        if (a->s[i].state != VSLOT_FREE && a->s[i].note == note)
            v = i; /* same note: retrigger, no steal */
    if (v < 0)
        v = oldest_in(a, VSLOT_FREE);
    if (v < 0) {
        *steal = 1;
        v = oldest_in(a, VSLOT_RELEASED);
        if (v < 0)
            v = oldest_in(a, VSLOT_SUSTAINED);
        if (v < 0) {
            /* all held: the quietest, oldest on ties */
            v = 0;
            for (int i = 1; i < a->n; i++) {
                const float li = levels ? levels[i] : 0.0f, lv = levels ? levels[v] : 0.0f;
                if (li < lv || (li == lv && a->s[i].age < a->s[v].age))
                    v = i;
            }
        }
    }
    a->s[v].note = (int16_t)note;
    a->s[v].state = VSLOT_HELD;
    a->s[v].age = a->clock++;
    return v;
}

int valloc_note_off(valloc_t *a, int note)
{
    for (int i = 0; i < a->n; i++) {
        if (a->s[i].state == VSLOT_HELD && a->s[i].note == note) {
            if (a->sustain) {
                a->s[i].state = VSLOT_SUSTAINED;
                return -1;
            }
            a->s[i].state = VSLOT_RELEASED;
            return i;
        }
    }
    return -1;
}

int valloc_set_sustain(valloc_t *a, int on, int *out)
{
    int count = 0;
    a->sustain = on ? 1 : 0;
    if (!on) {
        for (int i = 0; i < a->n; i++) {
            if (a->s[i].state == VSLOT_SUSTAINED) {
                a->s[i].state = VSLOT_RELEASED;
                out[count++] = i;
            }
        }
    }
    return count;
}

int valloc_release_all(valloc_t *a, int *out)
{
    int count = 0;
    for (int i = 0; i < a->n; i++) {
        if (a->s[i].state == VSLOT_HELD || a->s[i].state == VSLOT_SUSTAINED) {
            a->s[i].state = VSLOT_RELEASED;
            out[count++] = i;
        }
    }
    return count;
}

void valloc_voice_done(valloc_t *a, int v)
{
    if (v >= 0 && v < a->n) {
        a->s[v].state = VSLOT_FREE;
        a->s[v].note = -1;
    }
}
