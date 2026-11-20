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
