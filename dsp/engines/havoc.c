/* havoc.c — noise machine with a wide palette (see havoc.h). The four generators are rendered one after the other,
 * a block each, so a generator reads the block its predecessor has just made as its modulator (the first reads the
 * last one's previous block: the ring closes with 32 samples of delay). Per block: pitch, life, coefficients; per
 * sample: the process itself, written without libm or division. Then the two processors on the stereo sum, a soft
 * bound, a DC blocker and the envelope. */
#include "havoc.h"
#include "../core/fastmath.h"

#define HAVOC_GAIN 0.2f /* a few bounded generators -> about -18 dBFS */
#define HAVOC_MASK (HAVOC_DELAY - 1)

static const char *const type_labels[] = {"tone", "logistic", "byte", "dust", "wind", "pulsar", "cell", "swarm",
                                          "computer", "rumble", "bleep", "glitch", "talk", "modem", "neuron", "alias", NULL};
static const char *const life_labels[] = {"steady", "bursts", "pulse", "swell", NULL};
static const char *const proc_labels[] = {"off", "lp", "bp", "hp", "comb", "fold", "crush", "ring", "chop", "stutter", "sub", "warp", "mangle", NULL};

#define HAVOC_GEN_PARAMS(g, n, dtype, dpitch, dtrack, da, db, dlevel, dpan, drate)                                         \
    PARAM(HAVOC_G_P(g, HAVOC_G_TYPE), "g" n "_type", "havoc.gen" n ".type", 0.0f, (float)(HAVOC_T_COUNT - 1), dtype, \
          PARAM_INT, "", 0, type_labels,                                                                             \
          "process of this generator. tone: a sine, a = phase feedback (sine, saw, noise), b = a second detuned "    \
          "sine. logistic: the logistic map clocked at the pitch, a = from a period-2 buzz to chaos, b = stepped "   \
          "to smooth. byte: an integer formula on a counter (8-bit digital melodies and noise), a = formula, "       \
          "b = its constant. dust: random impulses (the pitch is their number per second) into a resonator, "        \
          "a = its frequency, b = its ring. wind: a band of noise at the pitch, a = from wide to a whistle, "        \
          "b = gusts. pulsar: a train of short sine bursts, a = their frequency (a formant), b = their length. "     \
          "cell: a cellular automaton of 32 cells read as a wave and stepped every period, a = the rule (0..255), "  \
          "b = smoothing. swarm: eight sines wandering around the pitch, a = how far, b = how fast. computer: a "    \
          "tone that jumps to random pitches, a = range, b = speed. rumble: a random walk, a = fold, b = only the "  \
          "peaks pass (crackle). bleep: a chatter of short notes, some gliding, some square, with rests, a = how far "    \
          "they go and glide, b = notes per second. glitch: electronic faults, one after the other (a stuck loop "   \
          "of bits, a burst of bit noise, a whine, a gap), a = from buzzing loops to noise, b = faults per second. "  \
          "talk: a voice speaking no language (vowels, hisses, stops through three formants; the pitch is the "      \
          "voice's), a = how fast it speaks, b = from voiced to whispered. modem: a data transmission (carrier, "     \
          "preambles, bursts of random symbols, gaps), a = symbols per second (20 .. 640), b = 2, 4 or 8 tones. "    \
          "neuron: a model neuron firing (the pitch is its speed), a = from regular spikes to chattering bursts, "   \
          "b = input current and its noise. alias: a staircase saw pushed past the Nyquist frequency, a = how far "  \
          "(1 .. 64 times the pitch), b = from 256 steps to 2"),                                                                                   \
    PARAM(HAVOC_G_P(g, HAVOC_G_PITCH), "g" n "_pitch", "havoc.gen" n ".pitch", -96.0f, 60.0f, dpitch, PARAM_LIN,     \
          "st", 1, NULL,                                                                                             \
          "pitch of this generator, in semitones from the note (or from middle C when it does not track): "          \
          "-96 = 1 Hz, a rhythm; +60 = 8 kHz"),                                                                      \
    PARAM(HAVOC_G_P(g, HAVOC_G_TRACK), "g" n "_track", "havoc.gen" n ".track", 0.0f, 1.0f, dtrack, PARAM_INT, "", 0,  \
          param_onoff_labels, "on: this generator follows the note; off: it stays where its pitch puts it"),               \
    PARAM(HAVOC_G_P(g, HAVOC_G_A), "g" n "_a", "havoc.gen" n ".a", 0.0f, 1.0f, da, PARAM_LIN, "", 1, NULL,            \
          "first shape control of this generator (see its process)"),                                                \
    PARAM(HAVOC_G_P(g, HAVOC_G_B), "g" n "_b", "havoc.gen" n ".b", 0.0f, 1.0f, db, PARAM_LIN, "", 1, NULL,            \
          "second shape control of this generator (see its process)"),                                               \
    PARAM(HAVOC_G_P(g, HAVOC_G_FM), "g" n "_fm", "havoc.gen" n ".fm", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,       \
          "frequency modulation of this generator by the one before it (the first by the last)"),                    \
    PARAM(HAVOC_G_P(g, HAVOC_G_RING), "g" n "_ring", "havoc.gen" n ".ring", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, \
          "ring modulation of this generator by the one before it: at 1 it sounds only when the other does"),        \
    PARAM(HAVOC_G_P(g, HAVOC_G_LIFE), "g" n "_life", "havoc.gen" n ".life", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 0,       \
          life_labels,                                                                                               \
          "how this generator lives in time: steady, bursts (on and off at random), pulse (a periodic decaying "     \
          "hit), swell (a slow random breath)"),                                                                     \
    PARAM(HAVOC_G_P(g, HAVOC_G_LIFE_RATE), "g" n "_life_rate", "havoc.gen" n ".life_rate", 0.05f, 50.0f, 2.0f,        \
          PARAM_EXP, "Hz", 1, NULL, "rate of the bursts, the pulse or the swell"),                                   \
    PARAM(HAVOC_G_P(g, HAVOC_G_LIFE_SHAPE), "g" n "_life_shape", "havoc.gen" n ".life_shape", 0.0f, 1.0f, 0.5f,       \
          PARAM_LIN, "", 1, NULL,                                                                                    \
          "bursts: share of the time it is on; pulse: length of the hit; swell: depth of the breath"),               \
    PARAM(HAVOC_G_P(g, HAVOC_G_LEVEL), "g" n "_level", "havoc.gen" n ".level", 0.0f, 1.0f, dlevel, PARAM_LIN, "", 1,  \
          NULL, "output level of this generator (at 0 it can still modulate the next one)"),                         \
    PARAM(HAVOC_G_P(g, HAVOC_G_PAN), "g" n "_pan", "havoc.gen" n ".pan", -1.0f, 1.0f, dpan, PARAM_LIN, "", 1, NULL,   \
          "place of this generator, left to right"),                                                                 \
    PARAM(HAVOC_G_P(g, HAVOC_G_LFO_RATE), "g" n "_lfo_rate", "havoc.gen" n ".lfo_rate", 0.01f, 30.0f, drate,          \
          PARAM_EXP, "Hz", 1, NULL,                                                                                  \
          "rate of this generator's own oscillator, which moves its two shape controls: a at this rate, b at 0.62 "  \
          "of it, so the pair never repeats; give every generator another rate"),                                    \
    PARAM(HAVOC_G_P(g, HAVOC_G_LFO_A), "g" n "_lfo_a", "havoc.gen" n ".lfo_a", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1,   \
          NULL, "how far the oscillator moves a (0 = not at all)"),                                                  \
    PARAM(HAVOC_G_P(g, HAVOC_G_LFO_B), "g" n "_lfo_b", "havoc.gen" n ".lfo_b", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1,   \
          NULL, "how far the oscillator moves b (0 = not at all)"),                                                  \
    PARAM(HAVOC_G_P(g, HAVOC_G_ROT), "g" n "_rot", "havoc.gen" n ".rot", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,    \
          "this generator resampled at a lower and lower rate and on fewer and fewer bits (down to 270 Hz and "      \
          "1 bit), with no filter: its aliases then go into whatever it modulates")

