/* modal.c — modal synthesis engine (see modal.h).
 * Mode k: complex one-pole z <- z * r e^{jw} + excitation, output Im(z) (exact frequency and decay,
 * unconditionally stable, cheap: 4 mul + 4 add per sample). Coefficients are rebuilt per block only
 * when a parameter, the bend or the release state changes (pitch moves in block steps; the phasor
 * keeps the phase continuous). Bow excitation per mode is scaled by sqrt(1 - r^2) so its steady level
 * does not depend on the decay time. */
#include "modal.h"
#include "../core/fastmath.h"

#define MODAL_VOICE_GAIN 0.125f /* -18 dBFS per voice */
#define LOG2_1000 9.96578428f   /* T60: amplitude / 1000 */

enum { M_HARMONIC = 0, M_STRING, M_BAR, M_MARIMBA, M_BELL, M_MEMBRANE, M_BOWL, M_TUBE, M_COUNT };

static const char *const model_labels[] = {"harmonic", "string", "bar", "marimba", "bell",
                                            "membrane", "bowl", "tube", NULL};

#define TABLE_MODES 16

/* Measured/theoretical mode ratios for the table-based models (bar .. bowl, in enum order).
 * harmonic, string and tube are computed (any number of modes). Physical sources in modal.h. */
static const float ratio_table[M_BOWL - M_BAR + 1][TABLE_MODES] = {
    /* free-free uniform bar (glockenspiel, vibraphone without tuning) */
    {1.0f, 2.757f, 5.404f, 8.933f, 13.344f, 18.638f, 24.814f, 31.872f, 39.812f, 48.635f, 58.34f, 68.927f,
     80.397f, 92.748f, 105.982f, 120.1f},
    /* tuned (undercut) marimba bar: first overtones tuned near 4 and 10 */
    {1.0f, 3.99f, 9.95f, 17.3f, 26.7f, 37.9f, 50.9f, 65.6f, 82.1f, 100.3f, 120.4f, 142.0f, 165.5f, 190.7f,
     217.6f, 246.3f},
    /* church bell partials: hum, prime, tierce (minor third), quint, nominal, then upper partials */
    {0.5f, 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 2.667f, 3.0f, 4.0f, 5.333f, 6.0f, 6.667f, 8.0f, 9.333f, 10.667f, 12.0f},
    /* ideal circular membrane: zeros of the Bessel functions */
    {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.154f, 4.229f, 4.601f,
     4.832f, 4.903f},
    /* bowl / glass (circular-plate-like, widely spaced partials) */
    {1.0f, 2.71f, 5.15f, 8.43f, 12.5f, 17.4f, 23.0f, 29.4f, 36.6f, 44.5f, 53.2f, 62.6f, 72.8f, 83.7f, 95.4f,
     107.8f},
};

/* Ratio of partial j (0-based) to the note frequency, or 0 if the model has no such partial.
 * Stiff string: f_j = (j+1) f0 sqrt((1 + B (j+1)^2) / (1 + B)), normalized so partial 1 is in tune. */
static float model_ratio(int model, int j, float B)
{
    const float k = (float)(j + 1);
    switch (model) {
    case M_HARMONIC: return k;
    case M_STRING: return k * fm_exp2(0.5f * (fm_log2(1.0f + B * k * k) - fm_log2(1.0f + B)));
    case M_TUBE: return 2.0f * k - 1.0f;
    default: return j < TABLE_MODES ? ratio_table[model - M_BAR][j] : 0.0f;
    }
}

