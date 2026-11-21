/* drums.c — drum machine engine (see drums.h). Each hit = up to five parts set up at note-on and
 * rendered per sample: tone (2 sine oscillators with fast + slow pitch envelopes and an optional
 * waveshaper), noise (filtered white noise, optional clap bursts), metal (6 detuned polyBLEP squares,
 * band-passed then high-passed, optionally 6-bit quantized), click, and 2 resonators (rim, clave).
 * Envelopes are exponential with T60 times. Pitch envelopes move per block, interpolated per sample.
 * Metal oscillator frequencies (205.3, 304.4, 369.6, 522.7, 540, 800 Hz) follow published analyses of
 * the classic analog cymbal circuit. */
#include "drums.h"
#include "../core/fastmath.h"

#define LOG2_1000 9.96578428f

const char *const drums_instrument_names[DR_COUNT] = {"kick", "snare", "clap", "rim", "chat", "ohat",
                                                      "ltom", "mtom", "htom", "cymbal", "cowbell", "clave", "bass"};
const uint8_t drums_instrument_notes[DR_COUNT] = {36, 38, 39, 37, 42, 46, 41, 45, 48, 49, 56, 75, 29};

/* Output gain per instrument (calibrated so the default kit sits around -12..-6 dBFS peak). */
static const float inst_gain[DR_COUNT] = {0.55f, 0.42f, 1.6f, 0.4f, 0.32f, 0.3f,
                                          0.5f, 0.5f, 0.5f, 0.28f, 0.17f, 0.4f, 0.5f};
/* Stereo position at full stereo width (-1 left .. 1 right), a drummer's-view kit. */
static const float inst_pan[DR_COUNT] = {0.0f, 0.0f, 0.1f, -0.2f, 0.35f, 0.4f,
                                         -0.45f, -0.1f, 0.3f, -0.35f, 0.2f, 0.45f, 0.0f};

static const char *const kit_labels[] = {"analog", "hybrid", "glitch", "trap", NULL};

#define INST(I, n, tune_help, decay_help, tone_help)                                                     \
    PARAM(DRUMS_P(I, DRUMS_LEVEL), n "_level", "drum." n ".level", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 1, NULL, n " level"), \
    PARAM(DRUMS_P(I, DRUMS_TUNE), n "_tune", "drum." n ".tune", -12.0f, 12.0f, 0.0f, PARAM_LIN, "st", 1, NULL, tune_help),  \
    PARAM(DRUMS_P(I, DRUMS_DECAY), n "_decay", "drum." n ".decay", 0.2f, 5.0f, 1.0f, PARAM_EXP, "x", 1, NULL, decay_help),   \
    PARAM(DRUMS_P(I, DRUMS_TONE), n "_tone", "drum." n ".tone", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL, tone_help)

const param_desc_t drums_params[DRUMS_P_COUNT] = {
    PARAM(DRUMS_P_KIT, "kit", "drum.kit", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 0, kit_labels,
          "analog = deep booming kick, soft round snare, silky metallic hats (electro, hip-hop, ambient); "
          "hybrid = punchy clicky kick, bright snappy snare, gritty hats and rides (house, techno); "
          "glitch = digital microsound: sub sine pulses, single-sample clicks, pure high beeps, white and "
          "decimated data noise, modem chirps (minimal, glitch, installation); trap = short punchy kick, tight "
          "cracking snare, layered roomy clap, crisp bright hats for fast rolls"),
    PARAM(DRUMS_P_ACCENT, "accent", "drum.accent", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 0, NULL,
          "how much velocity changes level, decay and brightness (0 = every hit equal)"),
    PARAM(DRUMS_P_DRIVE, "drive", "drum.drive", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "saturation of the whole kit: glue and grit, louder transients become rounder"),
    PARAM(DRUMS_P_STEREO, "stereo", "drum.stereo", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 0, NULL,
          "spread of the instruments in the stereo field (0 = mono like the original machines)"),
    PARAM(DRUMS_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "kit volume (applies to new hits)"),
    INST(DR_KICK, "kick", "kick pitch (default ~49 Hz analog, ~52 Hz hybrid)",
         "kick length: 0.3 = tight, 1 = default, 3+ = long sub boom",
         "click / attack amount (hybrid: beater click and noise)"),
    INST(DR_SNARE, "snare", "snare body pitch", "snare length", "snappy: 0 = mostly drum body, 1 = mostly wires"),
    INST(DR_CLAP, "clap", "clap filter pitch", "clap tail (room) length", "clap brightness"),
    INST(DR_RIM, "rim", "rim shot pitch", "rim shot length", "click amount"),
    INST(DR_CHAT, "chat", "closed hi-hat metal pitch", "closed hi-hat length", "brightness (filter centre)"),
    INST(DR_OHAT, "ohat", "open hi-hat metal pitch", "open hi-hat length (choked by the closed hat)",
         "brightness (filter centre)"),
    INST(DR_LTOM, "ltom", "low tom pitch", "low tom length", "skin noise amount"),
    INST(DR_MTOM, "mtom", "mid tom pitch", "mid tom length", "skin noise amount"),
    INST(DR_HTOM, "htom", "high tom pitch", "high tom length", "skin noise amount"),
    INST(DR_CYMBAL, "cymbal", "cymbal metal pitch", "cymbal length (crash/ride)", "brightness"),
    INST(DR_COWBELL, "cowbell", "cowbell pitch", "cowbell length", "brightness"),
    INST(DR_CLAVE, "clave", "clave pitch", "clave length", "unused"),
    INST(DR_BASS, "bass", "bass drum transposition (it plays at its note's pitch, notes 0..34)",
         "bass drum length: 1 = about 2 s, up to 5x", "drive: 0 = a clean sine, 1 = a growling, saturated boom"),
    PARAM(DRUMS_P_GLIDE, "glide", "drum.bass.glide", 0.0f, 0.5f, 0.08f, PARAM_LIN, "s", 1, NULL,
          "bass drum slide: a note played while the previous one is held slides from its pitch over this time")
};

