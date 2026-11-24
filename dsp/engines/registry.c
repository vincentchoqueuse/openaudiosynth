/* registry.c — the list of engines, indexed by synth_engine_t. Add new engines here. */
#include "engine.h"
#include "drums.h"
#include "havoc.h"
#include "fm6.h"
#include "micro.h"
#include "wt.h"
#include "drone.h"
#include "modal.h"
#include "mono.h"
#include "poly.h"
#include "sampler.h"
#include "va.h"

const engine_ops_t *const engine_registry[SYNTH_ENGINE_COUNT] = {
    [SYNTH_ENGINE_VA] = &va_engine,
    [SYNTH_ENGINE_MODAL] = &modal_engine,
    [SYNTH_ENGINE_DRUMS] = &drums_engine,
    [SYNTH_ENGINE_MONO] = &mono_engine,
    [SYNTH_ENGINE_POLY] = &poly_engine,
    [SYNTH_ENGINE_FM6] = &fm6_engine,
    [SYNTH_ENGINE_WT] = &wt_engine,
    [SYNTH_ENGINE_SAMPLER] = &sampler_engine,
    [SYNTH_ENGINE_DRONE] = &drone_engine,
    [SYNTH_ENGINE_MICRO] = &micro_engine,
    [SYNTH_ENGINE_HAVOC] = &havoc_engine,
};

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const engine_ops_t *engine_find(const char *name)
{
    for (int i = 0; i < SYNTH_ENGINE_COUNT; i++)
        if (same(engine_registry[i]->name, name))
            return engine_registry[i];
    return 0;
}

int engine_id(const engine_ops_t *e)
{
    for (int i = 0; i < SYNTH_ENGINE_COUNT; i++)
        if (engine_registry[i] == e)
            return i;
    return -1;
}