const param_desc_t modal_params[MODAL_P_COUNT] = {
    PARAM(MODAL_P_MODEL, "model", "resonator.model", 0.0f, 7.0f, 4.0f, PARAM_INT, "", 0, model_labels,
          "resonator: harmonic = ideal string, string = stiff string (piano; see inharmonicity), bar = glockenspiel, "
          "marimba, bell = church bell (minor-third tierce, low hum), membrane = drum/gong, bowl = singing bowl/glass, "
          "tube = hollow odd harmonics"),
    PARAM(MODAL_P_MODES, "modes", "resonator.modes", 1.0f, 32.0f, 12.0f, PARAM_INT, "", 0, NULL,
          "number of resonant modes (more = richer and costlier): 6-8 for bars and bells, 24-32 for a piano; "
          "table models (bar..bowl) use at most 16"),
    PARAM(MODAL_P_STRETCH, "stretch", "resonator.stretch", -0.3f, 0.3f, 0.0f, PARAM_LIN, "", 1, NULL,
          "bends the mode ratios (ratio^(1+stretch)): invents new, unheard materials; 0 = the physical model"),
    PARAM(MODAL_P_DECAY, "decay", "resonator.decay", 0.05f, 30.0f, 4.0f, PARAM_EXP, "s", 1, NULL,
          "ring time (T60) of the fundamental: 0.3 s wood, 2-4 s metal bar, 10+ s bell or bowl"),
    PARAM(MODAL_P_DAMPING, "damping", "resonator.damping", 0.0f, 4.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "how much faster high modes die (0 = all equal, metallic shimmer; 2+ = soft, wooden)"),
    PARAM(MODAL_P_RELEASE, "release", "resonator.release", 0.01f, 20.0f, 2.0f, PARAM_EXP, "s", 1, NULL,
          "ring time after note-off (a damper); set >= decay to let notes ring freely"),
    PARAM(MODAL_P_BRIGHTNESS, "brightness", "resonator.brightness", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "level of high modes vs low ones: 0 = dark and round, 1 = bright and glassy"),
    PARAM(MODAL_P_POSITION, "position", "exciter.position", 0.0f, 0.5f, 0.0f, PARAM_LIN, "", 1, NULL,
          "strike position (0 = off): 0.5 hits the middle and cancels even modes, 0.1-0.2 = edge, hollow comb"),
    PARAM(MODAL_P_HARDNESS, "hardness", "exciter.hardness", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "mallet hardness: 0 = felt (5 ms contact, dull), 1 = metal (0.15 ms, sharp attack)"),
    PARAM(MODAL_P_NOISE, "noise", "exciter.noise", 0.0f, 1.0f, 0.1f, PARAM_LIN, "", 1, NULL,
          "noise burst in the strike: scrape, breath, chiff"),
    PARAM(MODAL_P_VEL_BRIGHT, "vel_brightness", "exciter.velocity_brightness", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL,
          "soft notes are darker (like a real mallet)"),
    PARAM(MODAL_P_BOW, "bow", "exciter.bow", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "continuous noise excitation while the key is held: bowed glass, singing bowls, endless drones"),
    PARAM(MODAL_P_BOW_ATTACK, "bow_attack", "exciter.bow_attack", 0.01f, 10.0f, 1.0f, PARAM_EXP, "s", 1, NULL,
          "time for the bow to reach full level"),
    PARAM(MODAL_P_VARIATION, "variation", "resonator.variation", 0.0f, 30.0f, 4.0f, PARAM_LIN, "cent", 0, NULL,
          "random per-note detune of each mode: every strike is slightly different, beating partials"),
    PARAM(MODAL_P_VOLUME, "volume", "output.volume", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume in dB"),
    PARAM(MODAL_P_SPREAD, "pan_spread", "voice.pan_spread", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL,
          "random stereo position per note (0 = centre)"),
    PARAM(MODAL_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL, "pitch-bend range in semitones"),
    PARAM(MODAL_P_INHARMONICITY, "inharmonicity", "resonator.inharmonicity", 0.00001f, 0.02f, 0.0004f, PARAM_EXP, "", 1, NULL,
          "string stiffness B (model string): partials sharpen as k*sqrt(1+B k^2); 1e-4 bass strings, "
          "3e-4..1e-3 piano middle, 5e-3+ bright treble / bell-like"),
    PARAM(MODAL_P_UNISON, "unison", "resonator.unison", 0.0f, 10.0f, 0.0f, PARAM_LIN, "cent", 1, NULL,
          "2 strings per partial (first 8 partials) detuned by this many cents, one decaying faster: piano "
          "beating and double decay (prompt sound + long aftersound); 0.5-2 cents for a piano"),
    PARAM(MODAL_P_DECAY_KEYTRACK, "decay_keytrack", "resonator.decay_keytrack", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "high notes ring shorter: decay x 2^(-keytrack x octaves above C4); ~0.8-1 for a piano"),
    PARAM(MODAL_P_AFTERSOUND, "aftersound", "resonator.aftersound", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "unison double decay contrast: 0 = two similar strings (soft, even ring), 1 = a loud fast-decaying "
          "string plus a quiet long one (marked piano attack then long aftersound)"),
    PARAM(MODAL_P_TENSION, "tension", "resonator.tension", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "string nonlinearity: a loud note stretches the string and raises its tension, mostly through the low "
          "partials: every partial starts sharp and glides down together as the note decays (the fundamental's "
          "envelope drives the others' pitch). 0 = linear; 0.3-0.6 = piano (a few cents), 1 = gong-like"),
};

/* ---------------- coefficients ---------------- */

static void build_coeffs(modal_t *e, modal_voice_t *v, int note)
{
    const float *p = e->p;
    const int model = dsp_clampi((int)p[MODAL_P_MODEL], 0, M_COUNT - 1);
    const int nm = dsp_clampi((int)p[MODAL_P_MODES], 1, MODAL_MODES);
    const float f0 = fm_mtof((float)note + e->bend * p[MODAL_P_BEND_RANGE] + v->vm_pitch);
    const float fmax = 0.45f * e->sr, inv_sr = 1.0f / e->sr;
    const float stretch = p[MODAL_P_STRETCH], B = p[MODAL_P_INHARMONICITY];
    const float bright = dsp_clampf(p[MODAL_P_BRIGHTNESS] - (1.0f - v->vel) * p[MODAL_P_VEL_BRIGHT] * 0.6f + 0.25f * v->vm_bright,
                                    0.0f, 1.0f);
    const float tilt = 2.0f * (1.0f - bright); /* gain ~ ratio^-tilt */
    const float pos = v->vm_timbre != 0.0f ? dsp_clampf(p[MODAL_P_POSITION] + 0.5f * v->vm_timbre, 0.0f, 1.0f) : p[MODAL_P_POSITION];
    const float t_rel = p[MODAL_P_RELEASE];
    /* pitch-dependent decay: x 2^(-keytrack * octaves above C4) */
    const float t_base = p[MODAL_P_DECAY] * fm_exp2(-p[MODAL_P_DECAY_KEYTRACK] * ((float)note - 60.0f) * (1.0f / 12.0f));
    /* unison: the first `pairs` partials get 2 modes each (strings detuned by +/- unison/2 cents,
     * one decaying faster: prompt sound, the other slower: aftersound); the rest are single */
    const float uni = p[MODAL_P_UNISON];
    const int pairs = uni > 0.0f ? dsp_clampi(nm / 2, 0, 8) : 0;
    float sum2 = 0.0f;
    int slot = 0;
    v->last_ratio = v->cut_ratio = 0.0f;

    for (int j = 0; slot < nm; j++) {
        const float ratio0 = model_ratio(model, j, B);
        if (ratio0 <= 0.0f)
            break; /* the model has no more partials */
        const float ratio_p = fm_exp2((1.0f + stretch) * fm_log2(ratio0));
        if (f0 * ratio_p >= fmax) {
            v->cut_ratio = ratio_p;
            break; /* partials are increasing: all the next ones are above Nyquist too */
        }
        v->last_ratio = ratio_p;
        float g = fm_exp2(-tilt * fm_log2(ratio_p));
        if (pos > 0.0f)
            g *= dsp_absf(fm_sin(0.5f * pos * (float)(j + 1))); /* sin(pi * (j+1) * pos) */
        const int strings = (j < pairs && slot + 1 < nm) ? 2 : 1;
        for (int u = 0; u < strings; u++, slot++) {
            const float cents = strings == 2 ? (u ? 0.5f : -0.5f) * uni : 0.0f;
            const float ratio = ratio_p * v->jitter[slot] * fm_exp2(cents * (1.0f / 1200.0f));
            const float f = dsp_minf(f0 * ratio, fmax);
            /* decay: high modes die faster with damping; the hum of a bell (ratio < 1) lasts longer */
            float t60 = t_base / dsp_maxf(0.2f, 1.0f + p[MODAL_P_DAMPING] * (ratio - 1.0f) * 0.25f);
            /* double decay: aftersound 0 -> strings x0.45 / x1.6, equal levels; 1 -> x0.3 / x2.0, levels
             * 0.8 / 0.6 (the fast string carries the attack, the slow one the long aftersound) */
            const float as = p[MODAL_P_AFTERSOUND];
            if (strings == 2)
                t60 *= u ? 1.6f + 0.4f * as : 0.45f - 0.15f * as;
            if (v->released)
                t60 = dsp_minf(t60, t_rel);
            const float r = fm_exp2(-LOG2_1000 * inv_sr / t60);
            v->ratio[slot] = ratio;
            v->decay[slot] = r;
            float cs = fm_cos(f * inv_sr), sn = fm_sin(f * inv_sr);
            const float m2 = cs * cs + sn * sn;
            const float inv = 1.5f - 0.5f * m2; /* one Newton step: |(cs, sn)| = 1 to ~1e-12 */
            v->mode[slot].c = v->c0[slot] = r * cs * inv;
            v->mode[slot].s = v->s0[slot] = r * sn * inv;
            v->inc[slot] = f * inv_sr;
            v->w[slot] = (f / f0) * (f / f0);
            const float gs = strings == 2 ? g * (u ? 0.70710678f - 0.10710678f * as : 0.70710678f + 0.09289322f * as) : g;
            v->g_strike[slot] = gs;
            /* bow: steady level independent of decay, sqrt(1 - r^2) */
            v->g_bow[slot] = gs * fm_exp2(0.5f * fm_log2(dsp_maxf(1.0f - r * r, 1e-12f)));
            sum2 += gs * gs;
        }
    }
    /* unused / above-Nyquist modes are dropped, their state cleared */
    for (int k = slot; k < MODAL_MODES; k++) {
        reso_clear(&v->mode[k]);
        v->g_strike[k] = v->g_bow[k] = 0.0f;
    }
    v->nmodes = slot;
    /* normalize the strike so the mode bank has unit power */
    const float norm = sum2 > 0.0f ? fm_exp2(-0.5f * fm_log2(sum2)) : 0.0f;
    for (int k = 0; k < slot; k++) {
        v->g_strike[k] *= norm;
        v->g_bow[k] *= norm;
    }
    v->version = e->version;
    v->coeffs_released = v->released;
}

/* Only the pitch moved (per-note LFO, drift, bend of the voice): the gains and decays stay, the frequencies
 * are recomputed with the same expressions as build_coeffs (same result to the bit). Returns 0 when the set
 * of partials below 0.45 SR changes, which needs build_coeffs. */
static int retune_coeffs(modal_t *e, modal_voice_t *v, int note)
{
    const float *p = e->p;
    const float f0 = fm_mtof((float)note + e->bend * p[MODAL_P_BEND_RANGE] + v->vm_pitch);
    const float fmax = 0.45f * e->sr, inv_sr = 1.0f / e->sr;
    if (!(f0 * v->last_ratio < fmax) || (v->cut_ratio > 0.0f && !(f0 * v->cut_ratio >= fmax)))
        return 0;
    for (int k = 0; k < v->nmodes; k++) {
        const float f = dsp_minf(f0 * v->ratio[k], fmax), r = v->decay[k];
        float cs = fm_cos(f * inv_sr), sn = fm_sin(f * inv_sr);
        const float m2 = cs * cs + sn * sn;
        const float inv = 1.5f - 0.5f * m2;
        v->mode[k].c = v->c0[k] = r * cs * inv;
        v->mode[k].s = v->s0[k] = r * sn * inv;
        v->inc[k] = f * inv_sr;
        v->w[k] = (f / f0) * (f / f0);
    }
    return 1;
}

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    modal_t *e = eng;
    modal_voice_t *v = &e->v[vi];
    v->vel = dsp_clampf(vel, 0.0f, 1.0f);
    /* a harder blow compresses the felt: harder, brighter hammer at high velocity */
    const float hardness =
        dsp_clampf(e->p[MODAL_P_HARDNESS] + (v->vel - 0.7f) * 0.6f * e->p[MODAL_P_VEL_BRIGHT], 0.0f, 1.0f);
    if (hard) {
        for (int k = 0; k < MODAL_MODES; k++) {
            v->mode[k].zr = v->mode[k].zi = 0.0f;
            /* +/- variation cents per mode, fixed for this note */
            v->jitter[k] = fm_exp2(dsp_rand_bipolar(&e->rng) * e->p[MODAL_P_VARIATION] * (1.0f / 1200.0f));
        }
        v->jitter[0] = 1.0f; /* keep the reference mode in tune */
        v->vm_pitch = v->vm_bright = v->vm_timbre = 0.0f;
        const float pos = e->p[MODAL_P_SPREAD] * dsp_rand_bipolar(&e->rng);
        voice_pan(pos, &v->pan_l, &v->pan_r);
        v->bow_env = 0.0f;
        v->lp = 0.0f;
        v->rng = dsp_seed((uint32_t)note * 7919u + (uint32_t)vi);
    }
    v->released = 0;
    /* strike: raised-cosine pulse of unit area, 5 ms (felt) .. 0.15 ms (metal) */
    const float contact = 0.005f + (0.00015f - 0.005f) * hardness;
    v->strike_len = (int)(contact * e->sr) + 2;
    v->strike_pos = 0;
    v->strike_amp = 2.0f * v->vel / (float)v->strike_len; /* area = 2 x velocity: ~-18 dBFS per voice */
    v->noise_env = v->vel * e->p[MODAL_P_NOISE];
    v->noise_decay = fm_exp2(-FM_LOG2E / ((0.0005f + 0.004f * (1.0f - hardness)) * e->sr));
    v->gain_prev = MODAL_VOICE_GAIN * e->vol_prev;
    build_coeffs(e, v, note);
}