int drums_instrument_for_note(int note)
{
    switch (note) {
    case 35: case 36: return DR_KICK;
    case 37: return DR_RIM;
    case 38: case 40: return DR_SNARE;
    case 39: return DR_CLAP;
    case 42: case 44: return DR_CHAT;
    case 46: return DR_OHAT;
    case 41: case 43: return DR_LTOM;
    case 45: case 47: return DR_MTOM;
    case 48: case 50: return DR_HTOM;
    case 49: case 51: case 52: case 55: case 57: case 59: return DR_CYMBAL;
    case 56: return DR_COWBELL;
    case 75: case 76: case 77: return DR_CLAVE;
    default: return note >= 0 && note <= 34 ? DR_BASS : -1; /* under the drum map: the melodic bass drum */
    }
}

/* ---------------- set-up helpers ---------------- */

static float t60c(const drums_t *e, float t60) { return fm_exp2(-LOG2_1000 / (dsp_maxf(t60, 0.0005f) * e->sr)); }
static float t60c_block(const drums_t *e, float t60)
{
    return fm_exp2(-LOG2_1000 * (float)SYNTH_BLOCK / (dsp_maxf(t60, 0.0005f) * e->sr));
}

/* resonator tuned to f (Hz), ringing for t60 (s), started at amplitude amp */
static void reso_init(const drums_t *e, reso_t *r, float f, float t60, float amp)
{
    reso_tune(r, f, t60, e->sr);
    reso_strike(r, amp);
}

static void metal_init(drums_t *e, drum_voice_t *v, const float *freqs, int n, float mult)
{
    v->metal_n = n;
    for (int k = 0; k < n; k++) {
        osc_init(&v->metal[k], e->sr);
        osc_set_wave(&v->metal[k], OSC_PULSE);
        osc_reset(&v->metal[k], dsp_rand_unipolar(&e->rng));
        osc_set(&v->metal[k], freqs[k] * mult, 0.5f);
    }
}

static void clear_parts(drum_voice_t *v)
{
    v->f[0] = v->f[1] = 0.0f;
    v->ph[0] = v->ph[1] = 0.0f;
    v->pe_a = v->pe_b = 0.0f;
    v->pe_a_st = v->pe_b_st = 0.0f;
    v->pe_a_coef = v->pe_b_coef = 1.0f;
    v->env_tone = v->env_noise = v->env_metal = v->env_metal2 = v->env_click = v->env_burst = 0.0f;
    v->c_tone = v->c_noise = v->c_metal = v->c_metal2 = v->c_click = v->c_burst = 0.0f;
    v->burst_left = v->burst_timer = v->burst_period = 0;
    v->mix_tone = v->mix_tone2 = v->mix_noise = v->mix_metal = v->mix_click = v->metal_noise = 0.0f;
    v->shape = 0.0f;
    v->shape_norm = 1.0f;
    v->metal_n = 0;
    v->qsteps = 0.0f;
    v->hold_tone = v->hold_noise = 0;
    v->sh_period = 1, v->sh_count = 0, v->sh_val = 0.0f;
    v->ring = 0;
    v->noise_f2 = 0;
    v->noise_att = 1.0f;
    v->noise_att_step = 1.0f;
    reso_clear(&v->reso[0]);
    reso_clear(&v->reso[1]);
}

/* ---------------- vpoly callbacks ---------------- */

/* ---------------- instrument recipes ----------------
 * Every instrument is a recipe that sets the generic parts of a voice (tone oscillators, pitch envelopes,
 * noise, metal oscillators, click, clap bursts, resonators, filters); voice_render plays the parts.
 * One function per instrument and kit family: `setup_*` for the analog and hybrid kits (c->hy selects
 * hybrid), `glitch_*` for the glitch kit. */

typedef struct {
    int inst, vi, hy, note;
    float m;  /* tuning multiplier */
    float D;  /* decay multiplier (with accent) */
    float tn; /* tone 0..1 (with accent) */
} drum_setup_t;

typedef void (*drum_recipe_t)(drums_t *e, drum_voice_t *v, const drum_setup_t *c);

