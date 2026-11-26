/* test_patch.c — the patch reader's clip section (host/patch.c): a clip is read with its tempo and notes and plays
 * instead of the audition phrase (its notes at their beats, its length as the audition's); a malformed clip is
 * refused with a message; without a clip the phrase still plays. The settings grouped by module (version 2): a role
 * read at any depth gives its parameter, an unknown one names its module's settings, a flat "params" or a version 1
 * patch is refused; written back by patch_write_settings, the tree reads the same. */
#include <stdio.h>
#include <string.h>

#include "demos.h"
#include "patch.h"
#include "test.h"

static int note_ons(const seq_t *s)
{
    int n = 0;
    for (int i = 0; i < s->n; i++)
        n += s->ev[i].kind == SEQ_MIDI && (s->ev[i].msg[0] & 0xF0) == 0x90 && s->ev[i].msg[2] > 0;
    return n;
}

static float value_of(const patch_t *p, const char *name)
{
    for (int i = 0; i < p->n; i++)
        if (!strcmp(p->desc[i]->name, name))
            return p->values[i];
    return -1e9f;
}

static void put_buf(void *ctx, const char *text) { strncat(ctx, text, 4095 - strlen(ctx)); }

static void module_tree(void)
{
    static patch_t p, q;
    char err[1024], warn[256];
    const char *ok = "{\"version\":2,\"engine\":\"va\",\"filter\":{\"cutoff\":800,\"type\":\"growl\"},"
                     "\"fx\":{\"delay\":{\"mix\":0.25}},\"route1\":{\"source\":\"mlfo1\",\"destination\":\"filter.morph\"}}";
    CHECK(patch_parse(ok, &p, err, sizeof err, warn, sizeof warn) == 0, "v2 patch refused: %s", err);
    CHECK(value_of(&p, "cutoff") == 800.0f && value_of(&p, "delay_mix") == 0.25f, "v2 settings not read by role");
    CHECK(value_of(&p, "filter_mode") == 8.0f, "v2 label not read (filter.type growl)");
    CHECK(patch_parse("{\"version\":2,\"engine\":\"va\",\"filter\":{\"cutof\":800}}", &q, err, sizeof err, warn,
                      sizeof warn) != 0 && strstr(err, "cutoff"),
          "an unknown setting should name its module's: %s", err);
    CHECK(patch_parse("{\"version\":2,\"engine\":\"va\",\"params\":{\"cutoff\":800}}", &q, err, sizeof err, warn,
                      sizeof warn) != 0, "a flat \"params\" accepted");
    CHECK(patch_parse("{\"version\":1,\"engine\":\"va\"}", &q, err, sizeof err, warn, sizeof warn) != 0,
          "a version 1 patch accepted");
    /* written back as a tree, read again: the same values */
    patch_setting_t set[PATCH_MAX_PARAMS];
    for (int i = 0; i < p.n; i++) {
        set[i].role = p.desc[i]->role;
        snprintf(set[i].value, sizeof set[i].value, "%.9g", (double)p.values[i]);
    }
    static char text[4096];
    snprintf(text, sizeof text, "{\"version\":2,\"engine\":\"va\",\n");
    patch_write_settings(put_buf, text, set, p.n, 2);
    strncat(text, "\n}\n", sizeof text - strlen(text) - 1);
    CHECK(patch_parse(text, &q, err, sizeof err, warn, sizeof warn) == 0, "written tree refused: %s\n%s", err, text);
    int same = q.n == p.n;
    for (int i = 0; same && i < p.n; i++)
        same = value_of(&q, p.desc[i]->name) == p.values[i];
    CHECK(same, "written tree reads differently:\n%s", text);
}

void suite_patch(void)
{
    static patch_t p; /* large */
    module_tree();
    char err[512], warn[512];
    const char *with_clip =
        "{\"version\":2,\"engine\":\"micro\",\"audition\":{\"notes\":\"C4 D4 E4\",\"bpm\":90},"
        "\"clip\":{\"bpm\":60,\"length\":8,\"loop\":true,\"notes\":[{\"pitch\":60,\"start\":0,\"length\":2,\"velocity\":90},"
        "{\"pitch\":67,\"start\":4,\"length\":1},{\"pitch\":72,\"start\":9,\"length\":1}]}}";
    CHECK(patch_parse(with_clip, &p, err, sizeof err, warn, sizeof warn) == 0, "patch with a clip refused: %s", err);
    CHECK(p.clip_n == 3 && p.clip_beats == 8.0f && p.bpm == 60.0f && p.clip[1][3] == 100.0f,
          "clip read: %d notes, %g beats, %g bpm, default velocity %g", p.clip_n, (double)p.clip_beats, (double)p.bpm,
          (double)p.clip[1][3]);
    seq_t s;
    seq_init(&s);
    const float len = patch_audition(&p, NULL, &s, err, sizeof err);
    CHECK(len == 8.0f && note_ons(&s) == 2, "clip audition: %g s, %d notes (8 s, 2 expected: one past the end)",
          (double)len, note_ons(&s));
    seq_free(&s);
    seq_init(&s); /* notes given explicitly (a song's track): the phrase, not the clip */
    patch_audition(&p, "C4 E4", &s, err, sizeof err);
    CHECK(note_ons(&s) == 2, "explicit phrase over a clip: %d notes", note_ons(&s));
    seq_free(&s);

    const char *bad[] = {
        "{\"version\":2,\"engine\":\"micro\",\"clip\":{\"notes\":[]}}",
        "{\"version\":2,\"engine\":\"micro\",\"clip\":{\"length\":4,\"notes\":[{\"pitch\":200,\"start\":0,\"length\":1}]}}",
        "{\"version\":2,\"engine\":\"micro\",\"clip\":{\"length\":4,\"notes\":[{\"pitch\":60,\"start\":0,\"length\":0}]}}",
        "{\"version\":2,\"engine\":\"micro\",\"clip\":{\"length\":4,\"bpm\":5,\"notes\":[]}}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(patch_parse(bad[i], &p, err, sizeof err, warn, sizeof warn) != 0 && strstr(err, "clip"),
              "bad clip %u accepted (or a message without \"clip\": %s)", i, err);

    const char *no_clip = "{\"version\":2,\"engine\":\"micro\",\"audition\":{\"notes\":\"C4 D4 E4\",\"bpm\":120,\"step\":0.5}}";
    CHECK(patch_parse(no_clip, &p, err, sizeof err, warn, sizeof warn) == 0 && p.clip_n == 0, "patch without a clip");
    seq_init(&s);
    const float l2 = patch_audition(&p, NULL, &s, err, sizeof err);
    CHECK(note_ons(&s) == 3 && l2 > 0.74f && l2 < 0.76f, "phrase audition: %d notes over %g s", note_ons(&s), (double)l2);
    seq_free(&s);
}
