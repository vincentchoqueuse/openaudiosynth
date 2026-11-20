/* params.h — parameter descriptors: the single source of truth for UIs, MIDI learn, presets, the
 * generated catalog and AI agents. Each descriptor carries its range, default, unit, curve, the names
 * of enumerated values and a one-line description of its effect on the sound.
 * The only place where 0..1 normalization exists; the DSP itself works in physical units. */
#ifndef PARAMS_H
#define PARAMS_H

#include "../core/dsp.h"

typedef enum {
    PARAM_LIN = 0, /* min + n*(max-min) */
    PARAM_EXP,     /* min*(max/min)^n, min > 0 (Hz, seconds, Q) */
    PARAM_DB,      /* linear in dB: the physical value is in dB */
    PARAM_INT      /* rounded integer (enumerations use labels) */
} param_curve_t;

typedef struct {
    uint16_t id;
    const char *name;           /* snake_case, unique within its table: used in patches */
    float min, max, def;        /* physical units */
    uint8_t curve;
    const char *unit;
    uint8_t realtime;           /* 1: can move continuously while playing (CC, modulation) without
                                   clicks; 0: a setting (model, mode, voice count...) */
    const char *const *labels;  /* NULL-terminated names of the values of an enumeration, or NULL */
    const char *role;           /* meaning in the shared vocabulary (spec/roles.json), e.g. "filter.cutoff" */
    const char *help;           /* effect on the sound, one line (NULL if DSP_PARAM_TEXT is 0) */
} param_desc_t;

/* Help strings cost flash; a target may drop them with -DDSP_PARAM_TEXT=0. */
#ifndef DSP_PARAM_TEXT
#define DSP_PARAM_TEXT 1
#endif
#if DSP_PARAM_TEXT
#define PARAM_HELP(s) (s)
#else
#define PARAM_HELP(s) NULL
#endif

/* Table entry: PARAM(ID, "name", "role", min, max, default, curve, "unit", realtime, labels, "help") */
#define PARAM(id_, name_, role_, min_, max_, def_, curve_, unit_, rt_, labels_, help_) \
    { (id_), (name_), (min_), (max_), (def_), (curve_), (unit_), (rt_), (labels_), (role_), PARAM_HELP(help_) }

/* Normalized value (clamped to 0..1) -> physical value. */
float param_from_norm(const param_desc_t *d, float norm);
/* Physical value -> normalized value 0..1. */
float param_to_norm(const param_desc_t *d, float value);
/* Index of the parameter called name in a table, or -1. */
int param_find(const param_desc_t *table, int count, const char *name);
/* Index of the parameter with this role in a table, or -1 (first match). */
int param_find_role(const param_desc_t *table, int count, const char *role);
/* Index of a label of an enumerated parameter, or -1. */
int param_label(const param_desc_t *d, const char *label);

/* Labels of every on/off switch (min 0, max 1, PARAM_INT). */
extern const char *const param_onoff_labels[];
/* Labels of the oversampling setting of the nonlinear filters (0..2 -> 1x, 2x, 4x; factor = 1 << value). */
extern const char *const param_oversample_labels[];
#define PARAM_OVERSAMPLE(ID, def, help) \
    PARAM(ID, "oversample", "filter.oversample", 0.0f, 2.0f, def, PARAM_INT, "", 0, param_oversample_labels, help)

#endif /* PARAMS_H */
