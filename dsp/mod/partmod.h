/* partmod.h — parameter and modulation layer of a part, the same for every engine. Parameter changes made while
 * notes sound (knobs, CCs, macros, routes) glide to their target with a 5 ms one-pole, for every engine; with
 * nothing sounding they apply at once (patch loading stays exact). Modulation: 8 macros, 2 LFOs (free or tempo-synced),
 * 2 MSEGs, MIDI sources (mod wheel, aftertouch, pitch bend, velocity, note, random per note) and 8 routes
 * {source, destination, amount, via} onto any `realtime` parameter of the part's engine. Evaluated once per
 * block: each modulated parameter gets base + sum of its routes (normalized domain, clamped) through the
 * engine's set_param; when a route goes away the base value is restored. One value per part (all voices). */
#ifndef PARTMOD_H
#define PARTMOD_H

#include "../engines/engine.h"
#include "../env/mseg.h"
#include "../voice/voicemod.h"
#include "tempo.h"

#define PM_MACROS 8
#define PM_LFOS 2
#define PM_MSEGS 2
#define PM_ROUTES 8
#define PM_MAPS 32 /* macro targets: a macro moves a parameter between two values */
/* a macro target's destination is a synth parameter id + 1: an engine parameter (0..255), or the master level, the
 * effects and the output filter, which synth.c applies (not the tempo, clock and tuning in between) */
#define PM_MAP_DST_MAX (SYNTH_PARAM_GLOBAL_END) /* ids below this one */
static inline int pm_global_target(int id)
{
    return (id >= SYNTH_PARAM_GLOBAL && id < SYNTH_PARAM_TEMPO) || (id >= SYNTH_PARAM_OUT_FILTER && id < SYNTH_PARAM_GLOBAL_END);
}
#define PM_ENGINE_PARAMS 256
#define PM_LIVE_MAX 64 /* parameters gliding or modulated at the same time */
#define PM_SMOOTH_S 0.005f

/* parameter layout (index into partmod_params, id = SYNTH_MOD_PARAM + index) */
enum { PM_LFO_RATE = 0, PM_LFO_SYNC, PM_LFO_WAVE, PM_LFO_RETRIG, PM_LFO_COUNT };
enum { PM_MSEG_MODE = 0, PM_MSEG_SYNC, PM_MSEG_POINTS, PM_MSEG_SUSTAIN, PM_MSEG_LOOP_START, PM_MSEG_LOOP_END,
       PM_MSEG_HEAD };
enum { PM_PT_TIME = 0, PM_PT_LEVEL, PM_PT_CURVE, PM_PT_COUNT };
enum { PM_R_SRC = 0, PM_R_DST, PM_R_AMOUNT, PM_R_VIA, PM_R_COUNT };
#define PM_MSEG_PARAMS (PM_MSEG_HEAD + MSEG_POINTS * PM_PT_COUNT)
#define PM_MACRO(k) (k)
#define PM_LFO(k, f) (PM_MACROS + (k) * PM_LFO_COUNT + (f))
#define PM_MSEG(k, f) (PM_MACROS + PM_LFOS * PM_LFO_COUNT + (k) * PM_MSEG_PARAMS + (f))
#define PM_MSEG_PT(k, i, f) PM_MSEG(k, PM_MSEG_HEAD + (i) * PM_PT_COUNT + (f))
#define PM_ROUTE(r, f) (PM_MACROS + PM_LFOS * PM_LFO_COUNT + PM_MSEGS * PM_MSEG_PARAMS + (r) * PM_R_COUNT + (f))
/* per-voice modulation (configuration of every voice of the part): one MSEG, one LFO, VM_ROUTES routes */
enum { PM_VLFO_RATE = 0, PM_VLFO_SYNC, PM_VLFO_WAVE, PM_VLFO_COUNT };
enum { PM_VR_SRC = 0, PM_VR_DST, PM_VR_AMOUNT, PM_VR_COUNT };
#define PM_VBASE PM_ROUTE(PM_ROUTES, 0)
#define PM_VMSEG(f) (PM_VBASE + (f))
#define PM_VMSEG_PT(i, f) PM_VMSEG(PM_MSEG_HEAD + (i) * PM_PT_COUNT + (f))
#define PM_VLFO(f) (PM_VBASE + PM_MSEG_PARAMS + (f))
#define PM_VROUTE(r, f) (PM_VBASE + PM_MSEG_PARAMS + PM_VLFO_COUNT + (r) * PM_VR_COUNT + (f))
/* macro targets (appended: older ids unchanged): macro, destination, value at macro 0, value at macro 1 */
enum { PM_M_MACRO = 0, PM_M_DST, PM_M_FROM, PM_M_TO, PM_M_COUNT };
#define PM_MAP(k, f) (PM_VROUTE(VM_ROUTES, 0) + (k) * PM_M_COUNT + (f))
/* the MIDI CC that turns each macro (appended after the targets), 0 = none */
#define PM_MACRO_CC(k) (PM_MAP(PM_MAPS, 0) + (k))
/* Fourier LFO shape (appended): coefficient a (cosine) and b (sine) of harmonic k+1 of LFO l (0, 1: the part LFOs,
 * 2: the per-voice one); the shape is sum(a cos 2 pi h ph + b sin 2 pi h ph) divided by sum(|a| + |b|) when that
 * exceeds 1, so it stays within -1..1 */