static void voice_release(void *eng, int vi) { ((modal_t *)eng)->v[vi].released = 1; }

static float voice_level(const void *eng, int vi) { return ((const modal_t *)eng)->v[vi].energy; }

static void voice_silence(void *eng, int vi)
{
    modal_voice_t *v = &((modal_t *)eng)->v[vi];
    for (int k = 0; k < MODAL_MODES; k++)
        v->mode[k].zr = v->mode[k].zi = 0.0f;
    v->bow_env = 0.0f;
    v->strike_pos = v->strike_len;
    v->noise_env = 0.0f;
    v->energy = 0.0f;
}

static const vpoly_ops_t modal_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void modal_init(void *st, float sample_rate)
{
    modal_t *e = st;
    e->sr = sample_rate;
    for (int i = 0; i < MODAL_P_COUNT; i++)
        e->p[i] = modal_params[i].def;
    e->version = 1;
    e->bend = 0.0f;
    smooth_init(&e->vol, 0.005f, sample_rate / (float)SYNTH_BLOCK, fm_db2lin(e->p[MODAL_P_VOLUME]));
    e->vol_prev = e->vol.y;
    e->rng = dsp_seed(0x30DA1u);
    e->nan_count = 0;
    vpoly_init(&e->poly, MODAL_VOICES, sample_rate, &modal_poly_ops, e);
    for (int i = 0; i < MODAL_VOICES; i++) {
        modal_voice_t *v = &e->v[i];
        for (int k = 0; k < MODAL_MODES; k++)
            v->jitter[k] = 1.0f;
        v->vel = 0.0f;
        v->released = 0;
        v->vm_pitch = v->vm_bright = v->vm_timbre = 0.0f;
        voice_silence(e, i);
    }
}

