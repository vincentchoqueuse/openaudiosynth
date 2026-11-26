/* poly.c — classic polysynth engine (see poly.h).
 * Osc B: phase accumulator with ordinary polyBLEP/BLAMP corrections (saw, triangle, pulse summed).
 * Osc A: its frequency may move per sample (poly-mod from osc B) and its phase may be reset by osc B
 * (hard sync), so its discontinuities are corrected as events: for a jump h at fractional time
 * (1 - d) between two samples, the previous sample gets h d^2/2 and the current one -h (1-d)^2/2
 * (two-point polyBLEP), which is why osc A is delayed by one sample.
 * Unison: one note drives all voices, each detuned and panned. */
#include "poly.h"
#include "../core/fastmath.h"
#include "../osc/polyblep.h"

#define POLY_GAIN 0.11f

static const char *const lfo_waves[] = {"tri", "saw", "square", NULL};

#define SW(ID, name, role, def, help) PARAM(ID, name, role, 0.0f, 1.0f, def, PARAM_INT, "", 0, param_onoff_labels, help)
#define ADSR4(A, D, S, R, pre, rolep, a, d, s, r, what)                                                       \
    PARAM(A, pre "_attack", rolep ".attack", 0.001f, 10.0f, a, PARAM_EXP, "s", 1, NULL, what " attack"),     \
    PARAM(D, pre "_decay", rolep ".decay", 0.005f, 10.0f, d, PARAM_EXP, "s", 1, NULL, what " decay"),        \
    PARAM(S, pre "_sustain", rolep ".sustain", 0.0f, 1.0f, s, PARAM_LIN, "", 1, NULL, what " sustain"),      \
    PARAM(R, pre "_release", rolep ".release", 0.005f, 10.0f, r, PARAM_EXP, "s", 1, NULL, what " release")

