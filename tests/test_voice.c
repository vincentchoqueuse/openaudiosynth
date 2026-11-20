/* test_voice.c — voice allocation: retrigger, steal priorities, sustain pedal; bounded drift. */
#include "engines/engine.h"
#include "global_params.h"
#include "test.h"
#include "voice/drift.h"
#include "voice/voice_alloc.h"

/* Every descriptor table: entry i has id i, a unique name, a sane range, labels matching the range. */
static void check_tables(void)
{
    for (int e = 0; e < SYNTH_ENGINE_COUNT; e++) {
        const engine_ops_t *o = engine_registry[e];
        CHECK(o && o->name && o->params && o->render && o->poly && o->init, "engine %d incomplete", e);
        for (int i = 0; i < o->param_count; i++) {
            const param_desc_t *d = &o->params[i];
            CHECK(d->id == i, "%s: descriptor %d (%s) has id %d", o->name, i, d->name, d->id);
            CHECK(d->min < d->max && d->def >= d->min && d->def <= d->max, "%s.%s: bad range", o->name, d->name);
            CHECK(param_find(o->params, o->param_count, d->name) == i, "%s.%s: duplicate name", o->name, d->name);
            CHECK(d->role && param_find_role(o->params, o->param_count, d->role) == i, "%s.%s: missing or duplicate role",
                  o->name, d->name);
            if (d->labels) {
                int n = 0;
                while (d->labels[n])
                    n++;
                CHECK(n == (int)d->max + 1, "%s.%s: %d labels for range %g", o->name, d->name, n, (double)d->max);
            }
        }
    }
    for (int g = 0; g < SYNTH_GLOBAL_COUNT; g++) {
        const param_desc_t *d = synth_global_param(g);
        /* effect descriptors carry their index in the effect table; master and tempo carry their synth id */
        const int fx = g >= 1 && SYNTH_PARAM_GLOBAL + g < SYNTH_PARAM_TEMPO;
        CHECK(d && d->id == (fx ? g - 1 : SYNTH_PARAM_GLOBAL + g), "global %d has id %d", g, d ? d->id : -1);
    }
    /* part and modulation tables: ids, unique names, label counts */
    for (int k = 0; k < SYNTH_PART_COUNT + SYNTH_MOD_COUNT; k++) {
        const int part = k < SYNTH_PART_COUNT;
        const param_desc_t *d = part ? synth_part_param_info(k) : synth_mod_param_info(k - SYNTH_PART_COUNT);
        CHECK(d && d->id == (part ? SYNTH_PART_PARAM + k : k - SYNTH_PART_COUNT), "%s %d has id %d", part ? "part" : "mod",
              k, d ? d->id : -1);
        if (!d)
            continue;
        for (int j = 0; j < k; j++) {
            const param_desc_t *o = j < SYNTH_PART_COUNT ? synth_part_param_info(j) : synth_mod_param_info(j - SYNTH_PART_COUNT);
            CHECK(strcmp(o->name, d->name) && strcmp(o->role, d->role), "shared tables: %s duplicates %s", d->name, o->name);
        }
        if (d->labels) {
            int n = 0;
            while (d->labels[n])
                n++;
            CHECK(n == (int)d->max - (int)d->min + 1, "%s: %d labels for range %g..%g", d->name, n, (double)d->min, (double)d->max);
        }
    }
}

void suite_voice(void)
{
    check_tables();

    valloc_t a;
    int steal, list[VALLOC_MAX];
    float levels[4] = {0.5f, 0.1f, 0.9f, 0.3f};

    valloc_init(&a, 4);
    for (int i = 0; i < 4; i++) {
        const int v = valloc_note_on(&a, 60 + i, levels, &steal);
        CHECK(v == i && !steal, "note %d -> voice %d (steal %d)", 60 + i, v, steal);
    }
    /* same note: retriggers the same voice, no steal */
    CHECK(valloc_note_on(&a, 62, levels, &steal) == 2 && !steal, "retrigger of the same note");

    /* all held: the quietest (voice 1) is stolen */
    int v = valloc_note_on(&a, 70, levels, &steal);
    CHECK(v == 1 && steal, "steal the quietest: voice %d", v);

    /* a released voice has priority, even if loud: the oldest released */
    valloc_note_off(&a, 63); /* voice 3 */
    valloc_note_off(&a, 60); /* voice 0 */
    v = valloc_note_on(&a, 71, levels, &steal);
    CHECK(v == 0 && steal, "steal the oldest released: voice %d", v);

    /* pedal: note-off held back, then released when the pedal goes up */
    valloc_init(&a, 4);
    valloc_note_on(&a, 60, NULL, &steal);
    valloc_set_sustain(&a, 1, list);
    CHECK(valloc_note_off(&a, 60) == -1, "note-off ignored under pedal");
    CHECK(a.s[0].state == VSLOT_SUSTAINED, "voice not marked \"sustained\"");
    int n = valloc_set_sustain(&a, 0, list);
    CHECK(n == 1 && list[0] == 0, "pedal up: %d voices released", n);
    valloc_voice_done(&a, 0);
    CHECK(a.s[0].state == VSLOT_FREE, "voice not freed");

    /* drift: bounded, slow (small per-block change), deterministic */
    drift_t d1, d2;
    drift_init(&d1, 1500.0f, 3);
    drift_init(&d2, 1500.0f, 3);
    int ok = 1, same = 1;
    float prev = drift_next(&d1), maxstep = 0.0f;
    drift_next(&d2);
    for (int i = 0; i < 15000; i++) {
        const float x = drift_next(&d1);
        ok &= x >= -1.0f && x <= 1.0f;
        same &= x == drift_next(&d2);
        maxstep = x - prev > maxstep ? x - prev : (prev - x > maxstep ? prev - x : maxstep);
        prev = x;
    }
    CHECK(ok, "drift outside [-1, 1]");
    CHECK(same, "drift not deterministic");
    CHECK(maxstep < 0.01f, "drift too fast: step of %.4f per block", (double)maxstep);
}
