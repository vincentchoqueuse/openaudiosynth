/* synth_debug.h — diagnostics of an instance, for tests and for a board's status screen; not part of the
 * public API (dsp/synth.h), and free to change. */
#ifndef SYNTH_DEBUG_H
#define SYNTH_DEBUG_H

#include <stdint.h>

#include "synth.h"

/* The state of the engine of a part (engines/registry.h: engine_state_t), or NULL if the part is off. Tests use
 * it to inject a fault. */
void *synth_debug_engine_state(synth_t *s, int part);
/* How many times a part's or the effects' output was not finite (NaN or Inf) and was reset since synth_init. */
uint32_t synth_debug_nan_count(const synth_t *s);

#endif /* SYNTH_DEBUG_H */
