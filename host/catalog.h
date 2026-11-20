/* catalog.h — host-side lookups over the DSP's self-description: the engine registry
 * (dsp/engines/engine.h) and the global parameters (dsp/global_params.h). Everything (ranges,
 * labels, help) comes from the descriptor tables; this file only resolves names. */
#ifndef CATALOG_H
#define CATALOG_H

#include "engines/engine.h"
#include "global_params.h"

/* A parameter resolved by name for a given engine. */
typedef struct {
    int id;                  /* value to pass to synth_set_param (engine index, global, part or modulation id) */
    const param_desc_t *desc;
} catalog_param_t;

/* Parameters shared by every engine (global, part, modulation): count, and entry k with its synth id. */
int catalog_shared_count(void);
const param_desc_t *catalog_shared(int k, int *id);
/* Engine by name ("va"), or NULL. */
const engine_ops_t *catalog_engine(const char *name);
/* By name ("cutoff") or by role ("filter.cutoff"): engine parameters first, then global ones
 * (master, effects). Returns 0, -1 if unknown, -2 if the role is ambiguous for this engine. */
int catalog_lookup(const engine_ops_t *e, const char *name, catalog_param_t *out);
/* Writes "name1 name2 ..." of the valid parameter names for an engine (engine + global). */
void catalog_list_names(const engine_ops_t *e, char *buf, size_t len);

/* Hash of everything a normalized parameter value depends on: the engines in registry order, and for every
 * parameter (engine and shared) its id, name, range and curve. Two builds with the same hash interpret
 * (id, 0..1 value) pairs the same way: the board link (midi/link.h) refuses parameters from another hash. */
uint32_t catalog_hash(void);

#endif /* CATALOG_H */
