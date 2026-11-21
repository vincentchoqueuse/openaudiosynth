/* synth_probe.h — read-only views of an instance's state for displays (the web page, the board's screen): where
 * the modulation envelopes are. Nothing here changes the sound; call it from the interface thread between renders
 * (the value can be one block old). Kept apart from synth.h, the control API. */
#ifndef SYNTH_PROBE_H
#define SYNTH_PROBE_H

#include "synth.h"

/* Position of an envelope along its shape, 0..1, or -1 when it is not running. which: 0 and 1 = the part's
 * envelopes (MSEG 1 and 2), 2 = the per-note envelope of the most recent note still running. */
float synth_probe_mseg(const synth_t *s, int part, int which);

#endif /* SYNTH_PROBE_H */