#define HAVOC_PROC_PARAMS(x, n)                                                                                      \
    PARAM(HAVOC_X_P(x, HAVOC_X_TYPE), "x" n "_type", "havoc.proc" n ".type", 0.0f, (float)(HAVOC_X_COUNT - 1), 0.0f,  \
          PARAM_INT, "", 0, proc_labels,                                                                             \
          "processor on the sum of the generators. lp, bp, hp: a filter, p1 = cutoff (30 Hz .. 17 kHz), p2 = "       \
          "resonance. comb: p1 = its pitch (30 Hz .. 7.7 kHz), p2 = feedback (negative below the middle). fold: "    \
          "a wavefolder, p1 = drive, p2 = asymmetry. crush: p1 = lower sampling rate, p2 = fewer bits. ring: "       \
          "multiplied by a sine, p1 = its frequency (1 Hz .. 8 kHz), p2 = phase between left and right. chop: a "    \
          "periodic gate, p1 = rate (0.5 .. 32 Hz), p2 = share of the time it is open. stutter: now and then the "   \
          "last instants repeat as if stuck, p1 = length of the fragment (2 .. 42 ms), p2 = share of the time "      \
          "stuck. sub: a square one octave below whatever comes in (p1 towards 1: two octaves), at the level of "    \
          "the input, p2 = from dull to buzzing; mix adds it under the sound. warp: frequency modulation of the "     \
          "whole sound by a sine, p1 = its frequency (0.5 Hz .. 1 kHz), p2 = depth. mangle: operations on the bits "  \
          "of every sample, p1 = which (exclusive or, rotation, bits killed, bits reversed), p2 = the mask or amount"),                             \
    PARAM(HAVOC_X_P(x, HAVOC_X_P1), "x" n "_p1", "havoc.proc" n ".p1", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,      \
          "first control of this processor (see its type)"),                                                         \
    PARAM(HAVOC_X_P(x, HAVOC_X_P2), "x" n "_p2", "havoc.proc" n ".p2", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,      \
          "second control of this processor (see its type)"),                                                        \
    PARAM(HAVOC_X_P(x, HAVOC_X_MIX), "x" n "_mix", "havoc.proc" n ".mix", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,   \
          "share of the processed signal")

