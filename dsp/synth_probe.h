/* synth_probe.h — read-only views of an instance's state for displays (the web page, the board's screen): where
 * the modulation envelopes are. Nothing here changes the sound; call it from the interface thread between renders
 * (the value can be one block old). Kept apart from synth.h, the control API. */
#ifndef SYNTH_PROBE_H
#define SYNTH_PROBE_H

#include "synth.h"

/* Position of an envelope along its shape, 0..1, or -1 when it is not running. which: 0 and 1 = the part's
 * envelopes (MSEG 1 and 2), 2 = the per-note envelope of the most recent note still running. */
float synth_probe_mseg(const synth_t *s, int part, int which);

/* Frame `frame` of the wavetable that oscillator `osc` (0, 1) of a part plays, as n samples of one cycle (n <= 2048)
 * into out, at its full band; returns the table's frame count (a frame out of range leaves out untouched: ask frame
 * -1 for the count alone), 0 when the part's engine has no wavetable. */
int synth_probe_wavetable(const synth_t *s, int part, int osc, int frame, float *out, int n);

#endif /* SYNTH_PROBE_H */
