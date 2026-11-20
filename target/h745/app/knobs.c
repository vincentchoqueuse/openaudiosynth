/* knobs.c — the board's knobs on the macros (knobs.h). A default target moves its parameter over the parameter's
 * whole range (from 0 to 1, normalized), and the macro starts at the preset's own value: the preset sounds as
 * written until a knob is turned, then the knob sets the parameter. Targets use the modulation layer's macro
 * targets (dsp/mod/partmod.h, maps 1..8) and must be realtime parameters (others are skipped). */
#include "knobs.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "engines/engine.h"
#include "global_params.h"
#include "mod/partmod.h"
#include "shared.h"

/* what each knob moves, for the screen: the parameter, and its normalized value at macro 0 and at macro 1 */
typedef struct {
    const param_desc_t *d;
    float from, to;
} knob_t;
static knob_t knob[SHARED_KNOBS];
static float knob_shown[SHARED_KNOBS]; /* value of knob_text, -1 to format it again */

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
    {"gull", {"rise", "fall", "syllable", "roughness", "formant", "random"}},
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
static void knob_target(synth_t *s, int k, int id, float value, const param_desc_t *d)
{
    knob[k] = (knob_t){d, 0.0f, 1.0f};
    SHARED->knob_value[k] = value;
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
                knob_target(s, k, i, preset_value(params, count, i, &ops->params[i]), &ops->params[i]);
        }
    }
    for (int k = 0; k < 2; k++) {
        const int id = knob_globals[k];
        const param_desc_t *d = synth_global_param(id - SYNTH_PARAM_GLOBAL);
        if (d)
            knob_target(s, 6 + k, id, preset_value(params, count, id, d), d);
    }
}

/* a modulation parameter of the preset, physical (its own value or the default) */
static float preset_mod(const preset_param_t *params, int count, int index)
{
    const param_desc_t *d = &partmod_params[index];
    return param_from_norm(d, preset_value(params, count, SYNTH_MOD_PARAM + index, d));
}

/* the preset's own macro targets: each macro shows the first parameter it moves */
static void own_targets(int engine, const preset_param_t *params, int count)
{
    const engine_ops_t *ops = engine >= 0 && engine < SYNTH_ENGINE_COUNT ? engine_registry[engine] : NULL;
    for (int j = 0; j < PM_MAPS; j++) {
        const int mac = (int)(preset_mod(params, count, PM_MAP(j, PM_M_MACRO)) + 0.5f);
        const int id = (int)(preset_mod(params, count, PM_MAP(j, PM_M_DST)) + 0.5f) - 1;
        if (mac < 1 || mac > SHARED_KNOBS || knob[mac - 1].d)
            continue;
        const param_desc_t *d = NULL;
        if (ops && id >= 0 && id < ops->param_count)
            d = &ops->params[id];
        else if (id >= SYNTH_PARAM_GLOBAL && id < PM_MAP_DST_MAX)
            d = synth_global_param(id - SYNTH_PARAM_GLOBAL);
        if (d)
            knob[mac - 1] = (knob_t){d, preset_mod(params, count, PM_MAP(j, PM_M_FROM)),
                                     preset_mod(params, count, PM_MAP(j, PM_M_TO))};
    }
    for (int k = 0; k < SHARED_KNOBS; k++)
        SHARED->knob_value[k] = preset_mod(params, count, PM_MACRO(k));
}

