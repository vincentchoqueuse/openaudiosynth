/* vpoly.c — polyphony manager (see vpoly.h). All loops are bounded by VALLOC_MAX. */
#include "vpoly.h"

#define VPOLY_FADE_S 0.003f

void vpoly_init(vpoly_t *p, int n_voices, float sr, const vpoly_ops_t *ops, void *eng)
{
    valloc_init(&p->alloc, n_voices);
    p->n = p->alloc.n;
    p->fade_step = 1.0f / (VPOLY_FADE_S * sr);
    p->ops = ops;
    p->eng = eng;
    p->onset = 0;
    p->nan_count = 0;
    p->mode = VPOLY_POLY;
    p->priority = VPOLY_PRIO_LAST;
    p->nheld = 0;
    p->sustain = p->release_pending = p->overlap = 0;
    p->last_vel = 0.0f;
    for (int i = 0; i < VALLOC_MAX; i++) {
        p->v[i].active = p->v[i].pending = p->v[i].pending_off = 0;
        p->v[i].note = p->v[i].pending_note = -1;
        p->v[i].pending_vel = 0.0f;
        p->v[i].fade = 1.0f;
        voicemod_init(&p->vm[i], 0x7A11u + (uint32_t)i * 7919u);
        p->vm_done[i] = 0;
    }
    p->vm_cfg = 0;
}

static void release(vpoly_t *p, int v);

static void start(vpoly_t *p, int v, int note, float vel, int hard)
{
    vpoly_voice_t *s = &p->v[v];
    s->note = (int16_t)note;
    s->active = 1;
    s->pending = 0;
    s->fade = 1.0f;
    voicemod_start(&p->vm[v], p->vm_cfg, note, vel, hard);
    p->ops->start(p->eng, v, note, vel, hard);
}

/* ---------------- monophonic play ---------------- */

void vpoly_set_mode(vpoly_t *p, vpoly_mode_t mode)
{
    const int m = (unsigned)mode <= VPOLY_LEGATO ? (int)mode : VPOLY_POLY;
    if (m == p->mode)
        return; /* re-sending the same mode must not forget the held notes */
    /* poly <-> mono: the note-offs to come would not find their notes in the other mode: release them now */
    if ((m == VPOLY_POLY) != (p->mode == VPOLY_POLY))
        vpoly_all_notes_off(p);
    p->mode = m;
}

static void held_remove(vpoly_t *p, int note)
{
    int j = 0;
    for (int i = 0; i < p->nheld; i++)
        if (p->held[i] != note)
            p->held[j++] = p->held[i];
    p->nheld = j;
}

/* Moves the single voice to `note`: legato (no retrigger) if allowed, otherwise a retrigger. */
static void mono_move(vpoly_t *p, int note, float vel, int legato)
{
    vpoly_voice_t *s = &p->v[0];
    if (s->pending) { /* a fade is running (steal / sound-off): just replace what comes next */
        s->pending_note = (int16_t)note;
        s->pending_vel = vel;
        s->pending_off = 0;
        return;
    }
    if (!s->active) {
        start(p, 0, note, vel, 1);
    } else if (legato && p->ops->legato) {
        s->note = (int16_t)note;
        voicemod_note(&p->vm[0], p->vm_cfg, note);
        p->ops->legato(p->eng, 0, note, vel);
    } else {
        start(p, 0, note, vel, 0);
    }
}

void vpoly_set_priority(vpoly_t *p, vpoly_priority_t priority)
{
    p->priority = (unsigned)priority <= VPOLY_PRIO_HIGH ? (int)priority : VPOLY_PRIO_LAST;
}

/* The held key that sounds, by the note priority (-1 if no key is held). */
static int mono_pick(const vpoly_t *p)
{
    if (p->nheld == 0)
        return -1;
    int best = p->held[p->nheld - 1];
    if (p->priority != VPOLY_PRIO_LAST)
        for (int i = 0; i < p->nheld; i++)
            if (p->priority == VPOLY_PRIO_LOW ? p->held[i] < best : p->held[i] > best)
                best = p->held[i];
    return best;
}

static void mono_note_on(vpoly_t *p, int note, float vel)
{
    held_remove(p, note);
    if (p->nheld == VPOLY_HELD_MAX) /* full: forget the oldest key */
        held_remove(p, p->held[0]);
    p->overlap = p->nheld > 0;
    p->held[p->nheld++] = (int16_t)note;
    p->release_pending = 0;
    if (mono_pick(p) != note)
        return; /* another held key keeps the priority (low- or high-note): nothing changes */
    p->last_vel = vel;
    mono_move(p, note, vel, p->overlap && p->mode == VPOLY_LEGATO);
}