static const float metal6[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float bell2[2] = {540.0f, 800.0f};
static const float tom_a[3] = {85.0f, 125.0f, 170.0f}, tom_h[3] = {95.0f, 135.0f, 185.0f};
static const float tone_f[3] = {220.0f, 440.0f, 880.0f};

static void glitch_kick(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const float sr = e->sr;
    /* gated sub sine pulse + single-sample click */
               v->f[0] = 45.0f * m, v->mix_tone = 1.0f, v->env_tone = 1.0f;
               v->hold_tone = (int)(0.05f * D * sr), v->c_tone = t60c(e, 0.02f);
               v->env_click = 1.0f, v->c_click = 0.0f, v->mix_click = 0.8f * tn;
}

static void glitch_snare(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float D = c->D;
    const float tn = c->tn;
    const float sr = e->sr;
    /* white noise burst, decimated by `tone` */
               v->mix_noise = 1.0f, v->env_noise = 1.0f;
               v->hold_noise = (int)(0.012f * D * sr), v->c_noise = t60c(e, 0.004f);
               v->sh_period = 1 + (int)(tn * 12.0f);
               svf_set_mode(&v->f1, SVF_LP), svf_set(&v->f1, 18000.0f, 0.7f);
}

static void glitch_clap(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    /* digital click: one sample + a 3 ms 8 kHz blip */
               v->env_click = 1.0f, v->c_click = 0.0f, v->mix_click = 0.3f;
               v->f[0] = 8000.0f * m, v->mix_tone = 0.2f, v->env_tone = 1.0f, v->c_tone = t60c(e, 0.003f * D);
}

static void glitch_rim(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float sr = e->sr;
    /* square-gated 3 kHz beep */
               v->f[0] = 3000.0f * m, v->mix_tone = 0.6f, v->env_tone = 1.0f;
               v->hold_tone = (int)(0.015f * D * sr), v->c_tone = t60c(e, 0.003f);
}

/* The closed hat chokes a ringing open hat (every kit). */
static void choke_open_hat(drums_t *e, int vi)
{
    for (int j = 0; j < DRUMS_VOICES; j++)
        if (j != vi && vpoly_active(&e->poly, j) && e->v[j].inst == DR_OHAT)
            e->v[j].c_metal = e->v[j].c_metal2 = e->v[j].c_noise = t60c(e, 0.01f);
}

static void glitch_chat(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float D = c->D;
    const float tn = c->tn;
    /* micro tick: impulse + 1.5 ms of high-passed noise */
               v->env_click = 1.0f, v->c_click = 0.0f, v->mix_click = 0.7f;
               v->mix_noise = 0.8f, v->env_noise = 1.0f, v->c_noise = t60c(e, 0.0015f * D);
               svf_set_mode(&v->f1, SVF_HP), svf_set(&v->f1, 8000.0f * fm_exp2(tn - 0.5f), 0.7f);
    choke_open_hat(e, (int)(v - e->v));
}

static void glitch_ohat(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float sr = e->sr;
    /* piercing pure high beep */
               v->f[0] = 12000.0f * m, v->mix_tone = 0.8f, v->env_tone = 1.0f;
               v->hold_tone = (int)(0.06f * D * sr), v->c_tone = t60c(e, 0.004f);
}

static void glitch_tom(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const float sr = e->sr;
    const int inst = c->inst;
    v->f[0] = tone_f[inst - DR_LTOM] * m, v->mix_tone = 0.6f, v->env_tone = 1.0f;
    v->hold_tone = (int)(0.04f * D * sr), v->c_tone = t60c(e, 0.003f);
    v->shape = 6.0f * tn;
}

static void glitch_cymbal(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float D = c->D;
    const float tn = c->tn;
    const float sr = e->sr;
    /* data noise: decimated, 2-bit, gated */
               v->mix_noise = 0.7f, v->env_noise = 1.0f;
               v->hold_noise = (int)(0.15f * D * sr), v->c_noise = t60c(e, 0.01f);
               v->sh_period = 3 + (int)(tn * 20.0f), v->qsteps = 2.0f;
               svf_set_mode(&v->f1, SVF_LP), svf_set(&v->f1, 18000.0f, 0.7f);
}

static void glitch_cowbell(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float sr = e->sr;
    /* modem chirp: ring-modulated sines sweeping down */
               v->f[0] = 1200.0f * m, v->f[1] = 1730.0f * m, v->ring = 1, v->mix_tone = 2.2f, v->env_tone = 1.0f;
               v->pe_a = 1.0f, v->pe_a_st = 12.0f, v->pe_a_coef = t60c_block(e, 0.05f);
               v->hold_tone = (int)(0.05f * D * sr), v->c_tone = t60c(e, 0.005f);
}

static void glitch_clave(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    v->f[0] = 60.0f * m, v->mix_tone = 1.0f, v->env_tone = 1.0f, v->c_tone = t60c(e, 0.03f * D);
    v->env_click = 1.0f, v->c_click = 0.0f, v->mix_click = 0.5f;
}

static void setup_kick(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    v->mix_tone = 1.0f;
    v->env_tone = 1.0f;
    if (!hy) { /* resonant sine with a small dip in pitch, long boom, soft click */
        v->f[0] = 49.0f * m;
        v->pe_a = 1.0f, v->pe_a_st = 4.0f, v->pe_a_coef = t60c_block(e, 0.03f);
        v->c_tone = t60c(e, 1.6f * D);
        v->shape = 1.2f;
        v->env_click = 1.0f, v->c_click = t60c(e, 0.004f), v->mix_click = 0.5f * tn;
    } else { /* deep fast sweep + slow settle, waveshaped, beater click */
        v->f[0] = 52.0f * m;
        v->pe_a = 1.0f, v->pe_a_st = 30.0f, v->pe_a_coef = t60c_block(e, 0.06f);
        v->pe_b = 1.0f, v->pe_b_st = 5.0f, v->pe_b_coef = t60c_block(e, 0.3f);
        v->c_tone = t60c(e, 0.7f * D);
        v->shape = 2.2f;
        v->env_noise = 1.0f, v->c_noise = t60c(e, 0.012f), v->mix_noise = 0.6f * tn;
        svf_set_mode(&v->f1, SVF_HP);
        svf_set(&v->f1, 2500.0f, 0.7f);
        v->env_click = 1.0f, v->c_click = t60c(e, 0.002f), v->mix_click = 0.4f * tn;
    }
}

static void setup_snare(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    v->f[0] = (hy ? 190.0f : 185.0f) * m;
    v->f[1] = (hy ? 340.0f : 330.0f) * m;
    v->mix_tone2 = 0.6f;
    v->pe_a = 1.0f, v->pe_a_st = hy ? 5.0f : 2.0f, v->pe_a_coef = t60c_block(e, hy ? 0.035f : 0.02f);
    v->env_tone = 1.0f, v->c_tone = t60c(e, (hy ? 0.14f : 0.18f) * D);
    v->mix_tone = (1.1f - 0.6f * tn) * 0.7f;
    v->shape = hy ? 0.7f : 0.0f; /* light saturation only: rounder body */
    v->env_noise = 1.0f, v->c_noise = t60c(e, (hy ? 0.3f : 0.22f) * D);
    v->mix_noise = (0.3f + 0.9f * tn) * 1.5f; /* filtered twice: needs more level */
    /* wires: band-limited noise (high-pass + low-pass) with a 1 ms attack, not a raw white burst */
    svf_set_mode(&v->f1, SVF_HP);
    svf_set(&v->f1, hy ? 900.0f : 1500.0f, 0.6f);
    svf_set_mode(&v->f2, SVF_LP);
    svf_set(&v->f2, (hy ? 9000.0f : 7000.0f) * fm_exp2(0.5f * (tn - 0.5f)), 0.6f);
    v->noise_f2 = 1;
    v->noise_att = 0.0f, v->noise_att_step = 1.0f / (0.001f * e->sr);
}

static void setup_clap(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    /* hands: band-passed noise, then a high-pass that removes the boxy low-mids */
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, (hy ? 1300.0f : 1150.0f) * m * fm_exp2(tn - 0.5f), 1.1f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, 600.0f * m, 0.7f);
    v->noise_f2 = 1;
    v->mix_noise = 1.0f;
    v->burst_left = hy ? 4 : 3;
    v->burst_period = (int)((hy ? 0.008f : 0.010f) * e->sr);
    v->burst_timer = 0; /* first burst now */
    v->c_burst = t60c(e, 0.03f);
    v->c_noise = t60c(e, (hy ? 0.32f : 0.25f) * D);
}

