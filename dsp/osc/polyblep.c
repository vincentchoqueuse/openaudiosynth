/* polyblep.c — polyBLEP/polyBLAMP oscillator (see polyblep.h).
 * Residuals are expressed in samples: x = t/dt after the discontinuity, (t-1)/dt before.
 * BLEP of a unit step: (x+1)^2/2 before, -(1-x)^2/2 after. BLAMP of a unit-slope ramp
 * (per sample): (x+1)^3/6 before, (1-x)^3/6 after. */
#include "polyblep.h"
#include "../core/fastmath.h"

#define OSC_MIN_DT 1.0e-7f

void osc_init(osc_t *o, float sample_rate)
{
    o->inv_sr = 1.0f / sample_rate;
    o->max_dt = 0.45f;
    o->wave = OSC_SAW;
    o->dt = o->dt_target = 0.0f;
    o->pw = o->pw_target = 0.5f;
    osc_reset(o, 0.0f);
}

void osc_reset(osc_t *o, float phase)
{
    o->phase = fm_frac(phase);
    o->yd = 0.0f;
    if (o->phase >= 1.0f)
        o->phase = 0.0f;
    o->snap = 1;
}

void osc_set_wave(osc_t *o, osc_wave_t w)
{
    o->wave = ((unsigned)w < (unsigned)OSC_WAVE_COUNT) ? (int)w : OSC_SAW;
}

void osc_set(osc_t *o, float freq_hz, float pw)
{
    o->dt_target = dsp_clampf(freq_hz * o->inv_sr, 0.0f, o->max_dt);
    o->pw_target = dsp_clampf(pw, 0.02f, 0.98f);
}

DSP_FAST_CODE void osc_process(osc_t *o, float *restrict out, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dt0 = o->snap ? o->dt_target : o->dt;
    const float pw0 = o->snap ? o->pw_target : o->pw;
    o->snap = 0;
    const float dt1 = o->dt_target, pw1 = o->pw_target;
    const float ddt = (dt1 - dt0) * inv_n;
    /* 1/dt linearly interpolated: no per-sample division */
    const float idt0 = 1.0f / dsp_maxf(dt0, OSC_MIN_DT);
    const float idt1 = 1.0f / dsp_maxf(dt1, OSC_MIN_DT);
    const float didt = (idt1 - idt0) * inv_n;
    const float dpw = (pw1 - pw0) * inv_n;

    float t = o->phase, dt = dt0, idt = idt0, pw = pw0;

    switch (o->wave) {
    case OSC_SINE:
        for (int i = 0; i < n; i++) {
            dt += ddt;
            out[i] = fm_sin(t);
            t += dt;
            if (t >= 1.0f)
                t -= 1.0f;
        }
        break;
    case OSC_SAW:
        for (int i = 0; i < n; i++) {
            dt += ddt;
            idt += didt;
            /* falling step of 2 at phase wrap */
            out[i] = 2.0f * t - 1.0f - 2.0f * osc_blep(t, dt, idt);
            t += dt;
            if (t >= 1.0f)
                t -= 1.0f;
        }
        break;
    case OSC_PULSE:
        for (int i = 0; i < n; i++) {
            dt += ddt;
            idt += didt;
            pw += dpw;
            float t2 = t - pw;
            if (t2 < 0.0f)
                t2 += 1.0f;
            /* +2 at t = 0, -2 at t = pw */
            out[i] = (t < pw ? 1.0f : -1.0f) + 2.0f * osc_blep(t, dt, idt) - 2.0f * osc_blep(t2, dt, idt);
            t += dt;
            if (t >= 1.0f)
                t -= 1.0f;
        }
        break;
    case OSC_TRI:
    default:
        for (int i = 0; i < n; i++) {
            dt += ddt;
            idt += didt;
            float th = t + 0.5f;
            if (th >= 1.0f)
                th -= 1.0f;
            /* slope +/-4 per cycle: break of +8*dt (trough at t = 0) and -8*dt (peak at t = 0.5) */
            out[i] = 1.0f - 4.0f * dsp_absf(t - 0.5f) + 8.0f * dt * (osc_blamp(t, dt, idt) - osc_blamp(th, dt, idt));
            t += dt;
            if (t >= 1.0f)
                t -= 1.0f;
        }
        break;
    }
    o->phase = t;
    o->dt = dt1;
    o->pw = pw1;
}

/* a's value at phase t for its wave, before any step correction */
static inline float pair_value(int wave, float t, float pw)
{
    switch (wave) {
    case OSC_SAW: return 2.0f * t - 1.0f;
    case OSC_PULSE: return t < pw ? 1.0f : -1.0f;
    case OSC_TRI: return 1.0f - 4.0f * dsp_absf(t - 0.5f);
    default: return fm_sin(t);
    }
}

