/* sampler.c — the drum sampler (sampler.h). Per voice: the band-limited player (osc/splay.h) and the fading
 * tail of the previous hit, a 1 ms linear attack (start points inside a sound do not click), an exponential
 * decay to -60 dB in the pad's decay time, a TPT state-variable low-pass (filter/svf_tpt.h), constant-power
 * pan. Speed, gain and cutoff are computed per block and ramped across it; a choke group cuts the other pads
 * of the group in 5 ms. A retriggered pad hands its sound to the tail, which fades out in 3 ms, and starts
 * again from its start point: hits overlap without clicks. */
#include "sampler.h"

#include "../core/fastmath.h"

#define SAMPLER_VOICE_GAIN 0.71f /* a full-scale sample peaks at -6 dBFS once panned to the centre */
#define SAMPLER_ATTACK_S 0.001f
#define SAMPLER_TAIL_S 0.003f
#define SAMPLER_CHOKE_S 0.005f
#define SAMPLER_SILENT 1e-4f  /* -80 dB: the voice is done */

#define PAD_PARAMS(k, n)                                                                                         \
    PARAM(SAMPLER_P(k, SAMPLER_TUNE), "pad" #n "_tune", "pad" #n ".tune", -24.0f, 24.0f, 0.0f, PARAM_LIN, "st", 1,  \
          NULL, "playback speed in semitones: +12 plays the sample twice as fast, an octave up"),                \
    PARAM(SAMPLER_P(k, SAMPLER_START), "pad" #n "_start", "pad" #n ".start", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, \
          NULL, "start point, as a fraction of the sample"),                                                     \
    PARAM(SAMPLER_P(k, SAMPLER_END), "pad" #n "_end", "pad" #n ".end", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,  \
          "end point, as a fraction of the sample (a cut fades out in 1.3 ms)"),                                 \
    PARAM(SAMPLER_P(k, SAMPLER_REVERSE), "pad" #n "_reverse", "pad" #n ".reverse", 0.0f, 1.0f, 0.0f, PARAM_INT,   \
          "", 0, param_onoff_labels, "plays from the end point back to the start point"),                       \
    PARAM(SAMPLER_P(k, SAMPLER_LOOP), "pad" #n "_loop", "pad" #n ".loop", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0,     \
          param_onoff_labels, "loops between the start and end points while the note is held"),                 \
    PARAM(SAMPLER_P(k, SAMPLER_DECAY), "pad" #n "_decay", "pad" #n ".decay", 0.01f, 30.0f, 30.0f, PARAM_EXP, "s", \
          1, NULL, "time to fall by 60 dB (30 s: the sample's own envelope)"),                                    \
    PARAM(SAMPLER_P(k, SAMPLER_LEVEL), "pad" #n "_level", "pad" #n ".level", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB",  \
          1, NULL, "level of the pad"),                                                                          \
    PARAM(SAMPLER_P(k, SAMPLER_PAN), "pad" #n "_pan", "pad" #n ".pan", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, \
          "stereo position, -1 left .. 1 right"),                                                                \
    PARAM(SAMPLER_P(k, SAMPLER_CUTOFF), "pad" #n "_cutoff", "pad" #n ".cutoff", 20.0f, 20000.0f, 20000.0f,        \
          PARAM_EXP, "Hz", 1, NULL, "low-pass cutoff (20 kHz: open)"),                                           \
    PARAM(SAMPLER_P(k, SAMPLER_CHOKE), "pad" #n "_choke", "pad" #n ".choke", 0.0f, 8.0f, 0.0f, PARAM_INT, "", 0,  \
          NULL, "choke group 1..8: a pad stops the others of its group (closed hi-hat over open); 0 none")

