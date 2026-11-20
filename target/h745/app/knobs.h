/* knobs.h — a controller's eight knobs on the synth's eight macros, on every preset the board plays: the CCs
 * chosen in board_config.h (BOARD_MACRO_CC), and, for a preset that gives its macros nothing to do, eight
 * targets per engine (its main sound parameters, then the delay and reverb sends). Also tells the M4's
 * screen what each knob moves and its value (shared.h). */
#ifndef KNOBS_H
#define KNOBS_H

#include "presets.h"
#include "synth.h"

/* After a preset's own parameters (params[0..count-1], normalized values) were set on part 0. */
void knobs_apply(synth_t *s, int engine, const preset_param_t *params, int count);
/* Every MIDI message, before the synth (audio interrupt). A knob's CC (BOARD_MACRO_CC) is the board's: it drives
 * its macro once it has caught the macro's value (soft takeover: turning a knob never makes the sound jump), and
 * is not passed on. Returns 1 if the message was a knob's, 0 if the synth should get it. */
int knobs_midi(synth_t *s, const uint8_t *msg, int len);
/* Main loop: the value of each knob that moved, in its unit, for the screen (shared.h, knob_text). */
void knobs_poll(void);

#endif /* KNOBS_H */