/* Two-point polyBLEP corrections for a step h that happened a fraction d of a sample before the current sample:
 * one part on the previous (delayed) output, the other on the current one. */
static inline void pair_step(float h, float d, float *prev, float *cur)
{
    *prev += h * 0.5f * d * d;
    const float r = 1.0f - d;
    *cur -= h * 0.5f * r * r;
}

DSP_FAST_CODE void osc_process_pair(osc_t *a, osc_t *b, float *restrict out_a, float *restrict out_b, int sync, float fm_oct,
                                    float pwm, int n)
{
    const float inv_n = 1.0f / (float)n;
    const float dta0 = a->snap ? a->dt_target : a->dt, pwa0 = a->snap ? a->pw_target : a->pw;
    const float dtb0 = b->snap ? b->dt_target : b->dt, pwb0 = b->snap ? b->pw_target : b->pw;
    a->snap = b->snap = 0;
    const float ddta = (a->dt_target - dta0) * inv_n, dpwa = (a->pw_target - pwa0) * inv_n;
    const float ddtb = (b->dt_target - dtb0) * inv_n, dpwb = (b->pw_target - pwb0) * inv_n;
    const float idtb0 = 1.0f / dsp_maxf(dtb0, OSC_MIN_DT), idtb1 = 1.0f / dsp_maxf(b->dt_target, OSC_MIN_DT);
    const float didtb = (idtb1 - idtb0) * inv_n;
    const int wa = a->wave, wb = b->wave;
    const int stepped = wa == OSC_SAW || wa == OSC_PULSE;
    float pa = a->phase, pb = b->phase, yd = a->yd;
    float dta = dta0, pwa = pwa0, dtb = dtb0, idtb = idtb0, pwb = pwb0;
    for (int i = 0; i < n; i++) {
        dta += ddta, pwa += dpwa, dtb += ddtb, idtb += didtb, pwb += dpwb;
        /* b, as osc_process would, and whether it wrapped */
        pb += dtb;
        int wrapped = 0;
        if (pb >= 1.0f)
            pb -= 1.0f, wrapped = 1;
        float ob;
        if (wb == OSC_SAW) {
            ob = 2.0f * pb - 1.0f - 2.0f * osc_blep(pb, dtb, idtb);
        } else if (wb == OSC_PULSE) {
            float t2 = pb - pwb;
            if (t2 < 0.0f)
                t2 += 1.0f;
            ob = (pb < pwb ? 1.0f : -1.0f) + 2.0f * osc_blep(pb, dtb, idtb) - 2.0f * osc_blep(t2, dtb, idtb);
        } else if (wb == OSC_TRI) {
            float th = pb + 0.5f;
            if (th >= 1.0f)
                th -= 1.0f;
            ob = 1.0f - 4.0f * dsp_absf(pb - 0.5f) + 8.0f * dtb * (osc_blamp(pb, dtb, idtb) - osc_blamp(th, dtb, idtb));
        } else {
            ob = fm_sin(pb);
        }
        out_b[i] = ob;
        /* a: driven by b, its steps corrected on both sides */
        const float d_a = fm_oct != 0.0f ? dsp_clampf(dta * fm_exp2(fm_oct * ob), 0.0f, 0.45f) : dta;
        const float pw = pwm != 0.0f ? dsp_clampf(pwa + pwm * ob, 0.05f, 0.95f) : pwa;
        const float prev = pa;
        float corr = 0.0f;
        pa += d_a;
        if (sync && wrapped) {
            const float d = dsp_minf(pb * idtb, 1.0f); /* how long ago b wrapped, in samples */
            float aw = prev + d_a * (1.0f - d);       /* a's phase at that instant */
            if (aw >= 1.0f)
                aw -= 1.0f;
            if (stepped)
                pair_step(pair_value(wa, 0.0f, pw) - pair_value(wa, aw, pw), d, &yd, &corr);
            pa = d_a * d;
        } else if (stepped && d_a > 0.0f) {
            if (pa >= 1.0f) { /* its own wrap: saw -2, pulse +2 */
                pa -= 1.0f;
                pair_step(wa == OSC_SAW ? -2.0f : 2.0f, dsp_minf(pa / d_a, 1.0f), &yd, &corr);
            }
            if (wa == OSC_PULSE && prev < pw && pa >= pw) /* the falling edge */
                pair_step(-2.0f, dsp_minf((pa - pw) / d_a, 1.0f), &yd, &corr);
        } else if (pa >= 1.0f) {
            pa -= 1.0f;
        }
        out_a[i] = yd;
        yd = pair_value(wa, pa, pw) + corr;
    }
    a->phase = pa, a->yd = yd, a->dt = a->dt_target, a->pw = a->pw_target;
    b->phase = pb, b->dt = b->dt_target, b->pw = b->pw_target;
}