const param_desc_t sampler_params[SAMPLER_P_COUNT] = {
    PARAM(SAMPLER_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume"),
    PARAM(SAMPLER_P_TUNE, "tune", "voice.transpose", -12.0f, 12.0f, 0.0f, PARAM_LIN, "st", 1, NULL,
          "speed of every pad, in semitones"),
    PARAM(SAMPLER_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL,
          "how much the velocity changes the level (0: every hit the same)"),
    PARAM(SAMPLER_P_RESONANCE, "resonance", "filter.resonance", 0.5f, 8.0f, 0.707f, PARAM_EXP, "", 1, NULL,
          "resonance (Q) of the pads' low-pass filters"),
    PARAM(SAMPLER_P_RELEASE, "release", "amp.release", 0.005f, 10.0f, 0.2f, PARAM_EXP, "s", 1, NULL,
          "fade of a looping pad after its note-off, to -60 dB"),
    PAD_PARAMS(0, 1),   PAD_PARAMS(1, 2),   PAD_PARAMS(2, 3),   PAD_PARAMS(3, 4),
    PAD_PARAMS(4, 5),   PAD_PARAMS(5, 6),   PAD_PARAMS(6, 7),   PAD_PARAMS(7, 8),
    PAD_PARAMS(8, 9),   PAD_PARAMS(9, 10),  PAD_PARAMS(10, 11), PAD_PARAMS(11, 12),
    PAD_PARAMS(12, 13), PAD_PARAMS(13, 14), PAD_PARAMS(14, 15), PAD_PARAMS(15, 16),
};

/* setting k of the pad of kit entry `entry`: entries past the 16 pads use the defaults */
static float pad(const sampler_t *e, int entry, int k)
{
    return entry < SAMPLER_PADS ? e->p[SAMPLER_P(entry, k)] : sampler_params[SAMPLER_P(0, k)].def;
}

/* per-sample multiplier that falls by 60 dB in t seconds */
static float fall_coef(float t, float sr) { return fm_exp2(-9.96578428f / (t * sr)); }

/* ---------------- vpoly callbacks ---------------- */

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    sampler_t *e = eng;
    sampler_voice_t *v = &e->v[vi];
    const int entry = note >= 0 && note < 128 ? e->note_map[note] : -1;
    if (!hard && v->stage != SAMPLER_IDLE && v->play.active) { /* same pad again: the old hit becomes the tail */
        v->tail = v->play;
        v->tail_gain = v->env;
    } else {
        splay_init(&v->tail);
        v->tail_gain = 0.0f;
        svf_reset(&v->lp);
        v->fresh = 1;
    }
    v->entry = entry;
    if (entry < 0) { /* no sample on this note */
        splay_init(&v->play);
        v->stage = v->tail.active ? SAMPLER_DECAY_STAGE : SAMPLER_IDLE;
        v->env = 0.0f;
        return;
    }
    const sampler_sample_t *s = &e->smp[entry];
    splay_start(&v->play, s->data, s->frames, pad(e, entry, SAMPLER_START), pad(e, entry, SAMPLER_END),
                pad(e, entry, SAMPLER_REVERSE) >= 0.5f, pad(e, entry, SAMPLER_LOOP) >= 0.5f);
    const float vs = e->p[SAMPLER_P_VELOCITY];
    v->vel_gain = 1.0f - vs + vs * vel * vel;
    v->env = 0.0f;
    v->stage = SAMPLER_ATTACK;

    /* choke: the other pads of the group stop */
    const int group = (int)(pad(e, entry, SAMPLER_CHOKE) + 0.5f);
    if (group > 0)
        for (int k = 0; k < SAMPLER_VOICES; k++) {
            sampler_voice_t *o = &e->v[k];
            if (k != vi && o->stage != SAMPLER_IDLE && o->entry >= 0 && o->entry != entry &&
                (int)(pad(e, o->entry, SAMPLER_CHOKE) + 0.5f) == group) {
                o->stage = SAMPLER_RELEASE_STAGE;
                o->release_coef = fall_coef(SAMPLER_CHOKE_S, e->sr);
            }
        }
}

static void voice_release(void *eng, int vi)
{
    sampler_t *e = eng;
    sampler_voice_t *v = &e->v[vi];
    if (v->stage == SAMPLER_IDLE || !v->play.loop)
        return; /* one-shot: plays to its end */
    v->stage = SAMPLER_RELEASE_STAGE;
    v->release_coef = fall_coef(e->p[SAMPLER_P_RELEASE], e->sr);
}

