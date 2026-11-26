/* vfilter.c — filter slot with several characters (see vfilter.h). Switching type resets the new filter's
 * state so it starts clean; the scream and the ladder share their memory, so taking over from the other one starts
 * from its init (a slot that stays on one of them behaves as before). */
#include "vfilter.h"
#include "../core/fastmath.h"

void vfilter_init(vfilter_t *f, float sample_rate)
{
    svf_init(&f->svf, sample_rate);
    formant_init(&f->fmt, sample_rate);
    f->sr = sample_rate;
    f->oversample = 2;
    scream_init(&f->scream, sample_rate);
    scream_set_oversampling(&f->scream, 2);
    f->nl = VF_SCREAM;
    f->type = VF_LP;
    f->mpos = f->mfb = f->mfollow = 0.0f;
}

void vfilter_set_oversampling(vfilter_t *f, int factor)
{
    f->oversample = factor;
    if (f->nl == VF_LADDER)
        ladder_set_oversampling(&f->ladder, factor);
    else if (f->nl == VF_SCREAM)
        scream_set_oversampling(&f->scream, factor);
}

void vfilter_reset(vfilter_t *f)
{
    svf_reset(&f->svf);
    formant_reset(&f->fmt);
    if (f->nl == VF_LADDER)
        ladder_reset(&f->ladder);
    else if (f->nl == VF_MORPH)
        morph_reset(&f->morph);
    else
        scream_reset(&f->scream);
}

/* the shared memory taken over by the nonlinear filter `type`, from its init */
static void take_nl(vfilter_t *f, int type)
{
    if (type == VF_LADDER) {
        ladder_init(&f->ladder, f->sr);
        ladder_set_oversampling(&f->ladder, f->oversample);
    } else if (type == VF_MORPH) {
        morph_init(&f->morph, f->sr);
    } else {
        scream_init(&f->scream, f->sr);
        scream_set_oversampling(&f->scream, f->oversample);
    }
    f->nl = type;
}

void vfilter_set(vfilter_t *f, int type, float cutoff_hz, float q, float reso, float character)
{
    type = dsp_clampi(type, 0, VF_TYPE_COUNT - 1);
    if (type != f->type && (type >= VF_FORMANT || f->type >= VF_FORMANT)) { /* another filter takes over */
        if (type == VF_FORMANT)
            formant_reset(&f->fmt);
        else if (type >= VF_SCREAM) {
            const int nl = type == VF_GROWL ? VF_MORPH : type; /* both morph tables are one filter */
            if (f->nl != nl)
                take_nl(f, nl);
            else if (nl == VF_SCREAM)
                scream_reset(&f->scream);
            else if (nl == VF_LADDER)
                ladder_reset(&f->ladder);
            else
                morph_reset(&f->morph);
        } else
            svf_reset(&f->svf);
    }
    f->type = type;
    if (type == VF_FORMANT) {
        formant_set(&f->fmt, character, fm_log2(dsp_maxf(cutoff_hz, 1.0f) * 0.001f), 2.0f + 18.0f * reso);
    } else if (type == VF_SCREAM) {
        scream_set(&f->scream, cutoff_hz, reso, character);
    } else if (type == VF_LADDER) {
        ladder_set(&f->ladder, cutoff_hz, reso, character);
    } else if (type >= VF_MORPH) {
        morph_set(&f->morph, type == VF_GROWL ? MORPH_GROWL : MORPH_CLASSIC, cutoff_hz, f->mpos, reso, character, f->mfb,
                  f->mfollow);
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
    else if (f->type == VF_LADDER)
        ladder_process(&f->ladder, in, out, n);
    else if (f->type >= VF_MORPH)
        morph_process(&f->morph, in, out, n);
    else
        svf_process(&f->svf, in, out, n);
}