const param_desc_t poly_params[POLY_P_COUNT] = {
    PARAM(POLY_P_OSCA_SEMI, "osca_semi", "osc1.semitone", -24.0f, 24.0f, 0.0f, PARAM_INT, "st", 0, NULL,
          "osc A transposition (with sync on: sweep it, or poly-mod it, for the sync lead)"),
    SW(POLY_P_OSCA_SAW, "osca_saw", "osc1.wave_saw", 1.0f, "osc A sawtooth on/off"),
    SW(POLY_P_OSCA_PULSE, "osca_pulse", "osc1.wave_pulse", 0.0f, "osc A pulse on/off (both = saw + pulse summed)"),
    PARAM(POLY_P_OSCA_PW, "osca_pw", "osc1.pulse_width", 0.05f, 0.95f, 0.5f, PARAM_LIN, "", 1, NULL, "osc A pulse width"),
    SW(POLY_P_OSCA_SYNC, "osca_sync", "osc1.sync", 0.0f, "osc A hard-synced to osc B: tearing, vocal harmonics"),
    PARAM(POLY_P_OSCB_SEMI, "oscb_semi", "osc2.semitone", -24.0f, 24.0f, 0.0f, PARAM_INT, "st", 0, NULL, "osc B transposition"),
    PARAM(POLY_P_OSCB_FINE, "oscb_fine", "osc2.detune", -50.0f, 50.0f, 7.0f, PARAM_LIN, "cent", 1, NULL,
          "osc B fine tune against A: 5-10 cents = the classic warm beating"),
    SW(POLY_P_OSCB_SAW, "oscb_saw", "osc2.wave_saw", 1.0f, "osc B sawtooth on/off"),
    SW(POLY_P_OSCB_TRI, "oscb_tri", "osc2.wave_triangle", 0.0f, "osc B triangle on/off"),
    SW(POLY_P_OSCB_PULSE, "oscb_pulse", "osc2.wave_pulse", 0.0f, "osc B pulse on/off"),
    PARAM(POLY_P_OSCB_PW, "oscb_pw", "osc2.pulse_width", 0.05f, 0.95f, 0.5f, PARAM_LIN, "", 1, NULL, "osc B pulse width"),
    SW(POLY_P_OSCB_LOFREQ, "oscb_lofreq", "osc2.low_freq", 0.0f,
       "osc B in sub-audio range: a per-voice LFO for poly-mod (wobbles, slow PWM)"),
    SW(POLY_P_OSCB_KEYTRACK, "oscb_keytrack", "osc2.keytrack", 1.0f, "osc B follows the keyboard (off = drone / fixed modulator)"),
    PARAM(POLY_P_MIX_A, "mix_a", "osc1.level", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL, "osc A level"),
    PARAM(POLY_P_MIX_B, "mix_b", "osc2.level", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL, "osc B level (0 with sync = pure sync lead)"),
    PARAM(POLY_P_NOISE, "noise", "noise.level", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, "noise level"),
    PARAM(POLY_P_CUTOFF, "cutoff", "filter.cutoff", 20.0f, 18000.0f, 1400.0f, PARAM_EXP, "Hz", 1, NULL, "filter cutoff"),
    PARAM(POLY_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "resonance (self-oscillates near 1)"),
    PARAM(POLY_P_FENV_AMT, "fenv_amount", "filter.envelope_amount", -4.0f, 6.0f, 2.0f, PARAM_LIN, "oct", 1, NULL,
          "filter envelope amount (negative = inverted)"),
    PARAM(POLY_P_KEYTRACK, "keytrack", "filter.keytrack", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, "filter keyboard tracking"),
    ADSR4(POLY_P_FENV_A, POLY_P_FENV_D, POLY_P_FENV_S, POLY_P_FENV_R, "fenv", "filter_env", 0.005f, 0.5f, 0.3f, 0.4f, "filter envelope"),
    ADSR4(POLY_P_AMP_A, POLY_P_AMP_D, POLY_P_AMP_S, POLY_P_AMP_R, "amp", "amp", 0.005f, 0.4f, 0.8f, 0.4f, "amp envelope"),
    PARAM(POLY_P_PM_FENV, "pm_fenv", "polymod.source_filter_env", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "poly-mod: filter envelope -> destinations (with sync and freq A: the sweeping sync scream)"),
    PARAM(POLY_P_PM_OSCB, "pm_oscb", "polymod.source_osc2", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "poly-mod: osc B -> destinations at audio rate (freq A = FM bells and clangs)"),
    SW(POLY_P_PM_FREQ_A, "pm_freq_a", "polymod.to_freq1", 0.0f, "poly-mod modulates osc A frequency"),
    SW(POLY_P_PM_PW_A, "pm_pw_a", "polymod.to_pw1", 0.0f, "poly-mod modulates osc A pulse width"),
    SW(POLY_P_PM_FILTER, "pm_filter", "polymod.to_cutoff", 0.0f, "poly-mod modulates the filter cutoff"),
    PARAM(POLY_P_LFO_RATE, "lfo_rate", "lfo.rate", 0.05f, 30.0f, 5.0f, PARAM_EXP, "Hz", 1, NULL, "LFO rate"),
    PARAM(POLY_P_LFO_WAVE, "lfo_wave", "lfo.waveform", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, lfo_waves, "LFO shape"),
    PARAM(POLY_P_LFO_AMOUNT, "lfo_amount", "lfo.amount", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "initial LFO amount; the mod wheel adds to it"),
    PARAM(POLY_P_LFO_PITCH, "lfo_pitch", "lfo.to_pitch", 0.0f, 2.0f, 0.2f, PARAM_LIN, "st", 1, NULL, "LFO -> both oscillators pitch (vibrato)"),
    PARAM(POLY_P_LFO_PWM, "lfo_pwm", "lfo.to_pulse_width", 0.0f, 0.45f, 0.0f, PARAM_LIN, "", 1, NULL, "LFO -> both pulse widths"),
    PARAM(POLY_P_LFO_FILTER, "lfo_filter", "lfo.to_cutoff", 0.0f, 3.0f, 0.0f, PARAM_LIN, "oct", 1, NULL, "LFO -> cutoff"),
    PARAM(POLY_P_GLIDE, "glide", "voice.glide", 0.0f, 2.0f, 0.0f, PARAM_LIN, "s", 1, NULL, "portamento"),
    SW(POLY_P_UNISON, "unison", "voice.unison", 0.0f, "all voices on one note: huge mono leads and basses"),
    PARAM(POLY_P_UNISON_DETUNE, "unison_detune", "voice.unison_detune", 0.0f, 50.0f, 12.0f, PARAM_LIN, "cent", 1, NULL,
          "detune spread of the unison voices"),
    PARAM(POLY_P_DRIFT, "drift", "voice.drift", 0.0f, 15.0f, 4.0f, PARAM_LIN, "cent", 0, NULL,
          "analog drift: slow random wander of each oscillator (standard deviation, cents): voices never quite agree"),
    PARAM(POLY_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 0, NULL,
          "velocity -> loudness and filter (the original had none)"),
    PARAM(POLY_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(POLY_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 0, NULL, "stereo spread of the voices"),
    PARAM(POLY_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range"),
    PARAM_OVERSAMPLE(POLY_P_OVERSAMPLE, 0.0f, "oversampling of the filters: 1x (default, lightest for 8 voices), 2x "
                     "(clean drive and resonance, ~2.5x the filter cost), 4x (cleanest, ~5x)"),
    ANALOG_PARAMS(POLY_P_TOLERANCE, POLY_P_DRIFT_TIME, POLY_P_JITTER),
};

/* ---------------- voices ---------------- */

static void voice_init_note(poly_t *e, poly_voice_t *v, int note, float vel, int hard)
{
    if (hard) {
        v->pa = dsp_rand_unipolar(&e->rng);
        v->pb = dsp_rand_unipolar(&e->rng);
        v->ya_prev = 0.0f;
        ladder_reset(&v->filter);
        adsr_reset(&v->fenv);
        adsr_reset(&v->amp);
        v->pitch = (e->p[POLY_P_GLIDE] > 0.0f && e->has_last) ? e->last_note : (float)note;
        const float pos = e->p[POLY_P_SPREAD] * dsp_rand_bipolar(&e->rng);
        voice_pan(pos, &v->pan_l, &v->pan_r);
        v->gain_prev = 0.0f;
    }
    v->velgain = 1.0f - e->p[POLY_P_VELOCITY] * (1.0f - vel);
    adsr_gate(&v->fenv, 1);
    adsr_gate(&v->amp, 1);
}

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    poly_t *e = eng;
    if (e->unison) /* voice 0 drives every voice */
        for (int u = 0; u < POLY_VOICES; u++)
            voice_init_note(e, &e->v[u], note, vel, hard);
    else
        voice_init_note(e, &e->v[vi], note, vel, hard);
    e->last_note = (float)note;
    e->has_last = 1;
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    poly_t *e = eng;
    (void)vi, (void)vel;
    if (e->p[POLY_P_GLIDE] <= 0.0f)
        for (int u = 0; u < POLY_VOICES; u++)
            e->v[u].pitch = (float)note;
    e->last_note = (float)note;
}

static void voice_release(void *eng, int vi)
{
    poly_t *e = eng;
    const int a = e->unison ? 0 : vi, b = e->unison ? POLY_VOICES : vi + 1;
    for (int u = a; u < b; u++) {
        adsr_gate(&e->v[u].fenv, 0);
        adsr_gate(&e->v[u].amp, 0);
    }
}

static float voice_level(const void *eng, int vi) { return ((const poly_t *)eng)->v[vi].amp.level; }

static void voice_silence(void *eng, int vi)
{
    poly_t *e = eng;
    const int a = e->unison ? 0 : vi, b = e->unison ? POLY_VOICES : vi + 1;
    for (int u = a; u < b; u++) {
        adsr_reset(&e->v[u].fenv);
        adsr_reset(&e->v[u].amp);
    }
}

static const vpoly_ops_t poly_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void poly_init(void *st, float sr)
{
    poly_t *e = st;
    const float ctrl = sr / (float)SYNTH_BLOCK;
    e->sr = sr;
    for (int i = 0; i < POLY_P_COUNT; i++)
        e->p[i] = poly_params[i].def;
    smooth_init(&e->sm_cut, 0.005f, ctrl, fm_log2(e->p[POLY_P_CUTOFF]));
    smooth_init(&e->sm_res, 0.005f, ctrl, e->p[POLY_P_RESO]);
    lfo_init(&e->lfo, sr, 0x50u);
    lfo_set(&e->lfo, e->p[POLY_P_LFO_RATE], LFO_TRI);
    e->bend = e->modwheel = 0.0f;
    e->last_note = 60.0f;
    e->has_last = 0;
    e->env_dirty = 1;
    e->unison = 0;
    e->rng = dsp_seed(0x9A9Au);
    e->nan_count = 0;
    for (int i = 0; i < POLY_VOICES; i++) {
        poly_voice_t *v = &e->v[i];
        v->pa = v->pb = v->ya_prev = 0.0f;
        v->pitch = 60.0f;
        v->velgain = 1.0f;
        v->gain_prev = 0.0f;
        v->pan_l = v->pan_r = 0.70710678f;
        v->b_last = 0.0f;
        /* unison copies spread symmetrically, slightly irregular */
        v->unison_cents = ((float)i - 3.5f) / 3.5f * (i & 1 ? 1.0f : 0.83f);
        ladder_init(&v->filter, sr);
        adsr_init(&v->fenv, sr);
        adsr_init(&v->amp, sr);
        analog_init(&v->analog[0], 0x7000u + 2u * (uint32_t)i);
        analog_init(&v->analog[1], 0x7001u + 2u * (uint32_t)i);
        noise_init(&v->noise, 0x8000u + (uint32_t)i);
    }
    vpoly_init(&e->poly, POLY_VOICES, sr, &poly_poly_ops, e);
}

static vpoly_t *poly_poly(void *st) { return &((poly_t *)st)->poly; }

static void poly_set_param(void *st, int id, float norm)
{
    poly_t *e = st;
    if (id < 0 || id >= POLY_P_COUNT)
        return;
    e->p[id] = param_from_norm(&poly_params[id], norm);
    if (vpoly_active_count(&e->poly) == 0) { /* silent: nothing to glide, the next note starts on the new values */
        smooth_snap(&e->sm_cut, fm_log2(e->p[POLY_P_CUTOFF]));
        smooth_snap(&e->sm_res, e->p[POLY_P_RESO]);
    }
    switch (id) {
    case POLY_P_UNISON:
        e->unison = (int)e->p[id];
        vpoly_all_sound_off(&e->poly);
        vpoly_set_mode(&e->poly, e->unison ? VPOLY_LEGATO : VPOLY_POLY);
        break;
    case POLY_P_LFO_RATE:
    case POLY_P_LFO_WAVE: {
        static const lfo_wave_t w[3] = {LFO_TRI, LFO_SAW, LFO_SQUARE};
        lfo_set(&e->lfo, e->p[POLY_P_LFO_RATE], w[(int)e->p[POLY_P_LFO_WAVE]]);
        break;
    }
    default:
        if (id >= POLY_P_FENV_A && id <= POLY_P_AMP_R)
            e->env_dirty = 1;
        break;
    }
}

static void poly_pitch_bend(void *st, float bend) { ((poly_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }
static void poly_mod_wheel(void *st, float v) { ((poly_t *)st)->modwheel = dsp_clampf(v, 0.0f, 1.0f); }

/* Two-point polyBLEP corrections for a jump h at fractional time (1 - d) before the current sample. */
static inline void step_event(float h, float d, float *prev, float *cur)
{
    *prev += h * 0.5f * d * d;
    const float r = 1.0f - d;
    *cur -= h * 0.5f * r * r;
}

typedef struct {
    float lfo, depth;
    float cut;         /* smoothed base cutoff (log2 Hz), advanced once per block for every voice */
    const float *fade; /* steal fade of the controlling vpoly voice for this block, or NULL */
    int vi;            /* the controlling vpoly voice (0 for every unison copy) */
    analog_cfg_t analog;
} poly_block_t;

static DSP_FAST_CODE void voice_render(poly_t *e, int vi, int note, const poly_block_t *b, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    poly_voice_t *v = &e->v[vi];
    float x[SYNTH_BLOCK], env[SYNTH_BLOCK];

    /* pitch and frequencies (block rate) */
    if (p[POLY_P_GLIDE] > 0.0f)
        v->pitch += ((float)note - v->pitch) * voice_glide_coef(p[POLY_P_GLIDE], n, e->sr);
    else
        v->pitch = (float)note;
    const float slop = e->unison ? 0.01f * v->unison_cents * p[POLY_P_UNISON_DETUNE] : 0.0f;
    const float an_a = 0.01f * analog_next(&v->analog[0], &b->analog), an_b = analog_next(&v->analog[1], &b->analog); /* each oscillator its own */
    const float *vm = vpoly_vmod(&e->poly, b->vi); /* per-voice modulation, microtuning, MPE */
    const float common = v->pitch + e->bend * p[POLY_P_BEND_RANGE] + slop + b->lfo * b->depth * p[POLY_P_LFO_PITCH] + vm[VM_PITCH];
    adsr_process(&v->fenv, env, n);
    const float fe = env[n - 1];
    const float pm_f = p[POLY_P_PM_FENV] * fe, pm_b = p[POLY_P_PM_OSCB];
    const int to_freq = (int)p[POLY_P_PM_FREQ_A], to_pw = (int)p[POLY_P_PM_PW_A];
    const float semi_a = common + an_a + p[POLY_P_OSCA_SEMI] + (to_freq ? 24.0f * pm_f : 0.0f);
    float fb_hz;
    if ((int)p[POLY_P_OSCB_LOFREQ])
        fb_hz = 0.5f * fm_exp2((p[POLY_P_OSCB_SEMI] + 0.01f * p[POLY_P_OSCB_FINE] +
                                ((int)p[POLY_P_OSCB_KEYTRACK] ? v->pitch - 60.0f : 0.0f)) / 12.0f);
    else
        fb_hz = fm_mtof(((int)p[POLY_P_OSCB_KEYTRACK] ? common : 48.0f) + p[POLY_P_OSCB_SEMI] + 0.01f * p[POLY_P_OSCB_FINE]);
    const float inv_sr = 1.0f / e->sr;
    fb_hz *= fm_exp2(an_b * (1.0f / 1200.0f));
    const float dta0 = dsp_minf(fm_mtof(semi_a) * inv_sr, 0.45f), dtb = dsp_minf(fb_hz * inv_sr, 0.45f);
    const float idtb = 1.0f / dsp_maxf(dtb, 1e-7f);
    const float pw_lfo = b->lfo * b->depth * p[POLY_P_LFO_PWM];
    const float pwb = dsp_clampf(p[POLY_P_OSCB_PW] + pw_lfo, 0.05f, 0.95f);
    const float pwa_base = p[POLY_P_OSCA_PW] + pw_lfo + (to_pw ? 0.45f * pm_f : 0.0f);
    const int a_saw = (int)p[POLY_P_OSCA_SAW], a_pul = (int)p[POLY_P_OSCA_PULSE], sync = (int)p[POLY_P_OSCA_SYNC];
    const int b_saw = (int)p[POLY_P_OSCB_SAW], b_tri = (int)p[POLY_P_OSCB_TRI], b_pul = (int)p[POLY_P_OSCB_PULSE];
    const int nwb = dsp_clampi(b_saw + b_tri + b_pul, 1, 3); /* osc B's waveforms in use */
    const float mix_a = p[POLY_P_MIX_A] * (a_saw && a_pul ? 0.7f : 1.0f);
    const float mix_b = p[POLY_P_MIX_B] / (float)dsp_clampi(b_saw + b_tri + b_pul, 1, 3);
    const float mix_n = p[POLY_P_NOISE];

    float pa = v->pa, pb = v->pb, ya_prev = v->ya_prev, bsum = 0.0f;
    for (int i = 0; i < n; i++) {
        /* osc B */
        pb += dtb;
        int wrapped_b = 0;
        if (pb >= 1.0f) {
            pb -= 1.0f;
            wrapped_b = 1;
        }
        float ob = 0.0f;
        if (b_saw)
            ob += 2.0f * pb - 1.0f - 2.0f * osc_blep(pb, dtb, idtb);
        if (b_tri) {
            float th = pb + 0.5f;
            if (th >= 1.0f)
                th -= 1.0f;
            ob += 1.0f - 4.0f * dsp_absf(pb - 0.5f) + 8.0f * dtb * (osc_blamp(pb, dtb, idtb) - osc_blamp(th, dtb, idtb));
        }
        if (b_pul) {
            float t2 = pb - pwb;
            if (t2 < 0.0f)
                t2 += 1.0f;
            ob += (pb < pwb ? 1.0f : -1.0f) + 2.0f * osc_blep(pb, dtb, idtb) - 2.0f * osc_blep(t2, dtb, idtb);
        }
        const float bmod = nwb == 3 ? ob / 3.0f : nwb == 2 ? ob * 0.5f : ob; /* the same as ob / count, exactly, without a division per sample for 1 or 2 */
        bsum += bmod;

        /* osc A: audio-rate poly-mod from B, events with one-sample-delayed BLEPs */
        const float dta = (to_freq && pm_b > 0.0f) ? dsp_clampf(dta0 * fm_exp2(2.0f * pm_b * bmod), 0.0f, 0.45f) : dta0;
        const float pwa = dsp_clampf(pwa_base + (to_pw ? 0.45f * pm_b * bmod : 0.0f), 0.05f, 0.95f);
        const float prev = pa;
        float cur_corr = 0.0f;
        pa += dta;
        if (sync && wrapped_b) {
            const float d = dsp_minf(pb * idtb, 1.0f); /* samples since B wrapped */
            float aw = prev + dta * (1.0f - d);         /* A phase at the reset instant */
            if (aw >= 1.0f)
                aw -= 1.0f;
            const float before = (a_saw ? 2.0f * aw - 1.0f : 0.0f) + (a_pul ? (aw < pwa ? 1.0f : -1.0f) : 0.0f);
            const float after = (a_saw ? -1.0f : 0.0f) + (a_pul ? 1.0f : 0.0f);
            step_event(after - before, d, &ya_prev, &cur_corr);
            pa = dta * d;
        } else {
            if (pa >= 1.0f) { /* natural wrap: saw -2, pulse +2 */
                pa -= 1.0f;
                const float d = dsp_minf(pa / dta, 1.0f);
                step_event((a_saw ? -2.0f : 0.0f) + (a_pul ? 2.0f : 0.0f), d, &ya_prev, &cur_corr);
            }
            if (a_pul && prev < pwa && pa >= pwa) /* falling edge */
                step_event(-2.0f, dsp_minf((pa - pwa) / dta, 1.0f), &ya_prev, &cur_corr);
        }
        const float ya = (a_saw ? 2.0f * pa - 1.0f : 0.0f) + (a_pul ? (pa < pwa ? 1.0f : -1.0f) : 0.0f) + cur_corr;
        x[i] = mix_a * ya_prev + mix_b * ob + (mix_n > 0.0f ? mix_n * dsp_rand_bipolar(&v->noise.state) : 0.0f);
        ya_prev = ya;
    }
    v->pa = pa, v->pb = pb, v->ya_prev = ya_prev;
    v->b_last = bsum / (float)n;

    /* filter (block rate): base + envelope + keytrack + LFO + poly-mod + velocity */
    const float cut = b->cut + p[POLY_P_FENV_AMT] * fe +
                      p[POLY_P_KEYTRACK] * (v->pitch - 60.0f) / 12.0f + b->lfo * b->depth * p[POLY_P_LFO_FILTER] +
                      ((int)p[POLY_P_PM_FILTER] ? 3.0f * (pm_f + pm_b * v->b_last) : 0.0f) + 1.5f * (v->velgain - 1.0f) +
                      vm[VM_BRIGHT];
    ladder_set_oversampling(&v->filter, 1 << (int)p[POLY_P_OVERSAMPLE]);
    ladder_set(&v->filter, fm_exp2(cut), e->sm_res.y, 0.15f);
    ladder_process(&v->filter, x, x, n);

    /* amp */
    adsr_process(&v->amp, env, n);
    const float g1 = POLY_GAIN * v->velgain * fm_db2lin(p[POLY_P_VOLUME]) * (e->unison ? 0.45f : 1.0f);
    dsp_ramp_env_mul(x, env, v->gain_prev, g1, n);
    v->gain_prev = g1;
    if (b->fade) /* unison: one fade, computed once, for all copies */
        for (int i = 0; i < n; i++)
            x[i] *= b->fade[i];
    if (!e->unison) /* unison copies share one vpoly voice: per-voice level and pan only without unison */
        voice_vmod_out(&e->poly, vi, x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    else
        voice_pan_out(x, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
}

static DSP_FAST_CODE void poly_render(void *st, float *L, float *R)
{
    poly_t *e = st;
    const float *p = e->p;
    if (e->env_dirty) {
        for (int i = 0; i < POLY_VOICES; i++) {
            adsr_set(&e->v[i].fenv, p[POLY_P_FENV_A], p[POLY_P_FENV_D], p[POLY_P_FENV_S], p[POLY_P_FENV_R]);
            adsr_set(&e->v[i].amp, p[POLY_P_AMP_A], p[POLY_P_AMP_D], p[POLY_P_AMP_S], p[POLY_P_AMP_R]);
        }
        e->env_dirty = 0;
    }
    smooth_next(&e->sm_res, p[POLY_P_RESO]);
    poly_block_t b;
    b.cut = smooth_next(&e->sm_cut, fm_log2(p[POLY_P_CUTOFF]));
    float fade[SYNTH_BLOCK];
    b.lfo = lfo_advance(&e->lfo, SYNTH_BLOCK);
    b.depth = dsp_minf(1.0f, p[POLY_P_LFO_AMOUNT] + e->modwheel);
    analog_cfg(&b.analog, p[POLY_P_TOLERANCE], p[POLY_P_DRIFT], p[POLY_P_DRIFT_TIME], p[POLY_P_JITTER], (float)SYNTH_BLOCK / e->sr);
    if (e->unison) {
        if (!vpoly_active(&e->poly, 0))
            return;
        const int note = vpoly_note(&e->poly, 0);
        b.fade = vpoly_fade(&e->poly, 0, fade, SYNTH_BLOCK) ? fade : NULL; /* once for all copies */
        b.vi = 0;
        int sounding = 0;
        for (int u = 0; u < POLY_VOICES; u++) {
            voice_render(e, u, note, &b, L, R);
            sounding |= adsr_active(&e->v[u].amp);
        }
        vpoly_end_block(&e->poly, 0, sounding);
        return;
    }
    for (int vi = 0; vi < POLY_VOICES; vi++) {
        if (!vpoly_active(&e->poly, vi))
            continue;
        b.fade = vpoly_fade(&e->poly, vi, fade, SYNTH_BLOCK) ? fade : NULL;
        b.vi = vi;
        voice_render(e, vi, vpoly_note(&e->poly, vi), &b, L, R);
        vpoly_end_block(&e->poly, vi, adsr_active(&e->v[vi].amp));
    }
}

/* the signal chain for interfaces (engine_stage_t): sources, then filter and amp, modulators below */
static const engine_stage_t poly_stages[] = {
    {"osca", "Osc A", "source", "osca_ mix_a", "", NULL, "mix_a", 0.0f, 1.0f},
    {"oscb", "Osc B", "source", "oscb_ mix_b", "", NULL, "mix_b", 0.0f, 0.7f},
    {"noise", "Noise", "source", "noise", "", NULL, "noise", 0.0f, 0.2f},
    {"filter", "Filter", "filter", "cutoff resonance keytrack fenv_amount oversample", "osca oscb noise", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "amp_ velocity volume pan_spread", "filter", NULL, NULL, 0.0f, 0.0f},
    {"fenv", "Filter env", "mod", "fenv_attack fenv_decay fenv_sustain fenv_release", "", "filter", NULL, 0.0f, 0.0f},
    {"polymod", "Poly-mod", "mod", "pm_", "", "osca filter", NULL, 0.0f, 0.0f},
    {"lfo", "LFO", "mod", "lfo_", "", "osca oscb filter", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "glide unison unison_detune drift drift_time tolerance jitter bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t poly_engine = {
    .name = "poly",
    .description = "classic polysynth (late-70s 5-voice spirit): osc A saw/pulse with hard sync, osc B saw/tri/pulse "
                   "with low-freq mode, poly-mod (filter env and osc B -> freq A, PW A, filter), 24 dB ladder, "
                   "2 ADSR, LFO + wheel, glide, unison, analog slop (8 voices)",
    .params = poly_params,
    .param_count = POLY_P_COUNT,
    .voices = POLY_VOICES,
    .init = poly_init,
    .poly = poly_poly,
    .set_param = poly_set_param,
    .pitch_bend = poly_pitch_bend,
    .mod_wheel = poly_mod_wheel,
    .render = poly_render,
    .stages = poly_stages,
    .stage_count = (int)(sizeof poly_stages / sizeof poly_stages[0]),
};
