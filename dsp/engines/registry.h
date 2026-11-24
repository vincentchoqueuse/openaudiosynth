/* registry.h — storage able to hold the state of any engine (one per part).
 * Included by synth.c only; everything else uses engine.h. */
#ifndef ENGINE_REGISTRY_H
#define ENGINE_REGISTRY_H

#include "drone.h"
#include "drums.h"
#include "havoc.h"
#include "fm6.h"
#include "micro.h"
#include "modal.h"
#include "mono.h"
#include "poly.h"
#include "sampler.h"
#include "va.h"
#include "wt.h"

typedef union {
    va_t va;
    modal_t modal;
    drums_t drums;
    mono_t mono;
    poly_t poly;
    fm6_t fm6;
    wt_t wt;
    sampler_t sampler;
    drone_t drone;
    micro_t micro;
    havoc_t havoc;
} engine_state_t;

#endif /* ENGINE_REGISTRY_H */