const param_desc_t havoc_params[HAVOC_P_COUNT] = {
    PARAM(HAVOC_P_ATTACK, "amp_attack", "amp.attack", 0.001f, 60.0f, 0.01f, PARAM_EXP, "s", 1, NULL, "fade-in time"),
    PARAM(HAVOC_P_RELEASE, "amp_release", "amp.release", 0.005f, 60.0f, 0.5f, PARAM_EXP, "s", 1, NULL,
          "fade-out time"),
    PARAM(HAVOC_P_GLIDE, "glide", "voice.glide", 0.001f, 20.0f, 0.05f, PARAM_EXP, "s", 1, NULL,
          "time for the generators that follow the note to slide to a new note played while another is held"),
    PARAM(HAVOC_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "velocity to level: 0 = every note equal, 1 = full range"),
    PARAM(HAVOC_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume"),
    PARAM(HAVOC_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 12.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
    PARAM(HAVOC_P_WIDTH, "width", "drone.width", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "how far the generators are spread to their places (0 = all in the middle)"),
    PARAM(HAVOC_P_FEEDBACK, "feedback", "havoc.feedback", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "the machine's output modulates the frequency of every generator: a little roughens, a lot howls and "
          "collapses into chaos"),
    PARAM(HAVOC_P_MUTATE, "mutate", "havoc.mutate", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "how often the machine changes itself while it plays: 0 = never, 0.5 = about once a second, 1 = thirteen "
          "times a second. A mutation gives one generator, at random, other shapes and another pitch"),
    PARAM(HAVOC_P_MUTATE_DEPTH, "mutate_depth", "havoc.mutate_depth", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "size of the mutations: how far the shapes and the pitch (2 octaves at most) are thrown; above the "
          "middle a mutation can also change the generator's process"),
    PARAM(HAVOC_P_DARK, "dark", "havoc.dark", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "a low-pass on the whole machine, after the processors: 0 = open, 0.5 = 1.6 kHz, 1 = 140 Hz, only the "
          "weight is left"),
    HAVOC_PROC_PARAMS(0, "1"),
    HAVOC_PROC_PARAMS(1, "2"),
    HAVOC_GEN_PARAMS(0, "1", (float)HAVOC_T_TONE, 0.0f, 1.0f, 0.3f, 0.0f, 0.7f, 0.0f, 0.13f),
    HAVOC_GEN_PARAMS(1, "2", (float)HAVOC_T_DUST, -36.0f, 0.0f, 0.6f, 0.7f, 0.0f, -0.5f, 0.21f),
    HAVOC_GEN_PARAMS(2, "3", (float)HAVOC_T_WIND, 12.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.5f, 0.34f),
    HAVOC_GEN_PARAMS(3, "4", (float)HAVOC_T_LOGISTIC, -12.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.55f),
};

/* ---------------- voice ---------------- */

static void voice_clear(havoc_voice_t *v)
{
    for (int g = 0; g < HAVOC_GEN; g++) {
        havoc_gen_t *s = &v->gen[g];
        s->rng = dsp_seed(0xC4A05u + (uint32_t)g);
        s->ph = dsp_rand_unipolar(&s->rng);
        s->ph2 = 0.0f;
        s->s0 = 0.4f; /* a valid point of the logistic map */
        s->s1 = s->s2 = s->s3 = s->ic1 = s->ic2 = 0.0f;
        s->u0 = dsp_rand_u32(&s->rng);
        for (int j = 0; j < HAVOC_SWARM; j++) {
            s->sw_ph[j] = dsp_rand_unipolar(&s->rng);
            s->sw_x[j] = 0.7f * dsp_rand_bipolar(&s->rng);
        }
        s->life_ph = 1.0f; /* the pulse hits at once */
        s->life_env = s->walk = s->jump_ph = 0.0f;
        s->ev_oct = s->ev_sweep = s->ev_gain = 0.0f;
        s->ev_kind = 0;
        s->ev_len = 32;
        s->life_on = 1;
        s->gain = 0.0f;
        s->mut_a = s->mut_b = s->mut_pitch = 0.0f;
        s->rot_ph = s->rot_hold = 0.0f;
        s->tk_f[0] = s->tk_t[0] = 500.0f, s->tk_f[1] = s->tk_t[1] = 1500.0f, s->tk_f[2] = s->tk_t[2] = 2500.0f;
        for (int k = 0; k < 6; k++)
            s->tk_ic[k] = 0.0f;
        s->tk_voiced = 1;
        s->md_left = 0;
        s->mut_type = 0;
        s->lfo = 0.37f * (float)g; /* every generator starts elsewhere in its cycle */
        s->lfo2 = 0.25f + 0.61f * (float)g;
    }
    for (int x = 0; x < HAVOC_PROC; x++) {
        havoc_proc_t *q = &v->proc[x];
        for (int c = 0; c < 2; c++) {
            q->ic1[c] = q->ic2[c] = q->hold[c] = 0.0f;
            for (int i = 0; i < HAVOC_DELAY; i++)
                q->buf[c][i] = 0;
        }
        q->cph = q->ph = q->wait = 0.0f;
        q->stuck = q->pos = 0;
        for (int c = 0; c < 2; c++) {
            q->sub_env[c] = q->sub_lp[c] = 0.0f;
            q->sub_sgn[c] = q->sub_ff[c] = 0;
        }
        q->len = 64;
        q->rng = dsp_seed(0x57A7u + (uint32_t)x);
        q->gate = 1.0f;
        q->wr = 0;
    }
    for (int i = 0; i < SYNTH_BLOCK; i++)
        v->fb[i] = v->ofb[i] = 0.0f;
    v->rng = dsp_seed(0x3A7E1u);
    v->dc_xl = v->dc_yl = v->dc_xr = v->dc_yr = 0.0f;
    v->dk[0] = v->dk[1] = v->dk[2] = v->dk[3] = 0.0f;
    v->env = 0.0f;
    v->gain_prev = 0.0f;
    v->fresh = 1;
}

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    havoc_t *e = eng;
    havoc_voice_t *v = &e->v[vi];
    const float vs = e->p[HAVOC_P_VELOCITY];
    v->vel = 1.0f - vs + vs * vel;
    v->gate = 1;
    v->semi_target = (float)note;
    if (hard) { /* a new machine: every process from its start */
        voice_clear(v);
        v->semi = v->semi_target;
    }
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    havoc_voice_t *v = &((havoc_t *)eng)->v[vi];
    (void)vel;
    v->semi_target = (float)note;
}

static void voice_release(void *eng, int vi) { ((havoc_t *)eng)->v[vi].gate = 0; }
static float voice_level(const void *eng, int vi) { return ((const havoc_t *)eng)->v[vi].env; }
static void voice_silence(void *eng, int vi)
{
    havoc_voice_t *v = &((havoc_t *)eng)->v[vi];
    v->env = 0.0f;
    v->gate = 0;
}

static const vpoly_ops_t havoc_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void havoc_init(void *st, float sr)
{
    havoc_t *e = st;
    e->sr = sr;
    for (int i = 0; i < HAVOC_P_COUNT; i++)
        e->p[i] = havoc_params[i].def;
    e->bend = 0.0f;
    e->nan_count = 0;
    for (int i = 0; i < HAVOC_VOICES; i++) {
        havoc_voice_t *v = &e->v[i];
        voice_clear(v);
        for (int g = 0; g < HAVOC_GEN; g++)
            v->gen[g].inc = 0.0f;
        for (int x = 0; x < HAVOC_PROC; x++)
            v->proc[x].dly = 100.0f;
        v->semi = v->semi_target = 60.0f;
        v->vel = 1.0f;
        v->gate = 0;
    }
    vpoly_init(&e->poly, HAVOC_VOICES, sr, &havoc_poly_ops, e);
    vpoly_set_mode(&e->poly, VPOLY_LEGATO);
}

static vpoly_t *havoc_poly(void *st) { return &((havoc_t *)st)->poly; }

static void havoc_set_param(void *st, int id, float norm)
{
    havoc_t *e = st;
    if (id >= 0 && id < HAVOC_P_COUNT)
        e->p[id] = param_from_norm(&havoc_params[id], norm);
}

static void havoc_pitch_bend(void *st, float bend) { ((havoc_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

/* sqrt(x) at block rate, x > 0 */
static inline float bsqrt(float x) { return fm_exp2(0.5f * fm_log2(x)); }

/* one step of a random walk that stays around +-1 (Ornstein-Uhlenbeck): a = dt / T */
static inline float walk_step(float *x, uint32_t *rng, float a)
{
    *x += 2.449f * bsqrt(a) * dsp_rand_bipolar(rng) - a * *x;
    return *x;
}

/* The integer formulas of the `byte` generator: t = the counter, c = a constant 1..15. Only the low 8 bits sound. */
static inline uint32_t byte_formula(int f, uint32_t t, uint32_t c)
{
    switch (f) {
    case 0: return t * (t >> c);
    case 1: return (t >> c | t) * (t >> 9 & 7u);
    case 2: return (t * ((t >> 11 & 3u) + c)) ^ (t >> 4);
    case 3: return (t & t >> c) * 5u;
    case 4: return (t * c & t >> 6) | (t * 3u & t >> 10);
    case 5: return t * (((t >> 12) ^ (t >> c)) & 13u);
    case 6: return ((t >> 2) * (t >> c & 31u)) ^ (t >> 7);
    default: return ((t << 1) ^ (t >> c)) + (t >> 5 & t * 7u);
    }
}

/* One generation of an elementary cellular automaton on a ring of 32 cells. */
static inline uint32_t cell_step(uint32_t u, uint32_t rule)
{
    const uint32_t l = u << 1 | u >> 31, r = u >> 1 | u << 31;
    uint32_t out = 0;
    for (uint32_t k = 0; k < 8; k++)
        if (rule >> k & 1u)
            out |= (k & 4u ? l : ~l) & (k & 2u ? u : ~u) & (k & 1u ? r : ~r);
    return out;
}

/* One block of generator g into out; m is its modulator's block. */
static DSP_FAST_CODE void gen_render(havoc_t *e, havoc_voice_t *v, int g, float semi, const float *m, float *out)
{
    const int n = SYNTH_BLOCK;
    const float *gp = &e->p[HAVOC_G_P(g, 0)];
    havoc_gen_t *s = &v->gen[g];
    const float sr = e->sr, inv_sr = 1.0f / sr, dt = (float)n * inv_sr, inv_n = 1.0f / (float)n;
    const int type = ((int)gp[HAVOC_G_TYPE] + s->mut_type) % HAVOC_T_COUNT;
    float a = dsp_clampf(gp[HAVOC_G_A] + s->mut_a, 0.0f, 1.0f), b = dsp_clampf(gp[HAVOC_G_B] + s->mut_b, 0.0f, 1.0f);
    if (gp[HAVOC_G_LFO_A] != 0.0f || gp[HAVOC_G_LFO_B] != 0.0f) { /* its own oscillator moves the two shapes */
        s->lfo += gp[HAVOC_G_LFO_RATE] * dt;
        s->lfo -= (float)(int)s->lfo;
        s->lfo2 += 0.618034f * gp[HAVOC_G_LFO_RATE] * dt;
        s->lfo2 -= (float)(int)s->lfo2;
        a = dsp_clampf(a + gp[HAVOC_G_LFO_A] * fm_sin_pos(s->lfo), 0.0f, 1.0f);
        b = dsp_clampf(b + gp[HAVOC_G_LFO_B] * fm_sin_pos(s->lfo2), 0.0f, 1.0f);
    }

    /* pitch, and the frequency modulation by the other generator (through zero) */
    const float note = (gp[HAVOC_G_TRACK] >= 0.5f ? semi : 60.0f) + gp[HAVOC_G_PITCH] + s->mut_pitch;
    float mult = 1.0f;
    if (type == HAVOC_T_COMPUTER) { /* a new random pitch (or a rest) at its own rate */
        s->jump_ph += dt * fm_exp2(5.6f * b); /* 1 .. 48 jumps a second */
        if (s->s2 == 0.0f || s->jump_ph >= 1.0f) {
            s->jump_ph = 0.0f;
            s->s2 = dsp_rand_bipolar(&s->rng) + 1e-6f;
            s->s3 = dsp_rand_unipolar(&s->rng) < 0.15f ? 0.0f : 1.0f;
        }
        mult = fm_exp2(2.0f * a * s->s2);
    }
    int jumped = type == HAVOC_T_COMPUTER;
    if (type == HAVOC_T_BLEEP || type == HAVOC_T_GLITCH) { /* one event after the other, each with its own pitch */
        s->jump_ph -= dt;
        if (s->jump_ph <= 0.0f) {
            const float per_s = type == HAVOC_T_BLEEP ? fm_exp2(5.0f * b) : 2.0f * fm_exp2(5.0f * b); /* 1..32, 2..64 */
            const float dur = (0.3f + 1.4f * dsp_rand_unipolar(&s->rng)) / per_s;
            s->jump_ph = dur;
            jumped = 1;
            if (type == HAVOC_T_BLEEP) {
                s->ev_gain = dsp_rand_unipolar(&s->rng) < 0.25f ? 0.0f : 1.0f;                 /* a rest, one in four */
                s->ev_oct = (float)(int)(15.0f * a * dsp_rand_bipolar(&s->rng)) * (1.0f / 6.0f); /* whole tones */
                s->ev_sweep = dsp_rand_unipolar(&s->rng) < 0.45f ? 3.0f * a * dsp_rand_bipolar(&s->rng) / dur : 0.0f;
                s->ev_kind = (int)(dsp_rand_u32(&s->rng) >> 30);
            } else {
                const float r = dsp_rand_unipolar(&s->rng);
                /* a gap less and less often as the faults come faster; then loops or noise by `a`; a whine now and then */
                s->ev_kind = r < 0.3f - 0.25f * b ? 3 : r > 0.92f ? 2 : dsp_rand_unipolar(&s->rng) < a ? 1 : 0;
                s->ev_gain = s->ev_kind == 3 ? 0.0f : 0.5f + 0.5f * dsp_rand_unipolar(&s->rng);
                s->ev_oct = s->ev_kind == 2 ? 3.0f + dsp_rand_unipolar(&s->rng) : 2.0f * dsp_rand_bipolar(&s->rng);
                s->ev_sweep = 0.0f;
                s->ev_len = 4 << (dsp_rand_u32(&s->rng) >> 30); /* 4, 8, 16 or 32 cells */
                s->u0 = dsp_rand_u32(&s->rng) | 1u;
            }
        }
        s->ev_oct = dsp_clampf(s->ev_oct + s->ev_sweep * dt, -6.0f, 6.0f);
        mult = fm_exp2(s->ev_oct);
    }
    if (type == HAVOC_T_TALK) { /* one phoneme after the other: a vowel, a hiss or a stop; the pitch wanders as in speech */
        static const float vowel[7][3] = {{730.0f, 1090.0f, 2440.0f}, {270.0f, 2290.0f, 3010.0f}, {300.0f, 870.0f, 2240.0f},
                                          {530.0f, 1840.0f, 2480.0f}, {570.0f, 840.0f, 2410.0f},  {500.0f, 1500.0f, 2500.0f},
                                          {660.0f, 1720.0f, 2410.0f}};
        s->jump_ph -= dt;
        if (s->jump_ph <= 0.0f) {
            const float r = dsp_rand_unipolar(&s->rng);
            float dur = (0.5f + dsp_rand_unipolar(&s->rng)) / (2.0f * fm_exp2(2.6f * a)); /* 2 .. 12 a second */
            if (r < 0.62f) {
                const float *f = vowel[(dsp_rand_u32(&s->rng) >> 8) % 7u];
                s->tk_t[0] = f[0], s->tk_t[1] = f[1], s->tk_t[2] = f[2];
                s->tk_voiced = 1;
                s->ev_gain = 1.0f;
            } else if (r < 0.82f) {
                s->tk_t[0] = 2200.0f + 1800.0f * dsp_rand_unipolar(&s->rng);
                s->tk_t[1] = 4200.0f + 1500.0f * dsp_rand_unipolar(&s->rng);
                s->tk_t[2] = 7000.0f;
                s->tk_voiced = 0;
                s->ev_gain = 0.5f;
                dur *= 0.5f;
            } else {
                s->ev_gain = 0.0f;
                dur *= 0.6f;
            }
            s->ev_sweep = (0.3f * dsp_rand_bipolar(&s->rng) - s->ev_oct) / dur;
            s->jump_ph = dur;
        }
        s->ev_oct = dsp_clampf(s->ev_oct + s->ev_sweep * dt, -1.0f, 1.0f);
        mult = fm_exp2(s->ev_oct);
        for (int k = 0; k < 3; k++)
            s->tk_f[k] += 0.03f * (s->tk_t[k] - s->tk_f[k]); /* the mouth moves in about 20 ms */
    }
    if (type == HAVOC_T_MODEM) { /* one symbol after the other; sections: data, a preamble (alternating), a gap */
        s->jump_ph -= dt;
        if (s->jump_ph <= 0.0f) {
            s->jump_ph = dsp_maxf(s->jump_ph, -0.05f) + 1.0f / (20.0f * fm_exp2(5.0f * a));
            if (--s->md_left <= 0) {
                const float r = dsp_rand_unipolar(&s->rng);
                s->ev_kind = r < 0.2f ? 2 : r < 0.45f ? 1 : 0;
                s->md_left = s->ev_kind == 0 ? 20 + (int)(dsp_rand_u32(&s->rng) >> 26) : s->ev_kind == 1 ? 16 : 2 + (int)(dsp_rand_u32(&s->rng) >> 29);
            }
            const uint32_t levels = 2u << (uint32_t)(b * 2.99f);
            const uint32_t sym = s->ev_kind == 1 ? (uint32_t)(s->md_left & 1) * (levels - 1u) : (dsp_rand_u32(&s->rng) >> 8) % levels;
            s->ev_gain = s->ev_kind == 2 ? 0.0f : 1.0f;
            s->ev_oct = (float)sym / (float)levels; /* the tones share one octave */
            jumped = 1;
        }
        mult = fm_exp2(s->ev_oct);
    }
    const float inc = dsp_minf(fm_mtof(note) * inv_sr * mult, 0.45f);
    if (v->fresh || jumped)
        s->inc = inc;
    const float fmd = 8.0f * gp[HAVOC_G_FM] * gp[HAVOC_G_FM], d_inc = (inc - s->inc) * inv_n;
    const float fbd = 6.0f * e->p[HAVOC_P_FEEDBACK] * e->p[HAVOC_P_FEEDBACK]; /* the machine's own output */
    float incs[SYNTH_BLOCK];
    float ci = s->inc;
    for (int i = 0; i < n; i++) {
        ci += d_inc;
        incs[i] = dsp_minf(dsp_absf(ci * (1.0f + fmd * m[i] + fbd * v->ofb[i])), 0.49f);
    }
    s->inc = inc;

    switch (type) {
    case HAVOC_T_TONE: { /* sine with phase feedback (averaged over two samples), and a detuned second sine */
        const float fb = 0.9f * a * a, ratio = 1.0f + 1.5f * b, g2 = 0.45f * dsp_minf(8.0f * b, 1.0f);
        const float g1 = 1.0f - g2;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            s->ph2 += incs[i] * ratio;
            s->ph2 -= (float)(int)s->ph2;
            const float y = fm_sin(s->ph + fb * 0.5f * (s->s0 + s->s1));
            s->s1 = s->s0;
            s->s0 = y;
            out[i] = g1 * y + g2 * fm_sin(s->ph2 + fb * y);
        }
        break;
    }
    case HAVOC_T_LOGISTIC: { /* x <- r x (1 - x), one step per period: a buzz an octave below, then chaos */
        const float r = 3.2f + 0.8f * a;
        if (!(s->s0 > 0.001f && s->s0 < 0.999f))
            s->s0 = 0.4f;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f) {
                s->ph -= 1.0f;
                s->s1 = s->s0;
                s->s0 = dsp_clampf(r * s->s0 * (1.0f - s->s0), 0.002f, 0.998f);
            }
            out[i] = 2.0f * (s->s1 + b * s->ph * (s->s0 - s->s1)) - 1.0f;
        }
        break;
    }
    case HAVOC_T_BYTE: { /* the counter runs at 32 times the pitch */
        const int f = (int)(a * 7.99f);
        const uint32_t c = 1u + (uint32_t)(b * 14.99f);
        for (int i = 0; i < n; i++) {
            s->ph += 32.0f * incs[i];
            const int k = (int)s->ph;
            s->ph -= (float)k;
            s->u0 += (uint32_t)k;
            out[i] = (float)(byte_formula(f, s->u0, c) & 255u) * (1.0f / 127.5f) - 1.0f;
        }
        break;
    }
    case HAVOC_T_DUST: { /* impulses of random size, `pitch` a second on average, into a band-pass resonator */
        const float gg = fm_tan_pi(dsp_minf(50.0f * fm_exp2(7.9f * a) * inv_sr, 0.45f)), k = 2.0f * fm_exp2(-11.0f * b); /* a click .. a ring of a second */
        const float a1 = 1.0f / (1.0f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
        const float hit = dsp_minf(0.7f / a2, 60.0f); /* an impulse rings at about the same level wherever it is tuned */
        for (int i = 0; i < n; i++) {
            const float x = dsp_rand_unipolar(&s->rng) < incs[i] ? hit * dsp_rand_bipolar(&s->rng) : 0.0f;
            const float v3 = x - s->ic2, v1 = a1 * s->ic1 + a2 * v3, v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
            s->ic1 = v1 + v1 - s->ic1;
            s->ic2 = v2 + v2 - s->ic2;
            out[i] = fm_tanh(v1);
        }
        break;
    }
    case HAVOC_T_WIND: { /* white noise through a band-pass at the pitch; the gusts move the band */
        const float gust = b > 0.0f ? walk_step(&s->walk, &s->rng, dsp_minf(dt * 2.0f, 0.25f)) : 0.0f;
        const float gg = fm_tan_pi(dsp_clampf(incs[n - 1] * fm_exp2(1.5f * b * gust), 0.0004f, 0.45f));
        const float k = 2.0f * fm_exp2(-7.0f * a);
        const float a1 = 1.0f / (1.0f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
        const float lvl = dsp_minf(1.2f * bsqrt(k / gg), 60.0f) * (1.0f + b * 0.6f * gust); /* same loudness, any band */
        for (int i = 0; i < n; i++) {
            const float x = dsp_rand_bipolar(&s->rng);
            const float v3 = x - s->ic2, v1 = a1 * s->ic1 + a2 * v3, v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
            s->ic1 = v1 + v1 - s->ic1;
            s->ic2 = v2 + v2 - s->ic2;
            out[i] = fm_tanh(lvl * v1);
        }
        break;
    }
    case HAVOC_T_PULSAR: { /* every period, a sine burst at the formant under a parabolic window, then silence */
        const float finc = dsp_minf(100.0f * fm_exp2(7.0f * a) * inv_sr, 0.45f);
        const float duty = 0.04f + 0.96f * b, inv_duty = 1.0f / duty;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f) {
                s->ph -= 1.0f;
                s->ph2 = 0.0f;
            }
            float y = 0.0f;
            if (s->ph < duty) {
                const float w = s->ph * inv_duty;
                y = 4.0f * w * (1.0f - w) * fm_sin_pos(s->ph2);
                s->ph2 += finc;
            }
            out[i] = y;
        }
        break;
    }
    case HAVOC_T_CELL: { /* the 32 cells are one period of the wave; a new generation every period */
        const uint32_t rule = (uint32_t)(a * 255.99f);
        const float c = 1.0f - 0.97f * b;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f) {
                s->ph -= 1.0f;
                s->u0 = cell_step(s->u0, rule);
                if (s->u0 == 0u || s->u0 == 0xFFFFFFFFu) /* a dead world: one cell is born at random */
                    s->u0 ^= 1u << (dsp_rand_u32(&s->rng) >> 27);
            }
            const float target = s->u0 >> ((uint32_t)(s->ph * 32.0f) & 31u) & 1u ? 1.0f : -1.0f;
            s->s0 += c * (target - s->s0);
            out[i] = s->s0;
        }
        break;
    }
    case HAVOC_T_SWARM: { /* each sine on its own walk around the pitch */
        const float wa = dsp_minf(dt * 0.2f * fm_exp2(7.0f * b), 0.25f); /* a swing in 5 s .. 40 ms */
        float winc[HAVOC_SWARM];
        for (int j = 0; j < HAVOC_SWARM; j++)
            winc[j] = dsp_minf(incs[n - 1] * fm_exp2(2.0f * a * walk_step(&s->sw_x[j], &s->rng, wa)), 0.45f);
        for (int i = 0; i < n; i++) {
            float y = 0.0f;
            for (int j = 0; j < HAVOC_SWARM; j++) {
                s->sw_ph[j] += winc[j];
                if (s->sw_ph[j] >= 1.0f)
                    s->sw_ph[j] -= 1.0f;
                y += fm_sin_pos(s->sw_ph[j]);
            }
            out[i] = 0.3f * y;
        }
        break;
    }
    case HAVOC_T_COMPUTER: { /* the jumps are made above; here the tone, a little bright, and its rests */
        const float d = (s->s3 - s->s1) * inv_n;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            s->s1 += d;
            const float y = fm_sin(s->ph + 0.12f * s->s0);
            s->s0 = y;
            out[i] = s->s1 * y;
        }
        s->s1 = s->s3;
        break;
    }
    case HAVOC_T_BLEEP: { /* the note of the moment: a sine, a soft square, a bright sine or two sines multiplied */
        const float d = (s->ev_gain - s->s1) * inv_n;
        const int kind = s->ev_kind;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            s->s1 += d;
            float y = fm_sin(s->ph + (kind == 2 ? 0.2f * s->s0 : 0.0f));
            s->s0 = y;
            if (kind == 1)
                y = fm_tanh(4.0f * y);
            else if (kind == 3) {
                s->ph2 += 2.76f * incs[i];
                s->ph2 -= (float)(int)s->ph2;
                y *= fm_sin_pos(s->ph2);
            }
            out[i] = s->s1 * y;
        }
        s->s1 = s->ev_gain;
        break;
    }
    case HAVOC_T_GLITCH: { /* the fault of the moment; its level changes at once: every fault starts with a click */
        const int kind = s->ev_kind;
        const float lv = s->ev_gain, cells = (float)s->ev_len;
        const uint32_t mask = (uint32_t)s->ev_len - 1u;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            float y = 0.0f;
            if (kind == 0) { /* a few bits stuck in a loop: a buzz whose colour is the bits */
                y = s->u0 >> ((uint32_t)(s->ph * cells) & mask) & 1u ? 1.0f : -1.0f;
            } else if (kind == 1) { /* bit noise, clocked at 16 times the pitch */
                s->ph2 += 16.0f * incs[i];
                if (s->ph2 >= 1.0f) {
                    s->ph2 -= (float)(int)s->ph2;
                    dsp_rand_u32(&s->u0);
                }
                y = s->u0 & 1u ? 1.0f : -1.0f;
            } else if (kind == 2) { /* a whine */
                y = fm_sin_pos(s->ph);
            }
            out[i] = lv * y;
        }
        break;
    }
    case HAVOC_T_TALK: { /* a saw (the voice) or noise (the breath) through three band-pass formants */
        float c1[3], c2[3], c3[3];
        for (int k = 0; k < 3; k++) {
            const float gg = fm_tan_pi(dsp_minf(s->tk_f[k] * inv_sr, 0.45f));
            c1[k] = 1.0f / (1.0f + gg * (gg + 0.14f));
            c2[k] = gg * c1[k];
            c3[k] = gg * c2[k];
        }
        static const float weight[3] = {1.0f, 0.6f, 0.35f};
        const float d = (s->ev_gain - s->s1) * inv_n, breath = s->tk_voiced ? b : 1.0f;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            s->s1 += d;
            const float x = s->s1 * ((1.0f - breath) * (s->ph + s->ph - 1.0f) + 0.7f * breath * dsp_rand_bipolar(&s->rng));
            float y = 0.0f;
            for (int k = 0; k < 3; k++) {
                float *ic = &s->tk_ic[2 * k];
                const float v3 = x - ic[1], v1 = c1[k] * ic[0] + c2[k] * v3, v2 = ic[1] + c2[k] * ic[0] + c3[k] * v3;
                ic[0] = v1 + v1 - ic[0];
                ic[1] = v2 + v2 - ic[1];
                y += weight[k] * v1;
            }
            out[i] = fm_tanh(0.8f * y);
        }
        s->s1 = s->ev_gain;
        break;
    }
    case HAVOC_T_MODEM: { /* the tone of the symbol, its phase unbroken from one symbol to the next */
        const float d = (s->ev_gain - s->s1) * inv_n;
        for (int i = 0; i < n; i++) {
            s->ph += incs[i];
            if (s->ph >= 1.0f)
                s->ph -= 1.0f;
            s->s1 += d;
            out[i] = s->s1 * fm_sin_pos(s->ph);
        }
        s->s1 = s->ev_gain;
        break;
    }
    case HAVOC_T_NEURON: { /* v' = 0.04 v^2 + 5 v + 140 - u + I, u' = a (0.2 v - u); at 30 mV: v <- c, u <- u + d */
        const float c = -65.0f + 15.0f * a, dd = 8.0f - 6.0f * a, cur = 5.0f + 14.0f * b, jit = 10.0f * b;
        if (!(s->s0 < -20.0f && s->s0 > -110.0f)) /* (another process left its state here) */
            s->s0 = -65.0f, s->s1 = -13.0f;
        for (int i = 0; i < n; i++) {
            const float h = dsp_minf(40.0f * incs[i], 0.5f); /* milliseconds of the model per sample */
            const float vv = s->s0, I = cur + jit * dsp_rand_bipolar(&s->rng);
            s->s0 = vv + h * (0.04f * vv * vv + 5.0f * vv + 140.0f - s->s1 + I);
            s->s1 += h * 0.02f * (0.2f * vv - s->s1);
            float y = s->s0;
            if (s->s0 >= 30.0f) {
                y = 30.0f;
                s->s0 = c;
                s->s1 += dd;
            }
            s->s0 = dsp_maxf(s->s0, -100.0f);
            s->s1 = dsp_clampf(s->s1, -40.0f, 60.0f);
            out[i] = dsp_clampf(0.02f * (y + 55.0f), -1.0f, 1.7f);
        }
        break;
    }
    case HAVOC_T_ALIAS: { /* a saw with no band limit, far above its pitch, on a few steps: what is heard is the folding */
        const float times = fm_exp2(6.0f * a), steps = fm_exp2(8.0f - 7.0f * b), isteps = 2.0f / steps;
        for (int i = 0; i < n; i++) {
            s->ph += times * incs[i];
            s->ph -= (float)(int)s->ph;
            out[i] = (float)(int)(s->ph * steps) * isteps - 1.0f + 0.5f * isteps;
        }
        break;
    }
    default: { /* rumble: a leaky random walk whose speed is the pitch; only what passes the threshold, then a fold */
        const float k = dsp_minf(8.0f * incs[n - 1], 1.0f), leak = 1.0f - 0.05f * k;
        const float norm = 0.45f / bsqrt(k), thr = 1.2f * b, fold = a, drive = 0.2f + 1.2f * a;
        for (int i = 0; i < n; i++) {
            s->s0 = dsp_clampf(s->s0 * leak + k * dsp_rand_bipolar(&s->rng), -8.0f, 8.0f);
            float y = norm * s->s0;
            y = y > thr ? y - thr : y < -thr ? y + thr : 0.0f;
            y = dsp_clampf(y, -1.5f, 1.5f);
            out[i] = y + fold * (fm_sin(drive * y) - y);
        }
        break;
    }
    }

    /* rot: resampled and quantised with no filter, at a rate of its own */
    const float rot = gp[HAVOC_G_ROT];
    if (rot > 0.0f) {
        const float rate = dsp_minf(fm_exp2(-7.5f * rot) * (1.0f + 0.07f * (float)g), 1.0f);
        const float lv = fm_exp2(11.0f - 11.0f * rot), ilv = 1.0f / lv;
        for (int i = 0; i < n; i++) {
            s->rot_ph += rate;
            if (s->rot_ph >= 1.0f) {
                s->rot_ph -= 1.0f;
                const float x = dsp_clampf(out[i], -2.0f, 2.0f);
                s->rot_hold = ((float)(int32_t)(x * lv + (x >= 0.0f ? 0.0f : -1.0f)) + 0.5f) * ilv;
            }
            out[i] = s->rot_hold;
        }
    }

    /* ring modulation by the other generator */
    const float ring = gp[HAVOC_G_RING];
    if (ring > 0.0f)
        for (int i = 0; i < n; i++)
            out[i] *= 1.0f - ring + ring * m[i];

    /* life: the level this generator has for this block */
    const float rate = gp[HAVOC_G_LIFE_RATE], shape = gp[HAVOC_G_LIFE_SHAPE];
    float gain = 1.0f;
    switch ((int)gp[HAVOC_G_LIFE]) {
    case HAVOC_LIFE_BURSTS: { /* a random telegraph: `rate` cycles a second on average, on for `shape` of the time */
        const float duty = dsp_clampf(shape, 0.03f, 0.97f);
        const float flip = rate * dt / (s->life_on ? duty : 1.0f - duty);
        if (dsp_rand_unipolar(&s->rng) < flip)
            s->life_on = !s->life_on;
        gain = s->life_on ? 1.0f : 0.0f;
        break;
    }
    case HAVOC_LIFE_PULSE: /* a hit every period, falling in a share of it */
        s->life_ph += rate * dt;
        if (s->life_ph >= 1.0f) {
            s->life_ph -= 1.0f;
            s->life_env = 1.0f;
        } else {
            s->life_env *= fm_exp2(-8.0f * rate * dt / (4.0f * shape * shape + 0.02f));
        }
        gain = s->life_env;
        break;
    case HAVOC_LIFE_SWELL: /* a slow breath */
        gain = dsp_clampf(1.0f - shape + shape * (0.5f + 0.9f * walk_step(&s->life_env, &s->rng, dsp_minf(rate * dt, 0.25f))),
                          0.0f, 1.6f);
        break;
    default:
        break;
    }
    if (v->fresh)
        s->gain = gain;
    const float dg = (gain - s->gain) * inv_n;
    float cg = s->gain;
    for (int i = 0; i < n; i++) {
        cg += dg;
        out[i] *= cg;
    }
    s->gain = gain;

    if (!(out[n - 1] >= -4.0f && out[n - 1] <= 4.0f)) { /* a NaN must not stay in a process */
        for (int i = 0; i < n; i++)
            out[i] = 0.0f;
        s->s0 = 0.4f;
        s->s1 = s->ic1 = s->ic2 = s->ph = s->ph2 = 0.0f;
        e->nan_count++;
    }
}