static void setup_rim(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    if (!hy) {
        reso_init(e, &v->reso[0], 1700.0f * m, 0.035f * D, 1.0f);
        reso_init(e, &v->reso[1], 480.0f * m, 0.06f * D, 0.6f);
    } else {
        reso_init(e, &v->reso[0], 1600.0f * m, 0.03f * D, 1.0f);
        v->env_noise = 1.0f, v->c_noise = t60c(e, 0.012f), v->mix_noise = 0.5f;
        svf_set_mode(&v->f1, SVF_HP);
        svf_set(&v->f1, 2000.0f, 0.7f);
    }
    v->env_click = 1.0f, v->c_click = t60c(e, 0.002f), v->mix_click = 0.6f * tn;
}

static void setup_hat(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    const int inst = c->inst;
    const int vi = c->vi;
    const int open = inst == DR_OHAT;
    metal_init(e, v, metal6, 6, m);
    v->mix_metal = hy ? 1.0f : 2.5f; /* the narrow analog band-pass passes less energy */
    v->env_metal = 1.0f;
    v->c_metal = t60c(e, (open ? (hy ? 0.75f : 0.6f) : (hy ? 0.08f : 0.07f)) * D);
    v->metal_noise = hy ? 0.5f : 0.0f;
    v->qsteps = hy ? 32.0f : 0.0f;
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, (hy ? 9000.0f : 7500.0f) * fm_exp2(tn - 0.5f), hy ? 0.9f : 1.2f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, hy ? 7000.0f : 6000.0f, 0.7f);
    if (!open)
        choke_open_hat(e, vi);
}

static void setup_tom(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    const int inst = c->inst;
    const int k = inst - DR_LTOM;
    v->f[0] = (hy ? tom_h[k] : tom_a[k]) * m;
    v->pe_a = 1.0f, v->pe_a_st = hy ? 12.0f : 6.0f, v->pe_a_coef = t60c_block(e, hy ? 0.08f : 0.12f);
    v->mix_tone = 1.0f;
    v->env_tone = 1.0f, v->c_tone = t60c(e, (hy ? 0.5f - 0.05f * (float)k : 0.45f - 0.05f * (float)k) * D);
    v->env_noise = 1.0f, v->c_noise = t60c(e, 0.08f), v->mix_noise = (hy ? 0.3f : 0.15f) * tn;
    v->shape = hy ? 1.3f : 0.0f;
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, 2000.0f * m, 0.6f);
}

static void setup_cymbal(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    const int hy = c->hy;
    metal_init(e, v, metal6, 6, m);
    v->mix_metal = hy ? 1.0f : 1.6f;
    v->env_metal = 1.0f, v->c_metal = t60c(e, (hy ? 1.4f : 1.6f) * D);
    v->metal_noise = hy ? 0.7f : 0.3f;
    v->qsteps = hy ? 32.0f : 0.0f;
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, 5000.0f * fm_exp2(tn - 0.5f), 0.8f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, 3500.0f, 0.7f);
}

