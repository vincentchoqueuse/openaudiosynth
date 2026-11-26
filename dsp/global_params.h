/* global_params.h — descriptors of the global parameters (SYNTH_PARAM_GLOBAL and above): master
 * level, effect bus, tempo, tuning, output filter, master saturation and limiter, a part's strip (the tables are
 * in global_params.c). The audio path decodes its parameters through them. */
#ifndef GLOBAL_PARAMS_H
#define GLOBAL_PARAMS_H

#include "engines/engine.h"
#include "mod/params.h"
#include "synth.h"
#include "fx/rack.h"

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

/* the global parameter ids of synth.h mirror the effect-bus table */
_Static_assert(SYNTH_PARAM_REVERB_FREEZE - SYNTH_PARAM_CHORUS_MODE == RACK_P_REVERB_FREEZE, "rack ids");
_Static_assert(SYNTH_PARAM_TEMPO - SYNTH_PARAM_CHORUS_MODE == RACK_P_BUS_COUNT, "rack ids");
_Static_assert(SYNTH_PARAM_GLOBAL_END - SYNTH_PARAM_PHASER_MIX == RACK_P_COUNT - RACK_P_BUS_COUNT, "rack ids");
/* a global id of the rack -> its index in rack_params */
static inline int rack_index(int id)
{
    return id >= SYNTH_PARAM_PHASER_MIX ? RACK_P_BUS_COUNT + id - SYNTH_PARAM_PHASER_MIX : id - SYNTH_PARAM_CHORUS_MODE;
}

#endif /* GLOBAL_PARAMS_H */
