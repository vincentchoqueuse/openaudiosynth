/* vfilter.c — filter slot with several characters (see vfilter.h). Switching type resets the new filter's
 * state so it starts clean. */
#include "vfilter.h"
#include "../core/fastmath.h"

void vfilter_init(vfilter_t *f, float sample_rate)
{
    svf_init(&f->svf, sample_rate);
    formant_init(&f->fmt, sample_rate);
    scream_init(&f->scream, sample_rate);
    scream_set_oversampling(&f->scream, 2);
    f->type = VF_LP;
}

void vfilter_set_oversampling(vfilter_t *f, int factor) { scream_set_oversampling(&f->scream, factor); }

void vfilter_reset(vfilter_t *f)
{
    svf_reset(&f->svf);
    formant_reset(&f->fmt);
    scream_reset(&f->scream);
}

void vfilter_set(vfilter_t *f, int type, float cutoff_hz, float q, float reso, float character)
{
    type = dsp_clampi(type, 0, VF_TYPE_COUNT - 1);
    if (type != f->type && (type >= VF_FORMANT || f->type >= VF_FORMANT)) { /* another filter takes over */
        if (type == VF_FORMANT)
            formant_reset(&f->fmt);
        else if (type == VF_SCREAM)
            scream_reset(&f->scream);
        else
            svf_reset(&f->svf);
    }
    f->type = type;
    if (type == VF_FORMANT) {
        formant_set(&f->fmt, character, fm_log2(dsp_maxf(cutoff_hz, 1.0f) * 0.001f), 2.0f + 18.0f * reso);
    } else if (type == VF_SCREAM) {
        scream_set(&f->scream, cutoff_hz, reso, character);
    } else {
        svf_set_mode(&f->svf, (svf_mode_t)type);
        svf_set(&f->svf, cutoff_hz, q);
    }
}

void vfilter_process(vfilter_t *f, const float *in, float *out, int n)
{
    if (f->type == VF_FORMANT)
        formant_process(&f->fmt, in, out, n);
    else if (f->type == VF_SCREAM)
        scream_process(&f->scream, in, out, n);
    else
        svf_process(&f->svf, in, out, n);
}
