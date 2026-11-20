/* knobs.h — a controller's eight knobs on the synth's eight macros, on every preset the board plays: the CCs
 * chosen in board_config.h (BOARD_MACRO_CC), and, for a preset that gives its macros nothing to do, eight
 * targets per engine (its main sound parameters, then the delay and reverb sends). Main loop only. */
#ifndef KNOBS_H
#define KNOBS_H

#include "presets.h"
#include "synth.h"

/* After a preset's own parameters (params[0..count-1], normalized values) were set on part 0. */
void knobs_apply(synth_t *s, int engine, const preset_param_t *params, int count);

#endif /* KNOBS_H */
