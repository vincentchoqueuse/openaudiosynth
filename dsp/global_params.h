/* global_params.h — descriptors of the global parameters (SYNTH_PARAM_GLOBAL and above): master
 * level and effect bus. For hosts, catalogs and agents; the audio path does not need it. */
#ifndef GLOBAL_PARAMS_H
#define GLOBAL_PARAMS_H

#include "engines/engine.h"
#include "mod/params.h"
#include "synth.h"

#define SYNTH_GLOBAL_COUNT (SYNTH_PARAM_GLOBAL_END - SYNTH_PARAM_GLOBAL)

/* MIDI CCs handled by the synth itself (on top of each engine's cc_map). */
typedef struct {
    uint8_t cc;
    const char *target;
    const char *help;
} synth_cc_doc_t;
extern const synth_cc_doc_t synth_midi_cc[];
extern const int synth_midi_cc_count;

/* The effect bus as a chain of stages (engine.h), after every engine's output ("engine"), for UIs. */
extern const engine_stage_t synth_fx_stages[];
extern const int synth_fx_stage_count;

/* Descriptor of global parameter SYNTH_PARAM_GLOBAL + index, or NULL. */
const param_desc_t *synth_global_param(int index);

#define SYNTH_PART_COUNT (SYNTH_PART_PARAM_END - SYNTH_PART_PARAM)
/* Descriptor of part parameter SYNTH_PART_PARAM + index, or NULL. */
const param_desc_t *synth_part_param_info(int index);

/* Modulation parameters of a part: SYNTH_MOD_PARAM + index, index < SYNTH_MOD_COUNT. */
extern const int synth_mod_count;
#define SYNTH_MOD_COUNT synth_mod_count
const param_desc_t *synth_mod_param_info(int index);

#endif /* GLOBAL_PARAMS_H */