static void setup_cowbell(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const float tn = c->tn;
    metal_init(e, v, bell2, 2, m);
    v->mix_metal = 1.0f;
    v->env_metal = 0.65f, v->c_metal = t60c(e, 0.05f);            /* sharp attack stage */
    v->env_metal2 = 0.35f, v->c_metal2 = t60c(e, 0.35f * D);     /* ringing stage */
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, 1800.0f * m * fm_exp2(tn - 0.5f), 1.2f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, 250.0f, 0.7f);
}

static void setup_clave(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    const float m = c->m;
    const float D = c->D;
    const int hy = c->hy;
    reso_init(e, &v->reso[0], (hy ? 2700.0f : 2500.0f) * m, 0.06f * D, 1.0f);
}

/* ---------------- trap kit and the melodic bass drum ---------------- */

/* The bass drum: a sine at the note's pitch with a short downward punch, a held body then a long decay, driven
 * through tanh (tone), a soft click. Slides from the previous one when it is still held (set in voice_start). */
static void setup_bass(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    v->f[0] = fm_mtof((float)c->note) * c->m;
    v->mix_tone = 1.0f;
    v->env_tone = 1.0f;
    v->hold_tone = (int)(0.06f * e->sr);
    v->c_tone = t60c(e, 2.0f * c->D);
    v->pe_a = 1.0f, v->pe_a_st = 7.0f, v->pe_a_coef = t60c_block(e, 0.03f);
    v->shape = 0.4f + 4.0f * c->tn;
    v->env_click = 1.0f, v->c_click = t60c(e, 0.003f), v->mix_click = 0.25f;
}

static void trap_kick(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    /* short and punchy: a deep, fast sweep, a tight body, saturation and a hard click on top */
    v->f[0] = 55.0f * c->m;
    v->mix_tone = 1.0f, v->env_tone = 1.0f;
    v->pe_a = 1.0f, v->pe_a_st = 26.0f, v->pe_a_coef = t60c_block(e, 0.045f);
    v->c_tone = t60c(e, 0.45f * c->D);
    v->shape = 2.6f;
    v->env_noise = 1.0f, v->c_noise = t60c(e, 0.008f), v->mix_noise = 0.5f * c->tn;
    svf_set_mode(&v->f1, SVF_HP);
    svf_set(&v->f1, 3500.0f, 0.7f);
    v->env_click = 1.0f, v->c_click = t60c(e, 0.0015f), v->mix_click = 0.7f * c->tn;
}

static void trap_snare(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    /* a tight crack: a short, high body and bright, quickly decaying wires */
    v->f[0] = 205.0f * c->m, v->f[1] = 370.0f * c->m;
    v->mix_tone2 = 0.5f;
    v->pe_a = 1.0f, v->pe_a_st = 6.0f, v->pe_a_coef = t60c_block(e, 0.02f);
    v->env_tone = 1.0f, v->c_tone = t60c(e, 0.09f * c->D);
    v->mix_tone = 0.55f;
    v->shape = 1.2f;
    v->env_noise = 1.0f, v->c_noise = t60c(e, 0.17f * c->D);
    v->mix_noise = (0.6f + 1.0f * c->tn) * 1.6f;
    svf_set_mode(&v->f1, SVF_HP);
    svf_set(&v->f1, 1800.0f, 0.6f);
    svf_set_mode(&v->f2, SVF_LP);
    svf_set(&v->f2, 11000.0f * fm_exp2(0.5f * (c->tn - 0.5f)), 0.6f);
    v->noise_f2 = 1;
    v->noise_att = 0.0f, v->noise_att_step = 1.0f / (0.0005f * e->sr);
    v->env_click = 1.0f, v->c_click = t60c(e, 0.001f), v->mix_click = 0.4f;
}

static void trap_clap(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    /* several hands at once: dense bursts, a bright band, a roomy tail */
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, 1600.0f * c->m * fm_exp2(c->tn - 0.5f), 0.9f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, 900.0f * c->m, 0.7f);
    v->noise_f2 = 1;
    v->mix_noise = 1.1f;
    v->burst_left = 5;
    v->burst_period = (int)(0.006f * e->sr);
    v->burst_timer = 0;
    v->c_burst = t60c(e, 0.025f);
    v->c_noise = t60c(e, 0.38f * c->D);
}

static void trap_hat(drums_t *e, drum_voice_t *v, const drum_setup_t *c)
{
    /* crisp and bright, made for fast rolls: metal with noise high up, a very short closed hat */
    const int open = c->inst == DR_OHAT;
    metal_init(e, v, metal6, 6, c->m * 1.15f);
    v->mix_metal = 1.4f;
    v->env_metal = 1.0f;
    v->c_metal = t60c(e, (open ? 0.5f : 0.045f) * c->D);
    v->metal_noise = 0.8f;
    svf_set_mode(&v->f1, SVF_BP);
    svf_set(&v->f1, 10500.0f * fm_exp2(c->tn - 0.5f), 0.8f);
    svf_set_mode(&v->f2, SVF_HP);
    svf_set(&v->f2, 8000.0f, 0.7f);
    if (!open)
        choke_open_hat(e, c->vi);
}