/* One block of a processor, in place on the two sides. */
static DSP_FAST_CODE void proc_render(havoc_t *e, havoc_proc_t *q, const float *pp, float *xl, float *xr)
{
    const int n = SYNTH_BLOCK;
    const int type = (int)pp[HAVOC_X_TYPE];
    const float p1 = pp[HAVOC_X_P1], p2 = pp[HAVOC_X_P2], mix = pp[HAVOC_X_MIX];
    const float sr = e->sr, inv_sr = 1.0f / sr;
    float *const side[2] = {xl, xr};
    if (type == HAVOC_X_OFF || mix <= 0.0f)
        return;
    switch (type) {
    case HAVOC_X_LP:
    case HAVOC_X_BP:
    case HAVOC_X_HP: {
        const float gg = fm_tan_pi(dsp_clampf(30.0f * fm_exp2(9.2f * p1), 20.0f, 0.45f * sr) * inv_sr);
        const float k = 2.0f - 1.94f * p2;
        const float a1 = 1.0f / (1.0f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
        const float m0 = type == HAVOC_X_HP ? 1.0f : 0.0f;
        const float m1 = type == HAVOC_X_BP ? k : type == HAVOC_X_HP ? -k : 0.0f;
        const float m2 = type == HAVOC_X_LP ? 1.0f : type == HAVOC_X_HP ? -1.0f : 0.0f;
        for (int c = 0; c < 2; c++) {
            float *x = side[c];
            float ic1 = q->ic1[c], ic2 = q->ic2[c];
            for (int i = 0; i < n; i++) {
                const float v3 = x[i] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = v1 + v1 - ic1;
                ic2 = v2 + v2 - ic2;
                x[i] += mix * (m0 * x[i] + m1 * v1 + m2 * v2 - x[i]);
            }
            q->ic1[c] = ic1;
            q->ic2[c] = ic2;
        }
        break;
    }
    case HAVOC_X_COMB: {
        const float dly = dsp_clampf(sr / (30.0f * fm_exp2(8.0f * p1)), 2.0f, (float)(HAVOC_DELAY - 4));
        const float fb = 0.97f * (p2 + p2 - 1.0f), dd = (dly - q->dly) * (1.0f / (float)SYNTH_BLOCK);
        for (int i = 0; i < n; i++) {
            q->wr = (q->wr + 1) & HAVOC_MASK;
            q->dly += dd;
            const int di = (int)q->dly;
            const float df = q->dly - (float)di;
            for (int c = 0; c < 2; c++) {
                const float s0 = (float)q->buf[c][(q->wr - di) & HAVOC_MASK];
                const float s1 = (float)q->buf[c][(q->wr - di - 1) & HAVOC_MASK];
                const float w = fm_tanh(side[c][i] + fb * (s0 + df * (s1 - s0)) * (1.0f / 32767.0f));
                q->buf[c][q->wr] = (int16_t)(w * 32767.0f);
                side[c][i] += mix * (w - side[c][i]);
            }
        }
        q->dly = dly;
        break;
    }
    case HAVOC_X_FOLD: {
        const float drive = 0.15f * fm_exp2(4.5f * p1), off = 0.25f * p2, dc = fm_sin(off);
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < n; i++)
                side[c][i] += mix * (fm_sin(drive * side[c][i] + off) - dc - side[c][i]);
        break;
    }
    case HAVOC_X_CRUSH: {
        const float rate = fm_exp2(-7.0f * p1), lv = fm_exp2(15.0f - 14.0f * p2), ilv = 1.0f / lv;
        for (int i = 0; i < n; i++) {
            q->cph += rate;
            const int tick = q->cph >= 1.0f;
            if (tick)
                q->cph -= 1.0f;
            for (int c = 0; c < 2; c++) {
                if (tick) {
                    const float x = dsp_clampf(side[c][i], -4.0f, 4.0f);
                    q->hold[c] = (float)(int32_t)(x * lv + (x >= 0.0f ? 0.5f : -0.5f)) * ilv;
                }
                side[c][i] += mix * (q->hold[c] - side[c][i]);
            }
        }
        break;
    }
    case HAVOC_X_RING: {
        const float inc = dsp_minf(fm_exp2(13.0f * p1) * inv_sr, 0.45f), spread = 0.25f * p2;
        for (int i = 0; i < n; i++) {
            q->ph += inc;
            if (q->ph >= 1.0f)
                q->ph -= 1.0f;
            xl[i] += mix * (xl[i] * fm_sin_pos(q->ph) - xl[i]);
            xr[i] += mix * (xr[i] * fm_sin_pos(q->ph + spread) - xr[i]);
        }
        break;
    }
    case HAVOC_X_SUB: { /* every upward zero crossing flips a divider, every fall of that one flips the next */
        const float low = p1, c = 0.02f + 0.6f * p2 * p2;
        for (int k = 0; k < 2; k++) {
            float *x = side[k];
            for (int i = 0; i < n; i++) {
                q->sub_env[k] += 0.004f * (dsp_absf(x[i]) - q->sub_env[k]);
                const float hyst = 0.1f * q->sub_env[k];
                if (q->sub_sgn[k] <= 0 && x[i] > hyst) {
                    q->sub_sgn[k] = 1;
                    q->sub_ff[k] = (q->sub_ff[k] + 1) & 3;
                } else if (q->sub_sgn[k] > 0 && x[i] < -hyst) {
                    q->sub_sgn[k] = -1;
                }
                const float sq = (1.0f - low) * (q->sub_ff[k] & 1 ? 1.0f : -1.0f) + low * (q->sub_ff[k] & 2 ? 1.0f : -1.0f);
                q->sub_lp[k] += c * (1.5f * q->sub_env[k] * sq - q->sub_lp[k]);
                x[i] += mix * (q->sub_lp[k] - 0.5f * x[i]); /* at full mix, half of the sound stays above its sub */
            }
        }
        break;
    }
    case HAVOC_X_WARP: { /* a delay of 5 ms whose length a sine moves: vibrato, then frequency modulation */
        const float inc = dsp_minf(0.5f * fm_exp2(11.0f * p1) * inv_sr, 0.45f), base = 0.005f * sr, depth = 0.95f * p2 * base;
        for (int i = 0; i < n; i++) {
            q->ph += inc;
            if (q->ph >= 1.0f)
                q->ph -= 1.0f;
            q->wr = (q->wr + 1) & HAVOC_MASK;
            const float dl = base + depth * fm_sin_pos(q->ph);
            const int di = (int)dl;
            const float df = dl - (float)di;
            for (int c = 0; c < 2; c++) {
                q->buf[c][q->wr] = (int16_t)(dsp_clampf(side[c][i], -4.0f, 4.0f) * 8191.0f);
                const float s0 = (float)q->buf[c][(q->wr - di) & HAVOC_MASK], s1 = (float)q->buf[c][(q->wr - di - 1) & HAVOC_MASK];
                side[c][i] += mix * ((s0 + df * (s1 - s0)) * (1.0f / 8191.0f) - side[c][i]);
            }
        }
        break;
    }
    case HAVOC_X_MANGLE: { /* the sample as a 12-bit word */
        const int op = (int)(p1 * 3.99f);
        const uint32_t amount = (uint32_t)(p2 * 4095.0f), turn = 1u + (uint32_t)(p2 * 10.99f);
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < n; i++) {
                uint32_t w = (uint32_t)(int32_t)(dsp_clampf(side[c][i], -1.0f, 1.0f) * 2047.0f + 2048.0f) & 4095u;
                if (op == 0) {
                    w ^= amount;
                } else if (op == 1) {
                    w = (w << turn | w >> (12u - turn)) & 4095u;
                } else if (op == 2) {
                    w &= ~amount | 2048u; /* (the sign bit is kept: the sound thins out instead of vanishing) */
                } else {
                    uint32_t r = 0;
                    for (uint32_t k = 0; k < 12u; k++)
                        r |= (w >> k & 1u) << (11u - k);
                    w = (r & amount) | (w & ~amount);
                }
                side[c][i] += mix * (((float)w - 2048.0f) * (1.0f / 2047.0f) - side[c][i]);
            }
        break;
    }
    case HAVOC_X_STUTTER: { /* records the sum; now and then replays its last instants in a loop instead */
        q->wait -= (float)n * inv_sr;
        if (q->wait <= 0.0f) {
            const float share = dsp_clampf(p2, 0.0f, 0.98f);
            if (q->stuck || share <= 0.0f) { /* let go, for a time that keeps the share */
                q->stuck = 0;
                q->wait = share <= 0.0f ? 0.1f : (0.02f + 0.3f * dsp_rand_unipolar(&q->rng)) * (1.0f - share) / (share + 0.02f);
            } else { /* stuck on a fragment around the length asked, for 2 to 12 turns */
                q->len = (int)dsp_clampf(0.002f * sr * fm_exp2(4.4f * p1) * (0.6f + 0.8f * dsp_rand_unipolar(&q->rng)), 16.0f,
                                         (float)(HAVOC_DELAY - 1));
                q->pos = 0;
                q->stuck = 1;
                q->wait = (float)q->len * inv_sr * (2.0f + 10.0f * dsp_rand_unipolar(&q->rng));
            }
        }
        for (int i = 0; i < n; i++) {
            if (q->stuck) {
                const int at = (q->wr - q->len + 1 + q->pos) & HAVOC_MASK;
                if (++q->pos >= q->len)
                    q->pos = 0;
                for (int c = 0; c < 2; c++)
                    side[c][i] += mix * ((float)q->buf[c][at] * (1.0f / 8191.0f) - side[c][i]);
            } else {
                q->wr = (q->wr + 1) & HAVOC_MASK;
                for (int c = 0; c < 2; c++)
                    q->buf[c][q->wr] = (int16_t)(dsp_clampf(side[c][i], -4.0f, 4.0f) * 8191.0f);
            }
        }
        break;
    }
    default: { /* chop: open for p2 of each period; the edges take about a millisecond */
        const float inc = 0.5f * fm_exp2(6.0f * p1) * inv_sr, duty = 0.02f + 0.96f * p2;
        for (int i = 0; i < n; i++) {
            q->ph += inc;
            if (q->ph >= 1.0f)
                q->ph -= 1.0f;
            q->gate += 0.05f * ((q->ph < duty ? 1.0f : 0.0f) - q->gate);
            xl[i] += mix * (xl[i] * q->gate - xl[i]);
            xr[i] += mix * (xr[i] * q->gate - xr[i]);
        }
        break;
    }
    }
}

