/* song.h — multi-track arrangements as JSON: each track = a patch + a note phrase (same syntax as
 * patch auditions), rendered separately through the public API, then mixed with gain, pan and a
 * send to a shared Dattorro plate reverb (dsp/fx/reverb). Host only. Format: docs/patch_format.md. */
#ifndef SONG_H
#define SONG_H

#include <stddef.h>

/* Renders the song file; L and R are malloc'ed (caller frees). Returns the frame count, or -1 with a
 * message in err. Messages about clamped values go to warn. */
int song_render(const char *path, float sample_rate, float **L, float **R, char *err, size_t errlen, char *warn,
                size_t warnlen);

#endif /* SONG_H */