static float voice_level(const void *eng, int vi)
{
    const sampler_voice_t *v = &((const sampler_t *)eng)->v[vi];
    return v->env * v->vel_gain;
}

static void voice_silence(void *eng, int vi)
{
    sampler_voice_t *v = &((sampler_t *)eng)->v[vi];
    splay_init(&v->play);
    splay_init(&v->tail);
    v->stage = SAMPLER_IDLE;
    v->env = v->tail_gain = 0.0f;
}

static const vpoly_ops_t sampler_poly_ops = {voice_start, voice_release, voice_level, voice_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void sampler_init(void *st, float sample_rate)
{
    sampler_t *e = st;
    e->sr = sample_rate;
    e->rate_scale = (float)KIT_RATE / sample_rate;
    for (int i = 0; i < SAMPLER_P_COUNT; i++)
        e->p[i] = sampler_params[i].def;
    smooth_init(&e->vol, 0.005f, sample_rate / (float)SYNTH_BLOCK, fm_db2lin(e->p[SAMPLER_P_VOLUME]));
    e->vol_prev = e->vol.y;
    e->bend_semi = 0.0f;
    e->nan_count = 0;
    e->count = 0;
    for (int k = 0; k < 128; k++)
        e->note_map[k] = -1;
    vpoly_init(&e->poly, SAMPLER_VOICES, sample_rate, &sampler_poly_ops, e);
    for (int i = 0; i < SAMPLER_VOICES; i++) {
        sampler_voice_t *v = &e->v[i];
        splay_init(&v->play);
        splay_init(&v->tail);
        svf_init(&v->lp, sample_rate);
        svf_set_mode(&v->lp, SVF_LP);
        v->stage = SAMPLER_IDLE;
        v->entry = -1;
        v->env = v->tail_gain = v->vel_gain = 0.0f;
        v->release_coef = 0.0f;
        v->rate_prev = 1.0f;
        v->gain_prev = v->cut_prev = 0.0f;
        v->fresh = 1;
    }
}

static void sampler_kit(void *st, const uint8_t *kit, size_t bytes)
{
    sampler_t *e = st;
    for (int i = 0; i < SAMPLER_VOICES; i++) /* the previous samples are not read any more */
        voice_silence(e, i);
    vpoly_all_sound_off(&e->poly);
    for (int k = 0; k < 128; k++)
        e->note_map[k] = -1;
    const int n = kit ? kit_count(kit, bytes) : -1;
    e->count = n > 0 ? n : 0;
    for (int k = 0; k < e->count; k++) {
        kit_entry_t en;
        kit_entry(kit, k, &en);
        e->smp[k].data = kit_samples(kit, &en);
        e->smp[k].frames = (int32_t)en.frames;
        if (e->note_map[en.note] < 0) /* two samples on a note: the first one plays */
            e->note_map[en.note] = (int8_t)k;
    }
}

static vpoly_t *sampler_poly(void *st) { return &((sampler_t *)st)->poly; }

static void sampler_set_param(void *st, int id, float norm)
{
    sampler_t *e = st;
    if (id < 0 || id >= SAMPLER_P_COUNT)
        return;
    e->p[id] = param_from_norm(&sampler_params[id], norm);
    if (vpoly_active_count(&e->poly) == 0) { /* silent: the next hit starts at the new volume */
        smooth_snap(&e->vol, fm_db2lin(e->p[SAMPLER_P_VOLUME]));
        e->vol_prev = e->vol.y;
    }
}

static void sampler_pitch_bend(void *st, float bend) { ((sampler_t *)st)->bend_semi = 2.0f * bend; }

static DSP_FAST_CODE void sampler_render(void *st, float *L, float *R)
{
    sampler_t *e = st;
    const int n = SYNTH_BLOCK;
    float x[SYNTH_BLOCK], t[SYNTH_BLOCK];
    const float vol0 = e->vol_prev, vol1 = smooth_next(&e->vol, fm_db2lin(e->p[SAMPLER_P_VOLUME]));
    e->vol_prev = vol1;
    const float attack_step = 1.0f / (SAMPLER_ATTACK_S * e->sr), tail_step = 1.0f / (SAMPLER_TAIL_S * e->sr);
    const float q = e->p[SAMPLER_P_RESONANCE], nyq_cut = 0.45f * e->sr;

    for (int vi = 0; vi < SAMPLER_VOICES; vi++) {
        if (!vpoly_active(&e->poly, vi))
            continue;
        sampler_voice_t *v = &e->v[vi];
        if (v->stage == SAMPLER_IDLE) {
            vpoly_end_block(&e->poly, vi, 0);
            continue;
        }
        const int en = v->entry >= 0 ? v->entry : 0;
        const float *vm = vpoly_vmod(&e->poly, vi);
        const float semi = e->p[SAMPLER_P_TUNE] + pad(e, en, SAMPLER_TUNE) + e->bend_semi + vm[VM_PITCH];
        const float rate = e->rate_scale * fm_exp2(semi * (1.0f / 12.0f));
        const float gain = SAMPLER_VOICE_GAIN * v->vel_gain * fm_db2lin(pad(e, en, SAMPLER_LEVEL));
        float cut = pad(e, en, SAMPLER_CUTOFF) * fm_exp2(vm[VM_BRIGHT]);
        cut = cut > nyq_cut ? nyq_cut : cut;
        if (v->fresh) {
            v->rate_prev = rate;
            v->gain_prev = gain;
            v->cut_prev = cut;
            v->fresh = 0;
        }

        /* the hit, then the tail of the previous one */
        int playing = splay_process(&v->play, x, n, v->rate_prev, rate);
        const float decay = v->stage == SAMPLER_RELEASE_STAGE ? v->release_coef
                                                               : fall_coef(pad(e, en, SAMPLER_DECAY), e->sr);
        float env = v->env;
        int stage = v->stage;
        for (int i = 0; i < n; i++) {
            if (stage == SAMPLER_ATTACK) {
                env += attack_step;
                if (env >= 1.0f) {
                    env = 1.0f;
                    stage = SAMPLER_DECAY_STAGE;
                }
            } else {
                env *= decay;
            }
            x[i] *= env;
        }
        v->env = env;
        v->stage = stage;
        if (v->tail.active || v->tail_gain > 0.0f) {
            splay_process(&v->tail, t, n, v->rate_prev, rate);
            float g = v->tail_gain;
            for (int i = 0; i < n; i++) {
                g = g > tail_step ? g - tail_step : 0.0f;
                x[i] += t[i] * g;
            }
            v->tail_gain = g;
            if (g == 0.0f)
                splay_init(&v->tail);
        }

        /* low-pass (open: skipped), level, pan */
        if (cut < 19000.0f || v->cut_prev < 19000.0f) {
            svf_set(&v->lp, 0.5f * (v->cut_prev + cut), q);
            svf_process(&v->lp, x, x, n);
        }
        dsp_ramp_mul(x, v->gain_prev * vol0, gain * vol1, n);
        float pl, pr;
        voice_pan(pad(e, en, SAMPLER_PAN), &pl, &pr);
        voice_out(&e->poly, vi, x, n, pl, pr, L, R, &e->nan_count);
        v->rate_prev = rate;
        v->gain_prev = gain;
        v->cut_prev = cut;

        playing = (playing && env > SAMPLER_SILENT) || v->tail_gain > 0.0f;
        if (!playing)
            voice_silence(e, vi);
        vpoly_end_block(&e->poly, vi, playing);
    }
}

const engine_ops_t sampler_engine = {
    .name = "sampler",
    .description = "drum sampler: a kit of samples, 16 pads with speed, start/end, reverse, loop, filter, choke (12 voices)",
    .params = sampler_params,
    .param_count = SAMPLER_P_COUNT,
    .voices = SAMPLER_VOICES,
    .init = sampler_init,
    .poly = sampler_poly,
    .set_param = sampler_set_param,
    .pitch_bend = sampler_pitch_bend,
    .mod_wheel = NULL,
    .render = sampler_render,
    .kit = sampler_kit,
};