static DSP_FAST_CODE void voice_render(havoc_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    havoc_voice_t *v = &e->v[vi];
    const float sr = e->sr, inv_sr = 1.0f / sr, dt = (float)n * inv_sr, inv_n = 1.0f / (float)n;
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, concert pitch, MPE */

    /* envelope (block rate): exponential towards an overshooting target, as an analog one */
    const float env0 = v->env;
    if (v->gate) {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 1.609f * dt / p[HAVOC_P_ATTACK]); /* 0 -> 1 in `attack` */
        v->env = dsp_minf(v->env + (1.25f - v->env) * c, 1.0f);
    } else {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 3.932f * dt / p[HAVOC_P_RELEASE]); /* 1 -> 0 in `release` */
        v->env = dsp_maxf(v->env - (v->env + 0.02f) * c, 0.0f);
    }

    /* glide of the generators that follow the note */
    if (v->semi != v->semi_target) {
        v->semi += (v->semi_target - v->semi) * voice_glide_arrive_coef(p[HAVOC_P_GLIDE], n, sr);
        if (dsp_absf(v->semi_target - v->semi) < 1e-3f)
            v->semi = v->semi_target;
    }
    const float semi = v->semi + e->bend * p[HAVOC_P_BEND_RANGE] + vm[VM_PITCH];

    /* mutations: now and then one generator is thrown elsewhere */
    const float mut = p[HAVOC_P_MUTATE], mdepth = p[HAVOC_P_MUTATE_DEPTH];
    if (mut > 0.001f && dsp_rand_unipolar(&v->rng) < 0.05f * fm_exp2(8.0f * mut) * dt) {
        havoc_gen_t *s = &v->gen[dsp_rand_u32(&v->rng) >> 30];
        s->mut_a = mdepth * dsp_rand_bipolar(&v->rng);
        s->mut_b = mdepth * dsp_rand_bipolar(&v->rng);
        s->mut_pitch = (float)(int)(24.0f * mdepth * dsp_rand_bipolar(&v->rng));
        if (dsp_rand_unipolar(&v->rng) < 2.0f * mdepth - 1.0f)
            s->mut_type = (int)(dsp_rand_unipolar(&v->rng) * (float)HAVOC_T_COUNT) % HAVOC_T_COUNT;
    }

    /* the generators, each one modulated by the block of the one before */
    float out[HAVOC_GEN][SYNTH_BLOCK], xl[SYNTH_BLOCK], xr[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        xl[i] = xr[i] = 0.0f;
    const float width = p[HAVOC_P_WIDTH];
    for (int g = 0; g < HAVOC_GEN; g++) {
        const float *gp = &p[HAVOC_G_P(g, 0)], *next = &p[HAVOC_G_P((g + 1) & (HAVOC_GEN - 1), 0)];
        const float level = gp[HAVOC_G_LEVEL];
        if (level <= 0.0f && next[HAVOC_G_FM] <= 0.0f && next[HAVOC_G_RING] <= 0.0f) { /* nobody hears it */
            for (int i = 0; i < n; i++)
                out[g][i] = 0.0f;
            continue;
        }
        gen_render(e, v, g, semi, g == 0 ? v->fb : out[g - 1], out[g]);
        if (level > 0.0f) {
            float gl, gr;
            voice_pan(dsp_clampf(width * gp[HAVOC_G_PAN], -1.0f, 1.0f), &gl, &gr);
            gl *= level, gr *= level;
            for (int i = 0; i < n; i++) {
                xl[i] += gl * out[g][i];
                xr[i] += gr * out[g][i];
            }
        }
    }
    for (int i = 0; i < n; i++)
        v->fb[i] = out[HAVOC_GEN - 1][i];
    v->fresh = 0;

    /* the processors, a soft bound (a resonant filter can ask for much more than full scale), the DC blocker */
    for (int x = 0; x < HAVOC_PROC; x++)
        proc_render(e, &v->proc[x], &p[HAVOC_X_P(x, 0)], xl, xr);
    const float dark = p[HAVOC_P_DARK];
    if (dark > 0.0f) { /* 18 kHz .. 140 Hz, 12 dB per octave, no resonance */
        const float gg = fm_tan_pi(18000.0f * fm_exp2(-7.0f * dark) * inv_sr);
        const float a1 = 1.0f / (1.0f + gg * (gg + 1.414f)), a2 = gg * a1, a3 = gg * a2;
        float *const side[2] = {xl, xr};
        for (int c = 0; c < 2; c++) {
            float ic1 = v->dk[2 * c], ic2 = v->dk[2 * c + 1];
            for (int i = 0; i < n; i++) {
                const float v3 = side[c][i] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = v1 + v1 - ic1;
                ic2 = v2 + v2 - ic2;
                side[c][i] = v2;
            }
            v->dk[2 * c] = ic1;
            v->dk[2 * c + 1] = ic2;
        }
    }
    const float dc = 1.0f - 125.7f * inv_sr; /* 20 Hz */
    for (int i = 0; i < n; i++) {
        const float sl = 2.0f * fm_tanh(0.5f * xl[i]), sr_ = 2.0f * fm_tanh(0.5f * xr[i]);
        v->dc_yl = sl - v->dc_xl + dc * v->dc_yl;
        v->dc_xl = sl;
        v->dc_yr = sr_ - v->dc_xr + dc * v->dc_yr;
        v->dc_xr = sr_;
        xl[i] = v->dc_yl;
        xr[i] = v->dc_yr;
        v->ofb[i] = 0.5f * (xl[i] + xr[i]);
    }

    /* level: envelope, velocity, volume */
    const float vol = fm_db2lin(p[HAVOC_P_VOLUME]);
    const float g1 = HAVOC_GAIN * v->vel * vol * v->env * v->env;
    const float g0 = env0 <= 0.0f && v->gain_prev == 0.0f ? 0.0f : v->gain_prev;
    dsp_ramp_mul(xl, g0, g1, n);
    dsp_ramp_mul(xr, g0, g1, n);
    v->gain_prev = g1;

    /* steal fade, per-voice level and pan modulation, NaN guard, into the bus */
    float g[SYNTH_BLOCK];
    if (vpoly_fade(&e->poly, vi, g, n))
        for (int i = 0; i < n; i++)
            xl[i] *= g[i], xr[i] *= g[i];
    for (int i = 0; i < n; i++)
        g[i] = 1.0f;
    float p0, p1, l0, r0, l1, r1;
    voice_vmod_apply(&e->poly, vi, g, n, &p0, &p1);
    voice_pan_offset(p0, 1.0f, 1.0f, &l0, &r0);
    voice_pan_offset(p1, 1.0f, 1.0f, &l1, &r1);
    dsp_guard_block(xl, n, &e->nan_count);
    dsp_guard_block(xr, n, &e->nan_count);
    const float dl = (l1 - l0) * inv_n, dr = (r1 - r0) * inv_n;
    for (int i = 0; i < n; i++) {
        l0 += dl, r0 += dr;
        L[i] += xl[i] * g[i] * l0;
        R[i] += xr[i] * g[i] * r0;
    }
    vpoly_end_block(&e->poly, vi, v->gate || v->env > 0.0f);
}

