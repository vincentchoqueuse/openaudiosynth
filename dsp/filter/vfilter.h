/* vfilter.h — one filter slot of a voice, several characters behind one interface: 12 dB state-variable
 * (low-pass, band-pass, high-pass, notch), vowel formant filter, 2-pole screaming low-pass with saturating
 * feedback and 24 dB ZDF ladder with a driven input (both nonlinear ones oversampled 2x by default, or 1x / 4x;
 * they share one place in memory, as a slot plays one of them at a time). An engine keeps one vfilter_t per filter
 * slot and per voice. */
#ifndef VFILTER_H
#define VFILTER_H

#include "formant.h"
#include "ladder.h"
#include "scream.h"
#include "svf_tpt.h"

typedef enum { VF_LP = 0, VF_BP, VF_HP, VF_NOTCH, VF_FORMANT, VF_SCREAM, VF_LADDER, VF_TYPE_COUNT } vf_type_t;

typedef struct {
    svf_t svf;
    formant_t fmt;
    union { /* the nonlinear filter in use (nl says which one the memory holds) */
        scream_t scream;
        ladder_t ladder;
    };
    int type, nl, oversample;
    float sr;
} vfilter_t;

void vfilter_init(vfilter_t *f, float sample_rate);
void vfilter_reset(vfilter_t *f);
/* type: vf_type_t; cutoff in Hz (formant: 1 kHz = adult voice, the formants move with it); q: SVF quality;
 * reso: 0..1 resonance (formant sharpness, scream feedback, ladder resonance); character: 0..1 (formant: vowel a..u,
 * scream and ladder: drive). */
/* Oversampling of the nonlinear characters (scream, ladder): 1, 2 (default) or 4; the others are linear. */
void vfilter_set_oversampling(vfilter_t *f, int factor);
void vfilter_set(vfilter_t *f, int type, float cutoff_hz, float q, float reso, float character);
void vfilter_process(vfilter_t *f, const float *in, float *out, int n);

#endif /* VFILTER_H */
