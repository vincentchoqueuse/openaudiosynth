/* fm6_sysex.h — reader for the publicly documented system-exclusive voice formats of the 1983
 * 6-operator FM synthesizer: 32-voice bulk dump (packed, 128 bytes per voice) and single voice
 * (unpacked, 155 bytes). Converts each voice to the fm6 engine's parameters (same native scales).
 * Host only. Users import their own files; no factory data is shipped. */
#ifndef FM6_SYSEX_SYSEX_H
#define FM6_SYSEX_SYSEX_H

#include <stddef.h>
#include <stdint.h>

#include "engines/fm6.h"

typedef struct {
    char name[11];
    float p[FM6_P_COUNT]; /* fm6 parameter values (physical = native scales), volume/bend/spread at defaults */
} fm6_sysex_voice_t;

/* Parses every voice found in a buffer that may hold several sysex messages. Writes up to max voices,
 * returns the count, or -1 with a message in err. Checksum problems are reported in warn (not fatal). */
int fm6_sysex_parse(const uint8_t *data, size_t len, fm6_sysex_voice_t *out, int max, char *err, size_t errlen, char *warn,
              size_t warnlen);

/* Unpacks one 128-byte packed voice (bulk dump layout). */
void fm6_sysex_unpack_voice(const uint8_t *v128, fm6_sysex_voice_t *out);

#endif /* FM6_SYSEX_SYSEX_H */
