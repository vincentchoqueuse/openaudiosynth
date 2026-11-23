/* test_patch.c — the patch reader's clip section (host/patch.c): a clip is read with its tempo and notes and plays
 * instead of the audition phrase (its notes at their beats, its length as the audition's); a malformed clip is
 * refused with a message; without a clip the phrase still plays. */
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

void suite_patch(void)
{
    static patch_t p; /* large */
    char err[512], warn[512];
    const char *with_clip =
        "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"audition\":{\"notes\":\"C4 D4 E4\",\"bpm\":90},"
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
        "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"clip\":{\"notes\":[]}}",
        "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"clip\":{\"length\":4,\"notes\":[{\"pitch\":200,\"start\":0,\"length\":1}]}}",
        "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"clip\":{\"length\":4,\"notes\":[{\"pitch\":60,\"start\":0,\"length\":0}]}}",
        "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"clip\":{\"length\":4,\"bpm\":5,\"notes\":[]}}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(patch_parse(bad[i], &p, err, sizeof err, warn, sizeof warn) != 0 && strstr(err, "clip"),
              "bad clip %u accepted (or a message without \"clip\": %s)", i, err);

    const char *no_clip = "{\"version\":1,\"engine\":\"micro\",\"params\":{},\"audition\":{\"notes\":\"C4 D4 E4\",\"bpm\":120,\"step\":0.5}}";
    CHECK(patch_parse(no_clip, &p, err, sizeof err, warn, sizeof warn) == 0 && p.clip_n == 0, "patch without a clip");
    seq_init(&s);
    const float l2 = patch_audition(&p, NULL, &s, err, sizeof err);
    CHECK(note_ons(&s) == 3 && l2 > 0.74f && l2 < 0.76f, "phrase audition: %d notes over %g s", note_ons(&s), (double)l2);
    seq_free(&s);
}