static vpoly_t *modal_poly(void *st) { return &((modal_t *)st)->poly; }

static void modal_set_param(void *st, int id, float norm)
{
    modal_t *e = st;
    if (id < 0 || id >= MODAL_P_COUNT)
        return;
    e->p[id] = param_from_norm(&modal_params[id], norm);
    e->version++;
}

static void modal_pitch_bend(void *st, float bend)
{
    modal_t *e = st;
    e->bend = dsp_clampf(bend, -1.0f, 1.0f);
    e->version++;
}

/* The string nonlinearity (tension modulation). The vibration stretches the string: the tension rises by
 * T = sum_k w_k y_k^2 (w_k = (f_k / f0)^2, y_k the mode's displacement), and every mode turns faster by a factor
 * 1 + g T: a loud note starts sharp and every partial glides down with the energy (driven mostly by the low
 * partials). Its fast components also frequency-modulate the modes slightly; the strong phantom partials of a
 * real piano come from longitudinal waves, not modelled here. A rotation keeps the modulus: stable whatever the
 * settings. The extra
 * angle d is small: cos d ~ 1 - d^2/2, sin d ~ d - d^3/6, whose modulus is just below 1 (never adds energy). */
#define TENSION_GAIN 0.004f     /* fractional speed-up per unit of tension, at tension 1 */
#define TENSION_MAX_SPEEDUP 0.06f