static const drum_recipe_t recipes[DR_COUNT] = {
    [DR_KICK] = setup_kick,
    [DR_SNARE] = setup_snare,
    [DR_CLAP] = setup_clap,
    [DR_RIM] = setup_rim,
    [DR_CHAT] = setup_hat,
    [DR_OHAT] = setup_hat,
    [DR_LTOM] = setup_tom,
    [DR_MTOM] = setup_tom,
    [DR_HTOM] = setup_tom,
    [DR_CYMBAL] = setup_cymbal,
    [DR_COWBELL] = setup_cowbell,
    [DR_CLAVE] = setup_clave,
    [DR_BASS] = setup_bass,
};
static const drum_recipe_t glitch_recipes[DR_COUNT] = {
    [DR_KICK] = glitch_kick,
    [DR_SNARE] = glitch_snare,
    [DR_CLAP] = glitch_clap,
    [DR_RIM] = glitch_rim,
    [DR_CHAT] = glitch_chat,
    [DR_OHAT] = glitch_ohat,
    [DR_LTOM] = glitch_tom,
    [DR_MTOM] = glitch_tom,
    [DR_HTOM] = glitch_tom,
    [DR_CYMBAL] = glitch_cymbal,
    [DR_COWBELL] = glitch_cowbell,
    [DR_CLAVE] = glitch_clave,
    [DR_BASS] = setup_bass,
};
/* trap: its own kick, snare, clap and hats; the rest from the hybrid kit */
static const drum_recipe_t trap_recipes[DR_COUNT] = {
    [DR_KICK] = trap_kick,
    [DR_SNARE] = trap_snare,
    [DR_CLAP] = trap_clap,
    [DR_RIM] = setup_rim,
    [DR_CHAT] = trap_hat,
    [DR_OHAT] = trap_hat,
    [DR_LTOM] = setup_tom,
    [DR_MTOM] = setup_tom,
    [DR_HTOM] = setup_tom,
    [DR_CYMBAL] = setup_cymbal,
    [DR_COWBELL] = setup_cowbell,
    [DR_CLAVE] = setup_clave,
    [DR_BASS] = setup_bass,
};
static const drum_recipe_t *const kits[4] = {recipes, recipes, glitch_recipes, trap_recipes};

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    drums_t *e = eng;
    drum_voice_t *v = &e->v[vi];
    const int inst = drums_instrument_for_note(note);
    (void)hard;
    clear_parts(v);
    v->inst = inst;
    v->level = 0.0f;
    if (inst < 0)
        return; /* not a drum note: the voice ends at the end of the block */

    const float *p = e->p;
    const int kit = dsp_clampi((int)p[DRUMS_P_KIT], 0, 3);
    v->kit = kit;
    const float acc = p[DRUMS_P_ACCENT];
    vel = dsp_clampf(vel, 0.0f, 1.0f);
    drum_setup_t c;
    c.inst = inst, c.vi = vi, c.hy = kit == 1 || kit == 3, c.note = note;
    c.m = fm_exp2(p[DRUMS_P(inst, DRUMS_TUNE)] * (1.0f / 12.0f));
    if (e->poly.vm[vi].tune != 0.0f) /* microtuning, at the hit (one-shots) */
        c.m *= fm_exp2(e->poly.vm[vi].tune * (1.0f / 12.0f));
    c.D = p[DRUMS_P(inst, DRUMS_DECAY)] * (1.0f + 0.25f * acc * (vel - 0.6f));
    c.tn = dsp_clampf(p[DRUMS_P(inst, DRUMS_TONE)] + 0.2f * acc * (vel - 0.6f), 0.0f, 1.0f);
    v->gain = inst_gain[inst] * fm_db2lin(p[DRUMS_P(inst, DRUMS_LEVEL)] + p[DRUMS_P_VOLUME]) * (1.0f - acc * (1.0f - vel));
    const float pan = inst_pan[inst] * p[DRUMS_P_STEREO];
    voice_pan(pan, &v->pan_l, &v->pan_r);
    v->rng = dsp_seed((uint32_t)note * 977u + (uint32_t)vi + e->rng);
    dsp_rand_u32(&e->rng);
    svf_init(&v->f1, e->sr);
    svf_init(&v->f2, e->sr);

    kits[kit][inst](e, v, &c);
    v->held = 1;
    if (inst == DR_BASS) { /* one bass drum at a time: the previous one fades fast, and slides into this one if held */
        const float pitch = (float)note + p[DRUMS_P(DR_BASS, DRUMS_TUNE)];
        int slide = 0;
        for (int j = 0; j < DRUMS_VOICES; j++) {
            drum_voice_t *o = &e->v[j];
            if (j == vi || o->inst != DR_BASS || !vpoly_active(&e->poly, j))
                continue;
            slide |= o->held && o->env_tone > 0.01f;
            o->c_tone = t60c(e, 0.012f), o->hold_tone = 0;
        }
        if (slide && p[DRUMS_P_GLIDE] > 0.0f) {
            v->pe_b = 1.0f, v->pe_b_st = e->bass_pitch - pitch, v->pe_b_coef = t60c_block(e, p[DRUMS_P_GLIDE]);
            v->pe_a = 0.0f, v->env_click = 0.0f; /* a slide has no new punch */
        }
        e->bass_pitch = pitch;
    }
    if (v->shape > 0.0f)
        v->shape_norm = 1.0f / fm_tanh(v->shape);
    v->level = 1.0f;
}

