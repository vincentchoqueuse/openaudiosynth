/* registry.h — storage able to hold the state of any engine (one per part).
 * Included by synth.c only; everything else uses engine.h. */
#ifndef ENGINE_REGISTRY_H
#define ENGINE_REGISTRY_H

#include "acid.h"
#include "coupled.h"
#include "drums.h"
#include "fm6.h"
#include "modal.h"
#include "mono.h"
#include "poly.h"
#include "sampler.h"
#include "selfosc.h"
#include "sine.h"
#include "va.h"
#include "wt.h"

typedef union {
    sine_engine_t sine;
    va_t va;
    modal_t modal;
    drums_t drums;
    acid_t acid;
    mono_t mono;
    poly_t poly;
    fm6_t fm6;
    wt_t wt;
    coupled_t coupled;
    selfosc_engine_t selfosc;
    sampler_t sampler;
} engine_state_t;

#endif /* ENGINE_REGISTRY_H */