static DSP_FAST_CODE void havoc_render(void *st, float *L, float *R)
{
    havoc_t *e = st;
    for (int vi = 0; vi < HAVOC_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* the signal chain for interfaces (engine_stage_t) */
static const engine_stage_t havoc_stages[] = {
    {"gen1", "Generator 1", "source", "g1_", "", NULL, NULL, 0.0f, 0.0f},
    {"gen2", "Generator 2", "source", "g2_", "", NULL, NULL, 0.0f, 0.0f},
    {"gen3", "Generator 3", "source", "g3_", "", NULL, NULL, 0.0f, 0.0f},
    {"gen4", "Generator 4", "source", "g4_", "", NULL, NULL, 0.0f, 0.0f},
    {"proc1", "Processor 1", "filter", "x1_", "gen1 gen2 gen3 gen4", NULL, NULL, 0.0f, 0.0f},
    {"proc2", "Processor 2", "filter", "x2_", "proc1", NULL, NULL, 0.0f, 0.0f},
    {"chaos", "Chaos", "mod", "feedback mutate mutate_depth", "", "gen1 gen2 gen3 gen4", NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "dark width amp_attack amp_release velocity volume", "proc2", NULL, NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "glide bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t havoc_engine = {
    .name = "havoc",
    .description = "noise machine with a wide palette: four generators, each one of sixteen processes (feedback sine, "
                   "logistic map, integer formula, dust into a resonator, band of noise, pulsar train, cellular "
                   "automaton, swarm of sines, random jumping tone, clipped random walk, chatter of bleeps, electronic "
                   "faults, formant voice, data transmission, model neuron, aliased saw) with its own pitch, rot, life "
                   "(steady, bursts, pulse, swell) and place, modulating each other in a ring (FM, ring), then two "
                   "processors (filter, comb, fold, crush, ring, chop, stutter, octave divider, warp, bit mangle); the output can feed back "
                   "into every frequency and the machine can mutate while it plays (1 voice, notes slide the machine)",
    .params = havoc_params,
    .param_count = HAVOC_P_COUNT,
    .voices = HAVOC_VOICES,
    .init = havoc_init,
    .poly = havoc_poly,
    .set_param = havoc_set_param,
    .pitch_bend = havoc_pitch_bend,
    .render = havoc_render,
    .stages = havoc_stages,
    .stage_count = (int)(sizeof havoc_stages / sizeof havoc_stages[0]),
};
