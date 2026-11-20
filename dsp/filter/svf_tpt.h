/* svf_tpt.h — 2-pole state variable filter in TPT/ZDF topology (trapezoidal, zero delay).
 * Reference: V. Zavalishin, "The Art of VA Filter Design", ch. 4; A. Simper's "a1/a2/a3" form.
 * Coefficients are linearly interpolated over the block: fast modulation without clicks or instability. */
#ifndef SVF_TPT_H
#define SVF_TPT_H

#include "../core/dsp.h"

typedef enum { SVF_LP = 0, SVF_BP, SVF_HP, SVF_NOTCH, SVF_MODE_COUNT } svf_mode_t;

typedef struct {
    float ic1, ic2;            /* integrator states */
    float a1, a2, a3, k;       /* current coefficients */
    float ta1, ta2, ta3, tk;   /* target coefficients at end of block */
    float inv_sr, fmax;
    int mode;
    int snap;
} svf_t;

void svf_init(svf_t *s, float sample_rate);
void svf_reset(svf_t *s);
void svf_set_mode(svf_t *s, svf_mode_t mode);
/* Cutoff in Hz (clamped to 0.45*SR) and quality factor Q (0.707 = Butterworth). */
void svf_set(svf_t *s, float cutoff_hz, float q);
/* in and out may be the same. Normalized band-pass: unity gain at center. */
void svf_process(svf_t *s, const float *in, float *out, int n);

#endif /* SVF_TPT_H */