#define PM_FOURIER_H VM_FOURIER_H
#define PM_FOURIER(l, k, ab) (PM_MACRO_CC(PM_MACROS) + ((l) * PM_FOURIER_H + (k)) * 2 + (ab))
#define PM_COUNT PM_FOURIER(PM_LFOS + 1, 0, 0)
#define PM_WAVE_FOURIER 5
#define PM_WAVE_WIENER 6

/* sources (index 0 = none) */
enum { PM_SRC_NONE = 0, PM_SRC_MACRO1, PM_SRC_LFO1 = PM_SRC_MACRO1 + PM_MACROS, PM_SRC_MSEG1 = PM_SRC_LFO1 + PM_LFOS,
       PM_SRC_WHEEL = PM_SRC_MSEG1 + PM_MSEGS, PM_SRC_AFTERTOUCH, PM_SRC_BEND, PM_SRC_VELOCITY, PM_SRC_NOTE,
       PM_SRC_RANDOM, PM_SRC_COUNT };

extern const param_desc_t partmod_params[PM_COUNT];

typedef struct {
    float p[PM_COUNT];   /* physical values */
    mseg_shape_t shape[PM_MSEGS];
    int shape_dirty;
    mseg_t mseg[PM_MSEGS];
    float lfo_phase[PM_LFOS], lfo_sh[PM_LFOS];
    float wiener[PM_LFOS + 1][VM_WIENER_N]; /* the wiener shape of each LFO (the last: the per-voice one) */
    float src[PM_SRC_COUNT];
    float base[PM_ENGINE_PARAMS]; /* unmodulated normalized value of each engine parameter */
    float cur[PM_ENGINE_PARAMS];  /* value last given to the engine */
    int16_t live[PM_LIVE_MAX];    /* parameters gliding to their target or modulated */
    uint8_t is_live[PM_ENGINE_PARAMS];
    int n_live, n_routes;         /* n_routes = routes with a source, a destination and an amount */
    int n_maps;                   /* macro targets with a macro and a destination */
    float smooth_k;               /* one-pole coefficient per block */
    voicemod_cfg_t vcfg;          /* per-voice modulation of the part's voices (given to vpoly) */
    int vcfg_dirty;
    int held;                     /* notes held on the part */
    float sr;
    uint32_t rng;
} partmod_t;

void partmod_init(partmod_t *m, float sample_rate, uint32_t seed);
/* New engine: base values back to the engine's defaults, nothing modulated. */
void partmod_engine(partmod_t *m, const engine_ops_t *ops);
/* Modulation parameter index (0..PM_COUNT-1), normalized value. */
void partmod_set(partmod_t *m, int index, float norm);
/* The macro (0..7) a CC turns on this part, or -1. */
int partmod_macro_cc(const partmod_t *m, int cc);
/* Adds this part's macro targets on synth-level parameters (master, effects) into val[id - SYNTH_PARAM_GLOBAL]
 * (normalized; set[i] = 1 once a target is seen, the first target's `from` starting the sum). */
void partmod_global_targets(const partmod_t *m, float *val, uint8_t *set);
/* Base value of an engine parameter set by the user or MIDI. Returns 1 if the layer takes care of it (it glides
 * because notes sound, or it is modulated), 0 if the caller must give it to the engine now. */
int partmod_base(partmod_t *m, const engine_ops_t *ops, int id, float norm, int sounding);
void partmod_note_on(partmod_t *m, int note, float velocity);
void partmod_note_off(partmod_t *m);
void partmod_all_notes_off(partmod_t *m);
/* Microtuning (12 offsets in semitones from root, NULL = equal temperament) and MPE, set by the synth. */
void partmod_tuning(partmod_t *m, const float *offsets, int root);
void partmod_mpe(partmod_t *m, int on, float bend_range);
static inline void partmod_midi_source(partmod_t *m, int src, float v) { m->src[src] = v; }
/* Once per block, before the engine renders: sources, routes, engine parameters. */
void partmod_block(partmod_t *m, const tempo_t *t, int n, const engine_ops_t *ops, void *eng);

#endif /* PARTMOD_H */
