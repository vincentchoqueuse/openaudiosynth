/* wt_factory.h — the built-in wavetables, all generated from formulas (no sampled material): classic shapes,
 * pulse width, hard sync, additive harmonics, vowel formants, sine folding, phase-modulation growl,
 * resonant window (phase-distortion style) and random digital spectra. Built once into a caller-provided
 * memory block (init only). */
#ifndef WT_FACTORY_H
#define WT_FACTORY_H

#include "wavetable.h"

enum { WT_BASIC = 0, WT_PWM, WT_SYNC, WT_HARMONICS, WT_VOWEL, WT_FOLD, WT_GROWL, WT_RESONANT, WT_DIGITAL, WT_FACTORY_COUNT };
#define WT_FACTORY_FLOATS ((size_t)WT_FACTORY_COUNT * WT_TABLE_FLOATS + WT_SCRATCH_FLOATS)

/* Table names (labels of the engine's table parameter), NULL-terminated. */
extern const char *const wt_factory_names[];

/* Builds as many tables as fit in mem (floats); returns that count (0 if not even one fits). */
int wt_factory_build(float *mem, size_t floats);
/* Table i, or table 0 if i was not built. */
static inline const float *wt_factory_table(const float *mem, int built, int i)
{
    return mem + (size_t)(i < built ? i : 0) * WT_TABLE_FLOATS;
}

#endif /* WT_FACTORY_H */
