/* knobs.c — the board's knobs on the macros (knobs.h). A default target moves its parameter over the parameter's
 * whole range (from 0 to 1, normalized), and the macro starts at the preset's own value: the preset sounds as
 * written until a knob is turned, then the knob sets the parameter. Targets use the modulation layer's macro
 * targets (dsp/mod/partmod.h, maps 1..8) and must be realtime parameters (others are skipped). */
#include "knobs.h"

#include <string.h>

#include "board_config.h"
#include "engines/engine.h"
#include "global_params.h"
#include "mod/partmod.h"

/* The six main parameters of each engine, by name, for macros 1..6 ("" = none). */
static const struct {
    const char *engine;
    const char *param[6];
} knob_targets[] = {
    {"sine", {"attack", "decay", "sustain", "release", "volume", ""}},
    {"va", {"cutoff", "resonance", "fenv_amount", "fenv_decay", "amp_attack", "amp_release"}},
    {"acid", {"cutoff", "resonance", "env_mod", "decay", "accent", "drive"}},
    {"mono", {"cutoff", "emphasis", "contour", "fenv_decay", "mixer_drive", "glide"}},
    {"poly", {"cutoff", "resonance", "fenv_amount", "fenv_decay", "amp_release", "lfo_filter"}},
    {"fm6", {"op2_level", "op4_level", "op6_level", "feedback", "lfo_speed", "lfo_pmd"}},
    {"wt", {"a_position", "b_position", "a_warp", "cutoff", "resonance", "amp_release"}},
    {"modal", {"brightness", "position", "hardness", "damping", "decay", "inharmonicity"}},
    {"drums", {"drive", "kick_tune", "kick_decay", "snare_tone", "snare_decay", "chat_decay"}},
    {"coupled", {"amp_coupling", "phase_coupling", "stretch", "tilt", "drift", "amp_release"}},
    {"selfosc", {"drive", "stiffness", "entrain", "cutoff", "resonance", "amp_release"}},
    {"sampler", {"tune", "resonance", "release", "pad1_decay", "pad2_decay", "pad3_decay"}},
};
/* macros 7 and 8 on every engine: the effect sends */
static const int knob_globals[2] = {SYNTH_PARAM_DELAY_MIX, SYNTH_PARAM_REVERB_MIX};

/* the preset's normalized value of a parameter, or the parameter's default */
static float preset_value(const preset_param_t *params, int count, int id, const param_desc_t *d)
{
    for (int k = count - 1; k >= 0; k--) /* the last one set wins */
        if (params[k].id == id)
            return params[k].value;
    return param_to_norm(d, d->def);
}

/* 1 if the preset gives a macro something to do: a macro target or a route from a macro */
static int preset_uses_macros(const preset_param_t *params, int count)
{
    for (int k = 0; k < count; k++) {
        const int i = (int)params[k].id - SYNTH_MOD_PARAM;
        if (i < 0 || i >= PM_COUNT || params[k].value <= 0.0f)
            continue;
        if (i >= PM_MAP(0, 0) && i < PM_MAP(PM_MAPS, 0) && (i - PM_MAP(0, 0)) % PM_M_COUNT == PM_M_MACRO)
            return 1;
        if (i >= PM_ROUTE(0, 0) && i < PM_ROUTE(PM_ROUTES, 0)) {
            const int f = (i - PM_ROUTE(0, 0)) % PM_R_COUNT;
            const float src = params[k].value * (float)(PM_SRC_COUNT - 1);
            if ((f == PM_R_SRC || f == PM_R_VIA) && src > (float)PM_SRC_MACRO1 - 0.5f &&
                src < (float)(PM_SRC_MACRO1 + PM_MACROS) - 0.5f)
                return 1;
        }
    }
    return 0;
}

static void mod_set(synth_t *s, int index, float physical)
{
    synth_set_param(s, SYNTH_MOD_PARAM + index, param_to_norm(&partmod_params[index], physical));
}

/* macro k (0..7) moves parameter id (engine index, or global id) over its whole range, starting at value */
static void knob_target(synth_t *s, int k, int id, float value)
{
    mod_set(s, PM_MAP(k, PM_M_MACRO), (float)(k + 1));
    mod_set(s, PM_MAP(k, PM_M_DST), (float)(id + 1));
    mod_set(s, PM_MAP(k, PM_M_FROM), 0.0f);
    mod_set(s, PM_MAP(k, PM_M_TO), 1.0f);
    mod_set(s, PM_MACRO(k), value);
}

static void default_targets(synth_t *s, int engine, const preset_param_t *params, int count)
{
    if (engine < 0 || engine >= SYNTH_ENGINE_COUNT || !engine_registry[engine])
        return;
    const engine_ops_t *ops = engine_registry[engine];
    for (size_t t = 0; t < sizeof knob_targets / sizeof knob_targets[0]; t++) {
        if (strcmp(knob_targets[t].engine, ops->name))
            continue;
        for (int k = 0; k < 6; k++) {
            const int i = param_find(ops->params, ops->param_count, knob_targets[t].param[k]);
            if (i >= 0 && ops->params[i].realtime)
                knob_target(s, k, i, preset_value(params, count, i, &ops->params[i]));
        }
    }
    for (int k = 0; k < 2; k++) {
        const int id = knob_globals[k];
        const param_desc_t *d = synth_global_param(id - SYNTH_PARAM_GLOBAL);
        if (d)
            knob_target(s, 6 + k, id, preset_value(params, count, id, d));
    }
}

void knobs_apply(synth_t *s, int engine, const preset_param_t *params, int count)
{
#ifdef BOARD_MACRO_CC
    static const uint8_t cc[] = BOARD_MACRO_CC;
    for (int k = 0; k < PM_MACROS && k < (int)sizeof cc; k++)
        if (cc[k])
            mod_set(s, PM_MACRO_CC(k), (float)cc[k]);
#endif
    if (BOARD_KNOB_TARGETS && !preset_uses_macros(params, count))
        default_targets(s, engine, params, count);
}