static DSP_FAST_CODE void voice_tension(modal_t *e, modal_voice_t *v, const float *es, const float *eb, float *y, int n)
{
    const float g = e->p[MODAL_P_TENSION] * TENSION_GAIN;
    const int nm = v->nmodes;
    /* the tension of the next sample is summed while the modes turn (one pass over them instead of two;
     * same terms in the same order, so the same result to the bit) */
    float T = 0.0f;
    for (int m = 0; m < nm; m++)
        T += v->w[m] * v->mode[m].zi * v->mode[m].zi;
    for (int i = 0; i < n; i++) {
        const float up = FM_TWO_PI * dsp_minf(g * T, TENSION_MAX_SPEEDUP);
        float out = 0.0f;
        T = 0.0f;
        /* two modes per pass: their independent chains of operations overlap in the FPU's pipeline; the sums
         * take the same terms in the same order, so the same result to the bit */
        int m = 0;
        for (; m + 1 < nm; m += 2) {
            reso_t *ra = &v->mode[m], *rb = &v->mode[m + 1];
            const float da = up * v->inc[m], db = up * v->inc[m + 1];
            const float da2 = da * da, db2 = db * db;
            const float cda = 1.0f - 0.5f * da2, sda = da * (1.0f - da2 * (1.0f / 6.0f));
            const float cdb = 1.0f - 0.5f * db2, sdb = db * (1.0f - db2 * (1.0f / 6.0f));
            const float ca = v->c0[m] * cda - v->s0[m] * sda, sa = v->s0[m] * cda + v->c0[m] * sda;
            const float cb = v->c0[m + 1] * cdb - v->s0[m + 1] * sdb, sb = v->s0[m + 1] * cdb + v->c0[m + 1] * sdb;
            const float ina = (es ? v->g_strike[m] * es[i] : 0.0f) + (eb ? v->g_bow[m] * eb[i] : 0.0f);
            const float inb = (es ? v->g_strike[m + 1] * es[i] : 0.0f) + (eb ? v->g_bow[m + 1] * eb[i] : 0.0f);
            const float nra = ca * ra->zr - sa * ra->zi + ina, nrb = cb * rb->zr - sb * rb->zi + inb;
            const float zia = sa * ra->zr + ca * ra->zi, zib = sb * rb->zr + cb * rb->zi;
            ra->zi = zia, ra->zr = nra;
            rb->zi = zib, rb->zr = nrb;
            out += zia;
            out += zib;
            T += v->w[m] * zia * zia;
            T += v->w[m + 1] * zib * zib;
        }
        for (; m < nm; m++) {
            reso_t *r = &v->mode[m];
            const float d = up * v->inc[m], d2 = d * d;
            const float cd = 1.0f - 0.5f * d2, sd = d * (1.0f - d2 * (1.0f / 6.0f));
            const float c = v->c0[m] * cd - v->s0[m] * sd, s = v->s0[m] * cd + v->c0[m] * sd;
            const float in = (es ? v->g_strike[m] * es[i] : 0.0f) + (eb ? v->g_bow[m] * eb[i] : 0.0f);
            const float nr = c * r->zr - s * r->zi + in;
            r->zi = s * r->zr + c * r->zi;
            r->zr = nr;
            out += r->zi;
            T += v->w[m] * r->zi * r->zi;
        }
        y[i] += out;
    }
}

