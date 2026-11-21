/* song.h — multi-track arrangements as JSON: each track = a patch + a note phrase (same syntax as
 * patch auditions), rendered separately through the public API, then mixed with gain, pan and a
 * send to a shared Dattorro plate reverb (dsp/fx/reverb). Rendering is host only; the web page plays the tracks
 * live through song_info and song_track. Format: docs/patch_format.md. */
#ifndef SONG_H
#define SONG_H

#include <stddef.h>

#include "patch.h"

/* Renders the song file; L and R are malloc'ed (caller frees). Returns the frame count, or -1 with a
 * message in err. Messages about clamped values go to warn. */
int song_render(const char *path, float sample_rate, float **L, float **R, char *err, size_t errlen, char *warn,
                size_t warnlen);

struct json;
typedef struct {
    double bpm, beats, tail, length_s; /* length_s = beats at bpm + tail */
} song_info_t;

/* Checks a parsed song's header (version, tempo, length, tracks). Returns 0, or -1 with a message in err. */
int song_info(const struct json *root, song_info_t *info, char *err, size_t errlen);

/* Track ti of a song: its patch with the track's overrides and playing settings (tempo, step, gate, velocity,
 * humanize, swing), and its phrase (start offset as rests, repeats, or its pattern; malloc'ed, caller frees).
 * Mixing (gain_db, pan, reverb_send, pump) is the caller's. Returns 0, or -1 with a message in err. Patch
 * files are read from disk: a page passes inline patches. */
int song_track(const struct json *t, int ti, const song_info_t *info, patch_t *p, char **phrase, char *err,
               size_t errlen, char *warn, size_t warnlen);

#endif /* SONG_H */