static void voice_release(void *eng, int vi) { ((drums_t *)eng)->v[vi].held = 0; } /* one-shots ring on; the key is up */
static float voice_level(const void *eng, int vi) { return ((const drums_t *)eng)->v[vi].level; }
static void voice_silence(void *eng, int vi)
{
    drum_voice_t *v = &((drums_t *)eng)->v[vi];
    clear_parts(v);
    v->level = 0.0f;
}

static const vpoly_ops_t drums_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- rendering ---------------- */

static DSP_FAST_CODE void voice_render(drums_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    drum_voice_t *v = &e->v[vi];
    float y[SYNTH_BLOCK], t[SYNTH_BLOCK], g[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        y[i] = 0.0f;

    if (v->inst >= 0) {
        /* tone: pitch envelopes per block, frequency interpolated per sample */
        if (v->mix_tone > 0.0f && v->env_tone > 1e-5f) {
            const float st0 = v->pe_a_st * v->pe_a + v->pe_b_st * v->pe_b;
            v->pe_a *= v->pe_a_coef;
            v->pe_b *= v->pe_b_coef;
            const float st1 = v->pe_a_st * v->pe_a + v->pe_b_st * v->pe_b;
            const float k0 = fm_exp2(st0 * (1.0f / 12.0f)) / e->sr, k1 = fm_exp2(st1 * (1.0f / 12.0f)) / e->sr;
            if (v->ring) { /* osc 1 x osc 2 */
                float inc0 = v->f[0] * k0, inc1 = v->f[1] * k0;
                const float d0 = (v->f[0] * k1 - inc0) / (float)n, d1 = (v->f[1] * k1 - inc1) / (float)n;
                for (int i = 0; i < n; i++) {
                    inc0 += d0, inc1 += d1;
                    v->ph[0] += inc0, v->ph[1] += inc1;
                    if (v->ph[0] >= 1.0f)
                        v->ph[0] -= 1.0f;
                    if (v->ph[1] >= 1.0f)
                        v->ph[1] -= 1.0f;
                    if (v->hold_tone > 0)
                        v->hold_tone--;
                    else
                        v->env_tone *= v->c_tone;
                    y[i] += fm_sin(v->ph[0]) * fm_sin(v->ph[1]) * v->env_tone * v->mix_tone;
                }
            } else {
                const int hold0 = v->hold_tone;
                for (int o = 0; o < 2; o++) {
                    if (v->f[o] <= 0.0f)
                        continue;
                    const float w = o ? v->mix_tone2 : 1.0f;
                    float inc = v->f[o] * k0;
                    const float dinc = (v->f[o] * k1 - inc) / (float)n;
                    float ph = v->ph[o], env = v->env_tone;
                    int hold = hold0;
                    for (int i = 0; i < n; i++) {
                        inc += dinc;
                        ph += inc;
                        if (ph >= 1.0f)
                            ph -= 1.0f;
                        float s = fm_sin(ph);
                        if (v->shape > 0.0f)
                            s = fm_tanh(s * v->shape) * v->shape_norm;
                        if (hold > 0)
                            hold--;
                        else
                            env *= v->c_tone;
                        y[i] += s * env * v->mix_tone * w;
                    }
                    v->ph[o] = ph;
                    if (o == 1 || v->f[1] <= 0.0f) {
                        v->env_tone = env;
                        v->hold_tone = hold;
                    }
                }
            }
        }
        /* noise (snare wires, beater, clap bursts and tail) */
        if (v->mix_noise > 0.0f && (v->env_noise > 1e-5f || v->burst_left > 0 || v->env_burst > 1e-5f)) {
            for (int i = 0; i < n; i++) {
                if (v->burst_left > 0 && --v->burst_timer <= 0) {
                    v->env_burst = 0.8f + 0.2f * dsp_rand_unipolar(&v->rng);
                    /* irregular spacing: hands never clap at exactly regular intervals */
                    v->burst_timer = (int)((float)v->burst_period * (0.7f + 0.6f * dsp_rand_unipolar(&v->rng)));
                    if (--v->burst_left == 0)
                        v->env_noise = 0.7f; /* the tail starts with the last burst */
                }
                v->env_burst *= v->c_burst;
                if (v->hold_noise > 0)
                    v->hold_noise--;
                else
                    v->env_noise *= v->c_noise;
                v->noise_att = dsp_minf(1.0f, v->noise_att + v->noise_att_step);
                if (--v->sh_count <= 0) { /* sample-and-hold: sample-rate reduction */
                    v->sh_count = v->sh_period;
                    v->sh_val = dsp_rand_bipolar(&v->rng);
                    if (v->qsteps > 0.0f)
                        v->sh_val = (float)(int32_t)(v->sh_val * v->qsteps + (v->sh_val >= 0.0f ? 0.5f : -0.5f)) / v->qsteps;
                }
                t[i] = v->sh_val;
                g[i] = (v->env_noise + v->env_burst) * v->mix_noise * v->noise_att;
            }
            svf_process(&v->f1, t, t, n);
            if (v->noise_f2)
                svf_process(&v->f2, t, t, n);
            for (int i = 0; i < n; i++)
                y[i] += t[i] * g[i];
        }
        /* metal: detuned squares (+ noise), 6-bit grit, band-pass then high-pass */
        if (v->mix_metal > 0.0f && v->env_metal + v->env_metal2 > 1e-5f) {
            for (int i = 0; i < n; i++)
                t[i] = 0.0f;
            for (int k = 0; k < v->metal_n; k++) {
                osc_process(&v->metal[k], g, n);
                for (int i = 0; i < n; i++)
                    t[i] += g[i];
            }
            const float norm = 1.0f / (float)v->metal_n;
            for (int i = 0; i < n; i++) {
                float x = t[i] * norm;
                if (v->metal_noise > 0.0f)
                    x = x * (1.0f - 0.5f * v->metal_noise) + v->metal_noise * dsp_rand_bipolar(&v->rng);
                if (v->qsteps > 0.0f)
                    x = (float)(int32_t)(x * v->qsteps + (x >= 0.0f ? 0.5f : -0.5f)) / v->qsteps;
                t[i] = x;
            }
            svf_process(&v->f1, t, t, n);
            svf_process(&v->f2, t, t, n);
            for (int i = 0; i < n; i++) {
                v->env_metal *= v->c_metal;
                v->env_metal2 *= v->c_metal2;
                y[i] += t[i] * (v->env_metal + v->env_metal2) * v->mix_metal * 3.0f; /* filters remove a lot */
            }
        }
        /* click and resonators */
        if (v->mix_click > 0.0f && v->env_click > 1e-5f)
            for (int i = 0; i < n; i++) {
                y[i] += v->env_click * v->mix_click; /* add first: c_click = 0 gives a single-sample click */
                v->env_click *= v->c_click;
            }
        for (int r = 0; r < 2; r++)
            if (reso_energy(&v->reso[r]) >= 1e-10f)
                reso_ring(&v->reso[r], y, n);
    }

    voice_out(&e->poly, vi, y, n, v->gain * v->pan_l, v->gain * v->pan_r, L, R, &e->nan_count);

    /* rough remaining amplitude: the voice ends at -80 dB */
    float lvl = v->env_tone * v->mix_tone + (v->env_noise + v->env_burst) * v->mix_noise +
                (v->env_metal + v->env_metal2) * v->mix_metal + v->env_click * v->mix_click;
    for (int r = 0; r < 2; r++)
        lvl += dsp_absf(v->reso[r].zr) + dsp_absf(v->reso[r].zi);
    v->level = v->inst >= 0 ? lvl : 0.0f;
    vpoly_end_block(&e->poly, vi, v->level > 1e-4f || v->burst_left > 0 || v->hold_tone > 0 || v->hold_noise > 0);
}

/* ---------------- engine_ops ---------------- */

static void drums_init(void *st, float sample_rate)
{
    drums_t *e = st;
    e->sr = sample_rate;
    for (int i = 0; i < DRUMS_P_COUNT; i++)
        e->p[i] = drums_params[i].def;
    e->rng = dsp_seed(0xD5u);
    e->nan_count = 0;
    vpoly_init(&e->poly, DRUMS_VOICES, sample_rate, &drums_poly_ops, e);
    for (int i = 0; i < DRUMS_VOICES; i++) {
        clear_parts(&e->v[i]);
        e->v[i].inst = -1;
        e->v[i].level = 0.0f;
        e->v[i].gain = 0.0f;
        e->v[i].pan_l = e->v[i].pan_r = 0.70710678f;
    }
}

static vpoly_t *drums_poly(void *st) { return &((drums_t *)st)->poly; }

static void drums_set_param(void *st, int id, float norm)
{
    drums_t *e = st;
    if (id >= 0 && id < DRUMS_P_COUNT)
        e->p[id] = param_from_norm(&drums_params[id], norm);
}

static void drums_pitch_bend(void *st, float bend) { (void)st, (void)bend; }

static DSP_FAST_CODE void drums_render(void *st, float *L, float *R)
{
    drums_t *e = st;
    for (int vi = 0; vi < DRUMS_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
    const float drive = e->p[DRUMS_P_DRIVE];
    if (drive > 0.0f) { /* kit saturation: unity small-signal gain, rounded peaks */
        const float gd = 1.0f + 4.0f * drive, inv = (1.0f + 0.5f * drive) / gd;
        for (int i = 0; i < SYNTH_BLOCK; i++) {
            L[i] = fm_tanh(L[i] * gd) * inv;
            R[i] = fm_tanh(R[i] * gd) * inv;
        }
    }
}

static const engine_note_t drums_note_map[DR_COUNT] = {
    {36, "kick"}, {38, "snare"}, {39, "clap"}, {37, "rim"}, {42, "chat"}, {46, "ohat"},
    {41, "ltom"}, {45, "mtom"}, {48, "htom"}, {49, "cymbal"}, {56, "cowbell"}, {75, "clave"}, {29, "bass"},
};

const engine_ops_t drums_engine = {
    .name = "drums",
    .description = "drum machine, 12 instruments on GM notes plus a melodic bass drum (36 kick, 37 rim, 38 snare, 39 clap, 42 closed hat, 46 open "
                       "hat, 41/45/48 toms, 49 cymbal, 56 cowbell, 75 clave); kits: analog (early-80s analog machine) and "
                       "hybrid (mid-80s analog/sample machine), glitch, trap; under the map (notes 0..34) a melodic bass drum that "
                       "slides between held notes; per-instrument level, tune, decay, tone",
    .params = drums_params,
    .param_count = DRUMS_P_COUNT,
    .voices = DRUMS_VOICES,
    .init = drums_init,
    .poly = drums_poly,
    .set_param = drums_set_param,
    .pitch_bend = drums_pitch_bend,
    .mod_wheel = NULL,
    .notes = drums_note_map,
    .note_count = DR_COUNT,
    .render = drums_render,
};