static DSP_FAST_CODE void voice_render(modal_t *e, int vi, float vol0, float vol1, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    modal_voice_t *v = &e->v[vi];
    float es[SYNTH_BLOCK], eb[SYNTH_BLOCK], y[SYNTH_BLOCK];

    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, MPE: rebuild when it moves */
    const int pitch_moved = vm[VM_PITCH] != v->vm_pitch;
    const int full = vm[VM_BRIGHT] != v->vm_bright || vm[VM_TIMBRE] != v->vm_timbre || v->version != e->version ||
                     v->coeffs_released != v->released;
    v->vm_pitch = vm[VM_PITCH], v->vm_bright = vm[VM_BRIGHT], v->vm_timbre = vm[VM_TIMBRE];
    if (full || (pitch_moved && !retune_coeffs(e, v, vpoly_note(&e->poly, vi))))
        build_coeffs(e, v, vpoly_note(&e->poly, vi));

    /* excitation: strike pulse + noise burst, and the bow */
    int strike = 0;
    for (int i = 0; i < n; i++) {
        float x = 0.0f;
        if (v->strike_pos < v->strike_len) {
            const float ph = (float)v->strike_pos / (float)v->strike_len;
            x = v->strike_amp * (1.0f - fm_cos(ph));
            v->strike_pos++;
            strike = 1;
        }
        if (v->noise_env > 1e-5f) {
            x += v->noise_env * dsp_rand_bipolar(&v->rng) * 0.25f;
            v->noise_env *= v->noise_decay;
            strike = 1;
        }
        es[i] = x;
    }
    const float bow_target = v->released ? 0.0f : e->p[MODAL_P_BOW];
    const int bowing = bow_target > 0.0f || v->bow_env > 1e-5f;
    if (bowing) {
        const float t = v->released ? e->p[MODAL_P_RELEASE] : e->p[MODAL_P_BOW_ATTACK];
        const float k = 1.0f - fm_exp2(-FM_LOG2E * (float)n / (t * e->sr));
        const float b0 = v->bow_env, b1 = b0 + (bow_target - b0) * k;
        v->bow_env = b1;
        for (int i = 0; i < n; i++) {
            v->lp += (dsp_rand_bipolar(&v->rng) - v->lp) * 0.3f; /* soft, rosin-like noise */
            eb[i] = v->lp * (b0 + (b1 - b0) * (float)(i + 1) / (float)n) * 2.0f;
        }
    }

    /* resonator bank: modes outer, samples inner */
    for (int i = 0; i < n; i++)
        y[i] = 0.0f;
    float energy = 0.0f;
    if (e->p[MODAL_P_TENSION] > 0.0f) {
        voice_tension(e, v, strike ? es : 0, bowing ? eb : 0, y, n);
        for (int k = 0; k < v->nmodes; k++)
            energy += reso_energy(&v->mode[k]);
    } else
    for (int k = 0; k < v->nmodes; k++) {
        reso_t *m = &v->mode[k];
        if (strike && bowing)
            reso_drive2(m, es, v->g_strike[k], eb, v->g_bow[k], y, n);
        else if (strike)
            reso_drive(m, es, v->g_strike[k], y, n);
        else if (bowing)
            reso_drive(m, eb, v->g_bow[k], y, n);
        else
            reso_ring(m, y, n);
        energy += reso_energy(m);
    }
    v->energy = energy;

    /* gain (volume ramp) and steal fade */
    dsp_ramp_mul(y, MODAL_VOICE_GAIN * vol0, MODAL_VOICE_GAIN * vol1, n);
    voice_out(&e->poly, vi, y, n, v->pan_l, v->pan_r, L, R, &e->nan_count);
    /* silent: -100 dB of mode energy, strike over, no bow */
    const int sounding = energy > 1e-10f || v->strike_pos < v->strike_len || v->bow_env > 1e-5f || bow_target > 0.0f;
    vpoly_end_block(&e->poly, vi, sounding);
}

