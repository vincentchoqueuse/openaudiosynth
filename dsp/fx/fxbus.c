/* fxbus.c — chorus, tape delay and plate reverb as stages the caller runs in its own order (see fxbus.h). The
 * delay leaves its echoes pending: the reverb right after it takes them into its send (the echoes are reverberated
 * too), anything else has them added first (fxbus_flush). Mix levels are smoothed at control rate and ramped per
 * sample. */
#include "fxbus.h"

#define IDLE_AFTER_S 20.0f /* keep computing a muted effect this long so its tail empties */

static const char *const chorus_labels[] = {"off", "I", "II", "I+II", "ensemble", NULL};

const param_desc_t fxbus_params[FXB_P_COUNT] = {
    PARAM(FXB_P_CHORUS_MODE, "chorus_mode", "fx.chorus.mode", 0.0f, 4.0f, 0.0f, PARAM_INT, "", 0, chorus_labels,
          "chorus: I = slow and deep, II = faster, I+II = both, ensemble = 3-phase string-machine shimmer"),
    PARAM(FXB_P_CHORUS_MIX, "chorus_mix", "fx.chorus.mix", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "chorus wet amount added to the dry sound (width, analog lushness)"),
    PARAM(FXB_P_DELAY_MIX, "delay_mix", "fx.delay.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "tape echo level (0 = off): 0.2-0.4 ambient, more for dub"),
    PARAM(FXB_P_DELAY_TIME, "delay_time", "fx.delay.time", 0.02f, 2.0f, 0.375f, PARAM_EXP, "s", 1, NULL,
          "echo time; changing it bends the repeats like a tape machine. Tempo: 60/bpm x beats"),
    PARAM(FXB_P_DELAY_RATIO, "delay_ratio", "fx.delay.stereo_ratio", 0.25f, 2.0f, 1.0f, PARAM_LIN, "", 0, NULL,
          "right echo time = left x ratio (1.5 = dotted feel, 0.75 ...); ignored in ping-pong"),
    PARAM(FXB_P_DELAY_FEEDBACK, "delay_feedback", "fx.delay.feedback", 0.0f, 0.98f, 0.4f, PARAM_LIN, "", 1, NULL,
          "amount of repeats: 0.3 few, 0.6 long trail, 0.9+ near self-oscillation"),
    PARAM(FXB_P_DELAY_TONE, "delay_tone", "fx.delay.tone", 300.0f, 16000.0f, 3500.0f, PARAM_EXP, "Hz", 1, NULL,
          "low-pass inside the loop: each repeat gets darker (tape); 2-4 kHz = warm, 10k+ = digital"),
    PARAM(FXB_P_DELAY_DRIVE, "delay_drive", "fx.delay.drive", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "tape saturation inside the loop: repeats get grainier and compressed"),
    PARAM(FXB_P_DELAY_WOW, "delay_wow", "fx.delay.wow", 0.0f, 1.0f, 0.25f, PARAM_LIN, "", 1, NULL,
          "tape wow and flutter: pitch wobble of the repeats (0.2 subtle, 0.6+ seasick)"),
    PARAM(FXB_P_DELAY_PINGPONG, "delay_pingpong", "fx.delay.pingpong", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "repeats alternate left / right"),
    PARAM(FXB_P_REVERB_MIX, "reverb_mix", "fx.reverb.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "plate reverb level (0 = off): 0.2 room-ish, 0.5 ambient, 0.8+ washed out"),
    PARAM(FXB_P_REVERB_DECAY, "reverb_decay", "fx.reverb.decay", 0.0f, 0.99f, 0.85f, PARAM_LIN, "", 1, NULL,
          "tail length: 0.5 ~1.8 s, 0.85 ~5 s, 0.93 ~10 s, 0.97+ endless"),
    PARAM(FXB_P_REVERB_DAMPING, "reverb_damping", "fx.reverb.damping", 500.0f, 16000.0f, 4500.0f, PARAM_EXP, "Hz", 1, NULL,
          "high-frequency loss in the tail: lower = darker, softer"),
    PARAM(FXB_P_REVERB_PREDELAY, "reverb_predelay", "fx.reverb.predelay", 0.0f, 0.25f, 0.03f, PARAM_LIN, "s", 0, NULL,
          "gap before the reverb: keeps the dry sound clear"),
    PARAM(FXB_P_REVERB_FREEZE, "reverb_freeze", "fx.reverb.freeze", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 1, param_onoff_labels,
          "freeze: the current tail holds forever and new sound stays dry on top (CC 69)"),
};

void fxbus_init(fxbus_t *f, float sr, float *mem, size_t mem_floats, float max_delay_s, float reverb_size)
{
    f->sr = sr;
    for (int i = 0; i < FXB_P_COUNT; i++)
        f->p[i] = fxbus_params[i].def;
    chorus_init(&f->chorus, sr);

    /* reverb first (it defines the sound most), shrinking its size if the arena is small */
    f->has_reverb = f->has_delay = 0;
    float size = dsp_clampf(reverb_size, 0.5f, REVERB_MAX_SIZE);
    while (size >= 0.5f && mem && reverb_mem_floats(sr, size) > mem_floats)
        size -= 0.1f;
    if (mem && size >= 0.5f) {
        reverb_init(&f->reverb, sr, size, mem);
        f->has_reverb = 1;
        const size_t used = reverb_mem_floats(sr, size);
        mem += used;
        mem_floats -= used;
    }
    /* the delay takes what is left, up to max_delay_s */
    float tmax = max_delay_s;
    while (tmax > 0.05f && mem && delay_mem_floats(sr, tmax) > mem_floats)
        tmax *= 0.9f;
    if (mem && tmax > 0.05f) {
        delay_init(&f->delay, sr, tmax, mem);
        f->has_delay = 1;
        f->delay_max_s = tmax;
    }
    const float ctrl = sr / (float)SYNTH_BLOCK;
    smooth_init(&f->sm_dmix, 0.01f, ctrl, 0.0f);
    smooth_init(&f->sm_rmix, 0.01f, ctrl, 0.0f);
    f->dmix_prev = f->rmix_prev = 0.0f;
    f->delay_idle_s = f->reverb_idle_s = IDLE_AFTER_S; /* idle until used */
    f->dirty = 1;
    f->pending = 0;
}

void fxbus_set_param(fxbus_t *f, int id, float norm)
{
    if (id < 0 || id >= FXB_P_COUNT)
        return;
    f->p[id] = param_from_norm(&fxbus_params[id], norm);
    f->dirty = 1;
}

static void apply(fxbus_t *f)
{
    const float *p = f->p;
    chorus_set(&f->chorus, (chorus_mode_t)(int)p[FXB_P_CHORUS_MODE], p[FXB_P_CHORUS_MIX]);
    if (f->has_delay)
        delay_set(&f->delay, dsp_minf(p[FXB_P_DELAY_TIME], f->delay_max_s), p[FXB_P_DELAY_RATIO],
                  p[FXB_P_DELAY_FEEDBACK], p[FXB_P_DELAY_TONE], p[FXB_P_DELAY_DRIVE], p[FXB_P_DELAY_WOW],
                  (int)p[FXB_P_DELAY_PINGPONG]);
    if (f->has_reverb) {
        reverb_set(&f->reverb, p[FXB_P_REVERB_DECAY], p[FXB_P_REVERB_DAMPING], p[FXB_P_REVERB_PREDELAY]);
        reverb_set_freeze(&f->reverb, (int)p[FXB_P_REVERB_FREEZE]);
    }
    f->dirty = 0;
}

void fxbus_begin(fxbus_t *f)
{
    if (f->dirty)
        apply(f);
    f->pending = 0;
}

DSP_FAST_CODE void fxbus_flush(fxbus_t *f, float *L, float *R, int n)
{
    if (!f->pending)
        return;
    for (int i = 0; i < n; i++) {
        L[i] += f->pl[i];
        R[i] += f->pr[i];
    }
    f->pending = 0;
}

DSP_FAST_CODE void fxbus_chorus(fxbus_t *f, float *L, float *R, int n)
{
    fxbus_flush(f, L, R, n);
    chorus_process(&f->chorus, L, R, L, R, n);
}

/* delay: parallel send, its wet left pending (added by the reverb after it or by fxbus_flush) */
DSP_FAST_CODE void fxbus_delay(fxbus_t *f, float *L, float *R, int n)
{
    float wl[SYNTH_BLOCK], wr[SYNTH_BLOCK];
    fxbus_flush(f, L, R, n);
    const float dt = (float)n / f->sr;
    const float d0 = f->dmix_prev, d1 = smooth_next(&f->sm_dmix, f->has_delay ? f->p[FXB_P_DELAY_MIX] : 0.0f);
    f->dmix_prev = d1;
    const int was_idle = f->delay_idle_s >= IDLE_AFTER_S;
    f->delay_idle_s = (d0 <= 1e-5f && d1 <= 1e-5f) ? f->delay_idle_s + dt : 0.0f;
    const int delay_on = f->has_delay && f->delay_idle_s < IDLE_AFTER_S;
    if (delay_on && was_idle)
        delay_snap(&f->delay); /* tape glide only while running, not when starting */
    if (delay_on) {
        const int muted = d1 <= 1e-5f;
        for (int i = 0; i < n; i++) {
            wl[i] = muted ? 0.0f : L[i];
            wr[i] = muted ? 0.0f : R[i];
        }
        delay_process(&f->delay, wl, wr, f->pl, f->pr, n);
        dsp_ramp_mul(f->pl, d0, d1, n);
        dsp_ramp_mul(f->pr, d0, d1, n);
        f->pending = 1;
    }
}

/* reverb: send = the signal and the echoes still pending, wet added */
DSP_FAST_CODE void fxbus_reverb(fxbus_t *f, float *L, float *R, int n)
{
    float wl[SYNTH_BLOCK], wr[SYNTH_BLOCK], send[SYNTH_BLOCK] = {0.0f}; /* zeroed: n may be short */
    const float dt = (float)n / f->sr;
    const float r0 = f->rmix_prev, r1 = smooth_next(&f->sm_rmix, f->has_reverb ? f->p[FXB_P_REVERB_MIX] : 0.0f);
    f->rmix_prev = r1;
    f->reverb_idle_s = (r0 <= 1e-5f && r1 <= 1e-5f) ? f->reverb_idle_s + dt : 0.0f;
    /* a muted reverb goes idle; if it was frozen, the frozen tail is kept for when it comes back */
    const int reverb_on = f->has_reverb && f->reverb_idle_s < IDLE_AFTER_S;
    if (!reverb_on) {
        fxbus_flush(f, L, R, n);
        return;
    }
    const int muted = r1 <= 1e-5f, echoes = f->pending;
    for (int i = 0; i < n; i++)
        send[i] = muted ? 0.0f : 0.5f * (L[i] + R[i] + (echoes ? f->pl[i] + f->pr[i] : 0.0f));
    reverb_process(&f->reverb, send, wl, wr, n);
    dsp_ramp_mul(wl, r0, r1, n);
    dsp_ramp_mul(wr, r0, r1, n);
    fxbus_flush(f, L, R, n);
    for (int i = 0; i < n; i++) {
        L[i] += wl[i];
        R[i] += wr[i];
    }
}
