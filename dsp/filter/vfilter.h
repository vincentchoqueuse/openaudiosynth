/* vfilter.h — one filter slot of a voice, several characters behind one interface: 12 dB state-variable
 * (low-pass, band-pass, high-pass, notch), vowel formant filter and 2-pole screaming low-pass with
 * saturating feedback (oversampled 2x by default, or 1x / 4x). An engine keeps one vfilter_t per filter slot and per voice. */
#ifndef VFILTER_H
#define VFILTER_H

#include "formant.h"
#include "scream.h"
#include "svf_tpt.h"

typedef enum { VF_LP = 0, VF_BP, VF_HP, VF_NOTCH, VF_FORMANT, VF_SCREAM, VF_TYPE_COUNT } vf_type_t;

typedef struct {
    svf_t svf;
    formant_t fmt;
    scream_t scream;
    int type;
} vfilter_t;

void vfilter_init(vfilter_t *f, float sample_rate);
void vfilter_reset(vfilter_t *f);
/* type: vf_type_t; cutoff in Hz (formant: 1 kHz = adult voice, the formants move with it); q: SVF quality;
 * reso: 0..1 resonance (formant sharpness, scream feedback); character: 0..1 (formant: vowel a..u, scream: drive). */
/* Oversampling of the nonlinear characters (scream): 1, 2 (default) or 4; the others are linear. */
void vfilter_set_oversampling(vfilter_t *f, int factor);
void vfilter_set(vfilter_t *f, int type, float cutoff_hz, float q, float reso, float character);
void vfilter_process(vfilter_t *f, const float *in, float *out, int n);

#endif /* VFILTER_H */