static DSP_FAST_CODE void modal_render(void *st, float *L, float *R)
{
    modal_t *e = st;
    const float vol0 = e->vol_prev, vol1 = smooth_next(&e->vol, fm_db2lin(e->p[MODAL_P_VOLUME]));
    e->vol_prev = vol1;
    for (int vi = 0; vi < MODAL_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, vol0, vol1, L, R);
}

/* the signal chain for interfaces (engine_stage_t): sources, then filter and amp, modulators below */
static const engine_stage_t modal_stages[] = {
    {"exciter", "Exciter", "source", "hardness position noise bow bow_attack vel_brightness", "", NULL, NULL, 0.0f, 0.0f},
    {"resonator", "Resonator", "filter", "model modes stretch inharmonicity decay damping brightness decay_keytrack tension unison aftersound",
     "exciter", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "release volume pan_spread", "resonator", NULL, NULL, 0.0f, 0.0f},
    {"motion", "Motion", "mod", "variation", "", "resonator", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t modal_engine = {
    .name = "modal",
    .description = "modal synthesis: banks of up to 32 resonators per note, struck (hammer/mallet + noise) or bowed (noise), "
                       "with stiff string (piano: inharmonicity, unison strings, double decay), bar, marimba, bell, membrane, bowl "
                       "and tube models (12 voices)",
    .params = modal_params,
    .param_count = MODAL_P_COUNT,
    .voices = MODAL_VOICES,
    .init = modal_init,
    .poly = modal_poly,
    .set_param = modal_set_param,
    .pitch_bend = modal_pitch_bend,
    .mod_wheel = NULL,
    .render = modal_render,
    .stages = modal_stages,
    .stage_count = (int)(sizeof modal_stages / sizeof modal_stages[0]),
};