void knobs_apply(synth_t *s, int engine, const preset_param_t *params, int count)
{
    memset(knob, 0, sizeof knob);
#ifdef BOARD_MACRO_CC
    static const uint8_t cc[] = BOARD_MACRO_CC;
    for (int k = 0; k < PM_MACROS && k < (int)sizeof cc; k++)
        if (cc[k])
            mod_set(s, PM_MACRO_CC(k), (float)cc[k]);
#else
    static const uint8_t cc[1] = {0};
#endif
    for (int k = 0; k < SHARED_KNOBS; k++) { /* the CC each macro listens to: the board's, or the preset's */
        const int c = k < (int)sizeof cc && cc[k] ? cc[k] : (int)(preset_mod(params, count, PM_MACRO_CC(k)) + 0.5f);
        SHARED->knob_cc[k] = (uint8_t)c;
        SHARED->knob_moves[k] = 0;
        SHARED->knob_value[k] = 0.0f;
    }
    if (BOARD_KNOB_TARGETS && !preset_uses_macros(params, count))
        default_targets(s, engine, params, count);
    else
        own_targets(engine, params, count);
    for (int k = 0; k < SHARED_KNOBS; k++) { /* names: "fenv_amount" -> "fenv amount" */
        const char *n = knob[k].d ? knob[k].d->name : "";
        int i = 0;
        for (; n[i] && i < (int)sizeof SHARED->knob_name[k] - 1; i++)
            SHARED->knob_name[k][i] = n[i] == '_' ? ' ' : n[i];
        SHARED->knob_name[k][i] = 0;
        SHARED->knob_text[k][0] = 0;
        knob_shown[k] = -1.0f;
    }
    __asm volatile("dmb" ::: "memory");
    SHARED->knob_seq++;
}

void knobs_midi(const uint8_t *msg, int len)
{
    if (len < 3 || (msg[0] & 0xF0u) != 0xB0u)
        return;
    for (int k = 0; k < SHARED_KNOBS; k++)
        if (SHARED->knob_cc[k] && SHARED->knob_cc[k] == msg[1]) {
            SHARED->knob_value[k] = (float)msg[2] * (1.0f / 127.0f);
            SHARED->knob_moves[k]++;
        }
}

/* v with dec decimals, without the C library's float formatting */
static int fixed(char *out, int size, float v, int dec)
{
    const int scale = dec == 2 ? 100 : dec == 1 ? 10 : 1;
    const int neg = v < 0.0f;
    const long n = (long)((neg ? -v : v) * (float)scale + 0.5f);
    if (dec == 0)
        return snprintf(out, (size_t)size, "%s%ld", neg && n ? "-" : "", n);
    return snprintf(out, (size_t)size, "%s%ld.%0*ld", neg && n ? "-" : "", n / scale, dec, n % scale);
}

static void format(char *out, int size, const knob_t *kn, float macro)
{
    const param_desc_t *d = kn->d;
    const float v = param_from_norm(d, kn->from + (kn->to - kn->from) * macro);
    const char *u = d->unit ? d->unit : "";
    int n = 0;
    if (d->labels) {
        const int i = (int)(v + 0.5f) - (int)(d->min + 0.5f);
        int c = 0;
        while (d->labels[c] && c < i)
            c++;
        snprintf(out, (size_t)size, "%s", d->labels[c] ? d->labels[c] : "?");
        return;
    }
    if (!strcmp(u, "Hz") && v >= 1000.0f) {
        n = fixed(out, size, v * 0.001f, v < 10000.0f ? 2 : 1);
        u = "kHz";
    } else if (!strcmp(u, "s") && v < 1.0f) {
        n = fixed(out, size, v * 1000.0f, 0);
        u = "ms";
    } else if (!u[0] && d->min == 0.0f && d->max == 1.0f && d->curve != PARAM_INT) {
        n = fixed(out, size, v * 100.0f, 0);
        u = "%";
    } else {
        const float a = v < 0.0f ? -v : v;
        n = fixed(out, size, v, d->curve == PARAM_INT ? 0 : a < 10.0f ? 2 : a < 100.0f ? 1 : 0);
    }
    if (u[0] && n > 0 && n < size - 2) {
        if (u[0] != '%')
            out[n++] = ' ';
        for (int i = 0; u[i] && n < size - 1; i++)
            if (u[i] >= 0x20 && u[i] < 0x7F) /* the screen's fonts are ASCII */
                out[n++] = u[i];
        out[n] = 0;
    }
}

void knobs_poll(void)
{
    for (int k = 0; k < SHARED_KNOBS; k++) {
        const float v = SHARED->knob_value[k];
        if (!knob[k].d || v == knob_shown[k])
            continue;
        knob_shown[k] = v;
        char t[sizeof SHARED->knob_text[k]];
        format(t, (int)sizeof t, &knob[k], v);
        for (size_t i = 0; i < sizeof t; i++) {
            SHARED->knob_text[k][i] = t[i];
            if (!t[i])
                break;
        }
    }
}
