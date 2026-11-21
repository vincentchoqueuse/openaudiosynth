/* test_voice.c — voice allocation: retrigger, steal priorities, sustain pedal; the analog imperfections of an
 * oscillator (static spread, drift deviation and time constant, determinism). */
#include <math.h>

#include "engines/engine.h"
#include "global_params.h"
#include "test.h"
#include "voice/analog.h"
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

    /* analog imperfections: the walk's deviation and time constant as set (3 cents, 2 s), deterministic; the
     * static offsets of 64 oscillators spread over the tolerance */
    const float dt = 32.0f / 48000.0f;
    analog_cfg_t c;
    analog_cfg(&c, 0.0f, 3.0f, 2.0f, 0.0f, dt);
    analog_t a1, a2;
    analog_init(&a1, 3u);
    analog_init(&a2, 3u);
    const int nb = 400000, lag = (int)(2.0f / dt); /* ~267 s; lag = one time constant */
    static float xs[400000];
    double sum = 0.0, sq = 0.0;
    int same = 1;
    for (int i = 0; i < nb; i++) {
        xs[i] = analog_next(&a1, &c);
        same &= xs[i] == analog_next(&a2, &c);
        sum += (double)xs[i], sq += (double)xs[i] * (double)xs[i];
    }
    const double mean = sum / nb, sd = sqrt(sq / nb - mean * mean);
    double cov = 0.0;
    for (int i = 0; i + lag < nb; i++)
        cov += ((double)xs[i] - mean) * ((double)xs[i + lag] - mean);
    const double rho = cov / (nb - lag) / (sd * sd);
    CHECK(same, "analog drift not deterministic");
    CHECK(fabs(sd - 3.0) < 0.6, "analog drift deviation %.2f cents, expected 3", sd);
    CHECK(fabs(rho - 0.3679) < 0.12, "analog drift correlation after one time constant %.2f, expected 0.37", rho);
    analog_cfg(&c, 4.0f, 0.0f, 2.0f, 0.0f, dt);
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t k = 0; k < 64; k++) {
        analog_t o;
        analog_init(&o, 100u + k);
        const float x = analog_next(&o, &c);
        lo = fminf(lo, x), hi = fmaxf(hi, x);
    }
    CHECK(lo >= -4.0f && hi <= 4.0f && hi - lo > 6.0f, "analog tolerance 4 cents: offsets %.2f .. %.2f", (double)lo, (double)hi);
}