static void mono_note_off(vpoly_t *p, int note)
{
    const int was_sounding = mono_pick(p) == note;
    held_remove(p, note);
    if (!was_sounding)
        return; /* a key that was not sounding */
    if (p->nheld > 0) {
        /* back to a key still held, as on classic monosynths: no retrigger */
        p->overlap = 1;
        mono_move(p, mono_pick(p), p->last_vel, 1);
    } else if (p->sustain) {
        p->release_pending = 1;
    } else {
        release(p, 0);
    }
}

/* ---------------- polyphonic play ---------------- */

void vpoly_note_on(vpoly_t *p, int note, float velocity)
{
    if (p->mode != VPOLY_POLY) {
        mono_note_on(p, note, velocity);
        return;
    }
    p->overlap = 0;
    float levels[VALLOC_MAX];
    for (int i = 0; i < p->n; i++)
        levels[i] = p->v[i].active ? p->ops->level(p->eng, i) : 0.0f;
    int steal;
    const int v = valloc_note_on(&p->alloc, note, levels, &steal);
    vpoly_voice_t *s = &p->v[v];
    if ((steal && s->active) || s->pending) {
        /* fade the current note out, the new one starts in vpoly_end_block */
        if (!s->pending)
            s->fade = 1.0f;
        s->pending = 1;
        s->pending_note = (int16_t)note;
        s->pending_vel = velocity;
        s->pending_off = 0;
    } else {
        start(p, v, note, velocity, !s->active);
    }
}

static void release(vpoly_t *p, int v)
{
    if (p->v[v].pending)
        p->v[v].pending_off = 1; /* released before it even started */
    else {
        voicemod_release(&p->vm[v], p->vm_cfg);
        p->ops->release(p->eng, v);
    }
}

void vpoly_note_off(vpoly_t *p, int note)
{
    if (p->mode != VPOLY_POLY) {
        mono_note_off(p, note);
        return;
    }
    const int v = valloc_note_off(&p->alloc, note);
    if (v >= 0)
        release(p, v);
}

void vpoly_sustain(vpoly_t *p, int on)
{
    p->sustain = on ? 1 : 0;
    if (p->mode != VPOLY_POLY) {
        if (!on && p->release_pending && p->nheld == 0) {
            p->release_pending = 0;
            release(p, 0);
        }
        return;
    }
    int list[VALLOC_MAX];
    const int n = valloc_set_sustain(&p->alloc, on, list);
    for (int i = 0; i < n; i++)
        release(p, list[i]);
}

void vpoly_all_notes_off(vpoly_t *p)
{
    p->nheld = 0;
    p->release_pending = 0;
    if (p->mode != VPOLY_POLY) {
        if (p->v[0].active)
            release(p, 0);
        return;
    }
    int list[VALLOC_MAX];
    const int n = valloc_release_all(&p->alloc, list);
    for (int i = 0; i < n; i++)
        release(p, list[i]);
}

void vpoly_all_sound_off(vpoly_t *p)
{
    p->nheld = 0;
    p->release_pending = 0;
    for (int i = 0; i < p->n; i++) {
        vpoly_voice_t *s = &p->v[i];
        if (s->active) {
            if (!s->pending)
                s->fade = 1.0f;
            s->pending = 1;
            s->pending_note = -1;
        }
    }
}

int vpoly_active_count(const vpoly_t *p)
{
    int n = 0;
    for (int i = 0; i < p->n; i++)
        n += p->v[i].active;
    return n;
}

int vpoly_fade(vpoly_t *p, int v, float *gain, int n)
{
    vpoly_voice_t *s = &p->v[v];
    if (!s->pending)
        return 0;
    float f = s->fade;
    for (int i = 0; i < n; i++) {
        f = f > p->fade_step ? f - p->fade_step : 0.0f;
        gain[i] = f;
    }
    s->fade = f;
    return 1;
}

void vpoly_expression(vpoly_t *p, int note, int what, float value)
{
    for (int v = 0; v < p->n; v++)
        if (p->v[v].active && p->v[v].note == note) {
            if (what < 0)
                p->vm[v].bend = value;
            else if (what < VM_SRC_COUNT)
                p->vm[v].src[what] = value;
        }
}

void vpoly_end_block(vpoly_t *p, int v, int sounding)
{
    vpoly_voice_t *s = &p->v[v];
    p->vm_done[v] = 0;
    if (s->pending && s->fade <= 0.0f) {
        p->ops->silence(p->eng, v);
        if (s->pending_note >= 0) {
            const int off = s->pending_off;
            start(p, v, s->pending_note, s->pending_vel, 1);
            if (off) {
                voicemod_release(&p->vm[v], p->vm_cfg);
                p->ops->release(p->eng, v);
            }
        } else {
            s->pending = 0;
            s->active = 0;
            valloc_voice_done(&p->alloc, v);
        }
    } else if (!s->pending && !sounding) {
        s->active = 0;
        valloc_voice_done(&p->alloc, v);
    }
}
