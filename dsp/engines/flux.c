/* flux.c — feedback noise machine (see flux.h). Per sample, for each of the eight channels: the oscillator (band
 * limited pulse with its mean removed, or a parabolic sine), the delay read (linear interpolation, the time ramped
 * over the block), then in the loop a high-pass (below the loop's fundamental), a sine wavefolder, the regulator's gain, the TPT filter, the mix with the next
 * channel's loop (cross) and a tanh before the write: whatever the settings, the loop stays in +-1. Everything
 * else runs once per block: the mapping of the bars by the master controls, the random walks, the regulator's
 * target (no division per sample), the impulses of the crackle, the envelope. */
#include "flux.h"
#include "../core/fastmath.h"

#define FLUX_GAIN 0.13f   /* eight bounded channels -> about -18 dBFS */
#define FLUX_EXCITE 0.6f  /* level of the oscillator into its loop */
#define FLUX_TARGET 0.5f  /* level the regulator holds the loop at */
#define FLUX_MASK (FLUX_DELAY - 1)

static const char *const mode_labels[] = {"loop", "pre", "para", NULL};
static const char *const filter_labels[] = {"lp", "bp", "hp", NULL};

/* the eight bars of one channel setting */
#define FLUX_BAR(c, k, nm, n, d, help) \
    PARAM(FLUX_CH_P(c, k), nm "_" n, "flux.ch" n "." nm, 0.0f, 1.0f, d, PARAM_LIN, "", 1, NULL, help)
#define FLUX_ROW(c, nm, d1, d2, d3, d4, d5, d6, d7, d8, help)                                             \
    FLUX_BAR(c, 0, nm, "1", d1, help), FLUX_BAR(c, 1, nm, "2", d2, help), FLUX_BAR(c, 2, nm, "3", d3, help), \
    FLUX_BAR(c, 3, nm, "4", d4, help), FLUX_BAR(c, 4, nm, "5", d5, help), FLUX_BAR(c, 5, nm, "6", d6, help), \
    FLUX_BAR(c, 6, nm, "7", d7, help), FLUX_BAR(c, 7, nm, "8", d8, help)

const param_desc_t flux_params[FLUX_P_COUNT] = {
    PARAM(FLUX_P_MODE, "mode", "flux.mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, mode_labels,
          "loop: pulse, filter inside the feedback loop, the flow modulates the frequency (the wildest); pre: pulse, "
          "filter before the loop, the flow modulates the amplitude; para: a parabolic sine, no filter, the flow "
          "modulates the delay time (the smoothest)"),
    PARAM(FLUX_P_MAP_PITCH, "map_pitch", "flux.map.pitch", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "master pitch: reshapes the eight pitch bars (left: low and slow, right: high)"),
    PARAM(FLUX_P_MAP_CUTOFF, "map_cutoff", "flux.map.cutoff", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "master cutoff: reshapes the eight cutoff bars (left: dark, right: open)"),
    PARAM(FLUX_P_MAP_TIME, "map_time", "flux.map.time", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "master delay time: reshapes the eight time bars (left: very short, comb-like and metallic; right: long, "
          "deep resonances and flutter)"),
    PARAM(FLUX_P_MAP_FLOW, "map_flow", "flux.map.flow", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "master flow: reshapes the eight flow bars (left: inert, the channels ignore each other; right: every "
          "channel modulates its neighbour, chaos)"),
    PARAM(FLUX_P_FILTER_TYPE, "filter_type", "filter.type", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, filter_labels,
          "the channels' filter: low-pass, band-pass or high-pass (none in para mode)"),
    PARAM(FLUX_P_RESO, "resonance", "filter.resonance", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "resonance of the channels' filter: inside a loop it turns into whistles"),
    PARAM(FLUX_P_NORMALIZE, "normalize", "flux.normalize", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "level regulator in every loop: 0 = plain feedback (it dies away), 1 = the loop is held at a constant "
          "level and rings forever, whatever its feedback"),
    PARAM(FLUX_P_FOLD, "fold", "flux.fold", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "wavefolder in every loop: each pass folds the wave again, from a glassy edge to dense digital noise"),
    PARAM(FLUX_P_CROSS, "cross", "flux.cross", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "how much of each loop comes from the next channel's: 0 = eight separate resonators, 1 = one ring of "
          "eight delays (a dense metallic network)"),
    PARAM(FLUX_P_CRACKLE, "crackle", "flux.crackle", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "random impulses exciting the loops: 0 = none, low = rare sparks, 1 = a dense crackling (about 100 a "
          "second per channel)"),
    PARAM(FLUX_P_MOTION, "motion", "drone.motion", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 1, NULL,
          "speed of the random drift of every channel (a minute per swing .. several a second)"),
    PARAM(FLUX_P_DEPTH, "depth", "drone.depth", 0.0f, 1.0f, 0.4f, PARAM_LIN, "", 1, NULL,
          "how far the channels' pitch, delay time, cutoff, level and place drift (0 = frozen)"),
    PARAM(FLUX_P_WIDTH, "width", "drone.width", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL,
          "spread of the eight channels in the stereo field"),
    PARAM(FLUX_P_ATTACK, "amp_attack", "amp.attack", 0.005f, 60.0f, 1.0f, PARAM_EXP, "s", 1, NULL, "fade-in time"),
    PARAM(FLUX_P_RELEASE, "amp_release", "amp.release", 0.005f, 60.0f, 3.0f, PARAM_EXP, "s", 1, NULL,
          "fade-out time"),
    PARAM(FLUX_P_GLIDE, "glide", "voice.glide", 0.001f, 20.0f, 0.2f, PARAM_EXP, "s", 1, NULL,
          "time for the whole machine to slide to a new note played while another is held"),
    PARAM(FLUX_P_VELOCITY, "velocity", "amp.velocity", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "velocity to level: 0 = every note equal, 1 = full range"),
    PARAM(FLUX_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "engine volume"),
    PARAM(FLUX_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 12.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
    FLUX_ROW(FLUX_C_PITCH, "pitch", 0.30f, 0.36f, 0.42f, 0.47f, 0.52f, 0.58f, 0.63f, 0.20f,
             "pitch of this channel's oscillator around the note: 0 = 5 octaves below (a slow pulse train, clicks), "
             "0.56 = the note, 1 = 4 octaves above"),
    FLUX_ROW(FLUX_C_PULSE, "pulse", 0.20f, 0.50f, 0.10f, 0.80f, 0.30f, 0.60f, 0.15f, 0.05f,
             "pulse width of this channel: 0 = a thin click, 1 = a square (unused in para mode)"),
    FLUX_ROW(FLUX_C_CUTOFF, "cutoff", 0.45f, 0.60f, 0.50f, 0.70f, 0.55f, 0.75f, 0.65f, 0.40f,
             "cutoff of this channel's filter: 0 = 30 Hz, 1 = 17 kHz"),
    FLUX_ROW(FLUX_C_TIME, "time", 0.90f, 0.75f, 0.82f, 0.60f, 0.70f, 0.50f, 0.66f, 0.95f,
             "delay time of this channel's loop: 0 = 0.04 ms (a bright comb), 0.5 = 1.3 ms, 1 = 42 ms (a low "
             "resonance, a flutter)"),
    FLUX_ROW(FLUX_C_FEEDBACK, "feedback", 0.85f, 0.80f, 0.88f, 0.75f, 0.82f, 0.70f, 0.86f, 0.90f,
             "feedback of this channel's loop: 0 = the bare oscillator, 1 = it never decays"),
    FLUX_ROW(FLUX_C_FLOW, "flow", 0.20f, 0.10f, 0.30f, 0.00f, 0.25f, 0.15f, 0.00f, 0.20f,
             "how much the previous channel's output modulates this one (its frequency, amplitude or delay time, "
             "by mode)"),
    FLUX_ROW(FLUX_C_LEVEL, "level", 0.70f, 0.70f, 0.70f, 0.70f, 0.70f, 0.70f, 0.70f, 0.70f,
             "output level of this channel"),
};

/* one step of a smoothed walk: a = dt / T, kick = sqrt(6 a) (uniform noise of unit variance) */
static float walk_step(flux_walk_t *w, uint32_t *rng, float a, float kick)
{
    w->x += kick * dsp_rand_bipolar(rng) - a * w->x;
    w->y += 2.0f * a * (w->x - w->y);
    return 1.7f * w->y; /* the one-pole leaves ~0.6 of the deviation: back to ~1 */
}

/* a bar u (0..1) through a master control m (-1..1): u^(8^-m), so m < 0 keeps only the highest bars high */
static float map_bar(float u, float pow_e)
{
    return u <= 1e-4f ? 0.0f : fm_exp2(pow_e * fm_log2(u));
}

/* ---------------- voice ---------------- */

static void voice_clear(flux_voice_t *v)
{
    for (int k = 0; k < FLUX_CH; k++) {
        flux_chan_t *c = &v->ch[k];
        for (int i = 0; i < FLUX_DELAY; i++)
            c->buf[i] = 0;
        c->ph = dsp_rand_unipolar(&v->rng);
        c->ic1 = c->ic2 = c->env = c->r = c->y = c->hx = c->hy = 0.0f;
        c->ng = 1.0f;
        c->imp_at = -1;
        c->imp = 0.0f;
        for (int w = 0; w < FLUX_W_COUNT; w++)
            c->w[w].x = c->w[w].y = 0.6f * dsp_rand_bipolar(&v->rng);
    }
    v->wr = 0;
    v->dc_xl = v->dc_yl = v->dc_xr = v->dc_yr = 0.0f;
    v->env = 0.0f;
    v->gain_prev = 0.0f;
    v->fresh = 1;
}

static void voice_start(void *eng, int vi, int note, float vel, int hard)
{
    flux_t *e = eng;
    flux_voice_t *v = &e->v[vi];
    const float vs = e->p[FLUX_P_VELOCITY];
    v->vel = 1.0f - vs + vs * vel;
    v->gate = 1;
    v->semi_target = (float)note;
    if (hard) { /* a new machine: empty loops, its own phases and places in the walks */
        v->rng = dsp_seed(0xF1A0u + (uint32_t)vi);
        voice_clear(v);
        v->semi = v->semi_target;
    }
}

static void voice_legato(void *eng, int vi, int note, float vel)
{
    flux_voice_t *v = &((flux_t *)eng)->v[vi];
    (void)vel;
    v->semi_target = (float)note;
}

static void voice_release(void *eng, int vi) { ((flux_t *)eng)->v[vi].gate = 0; }
static float voice_level(const void *eng, int vi) { return ((const flux_t *)eng)->v[vi].env; }
static void voice_silence(void *eng, int vi)
{
    flux_voice_t *v = &((flux_t *)eng)->v[vi];
    v->env = 0.0f;
    v->gate = 0;
}

static const vpoly_ops_t flux_poly_ops = {voice_start, voice_release, voice_level, voice_silence, voice_legato};

/* ---------------- engine_ops ---------------- */

static void flux_init(void *st, float sr)
{
    flux_t *e = st;
    e->sr = sr;
    for (int i = 0; i < FLUX_P_COUNT; i++)
        e->p[i] = flux_params[i].def;
    e->bend = 0.0f;
    e->nan_count = 0;
    for (int i = 0; i < FLUX_VOICES; i++) {
        flux_voice_t *v = &e->v[i];
        v->rng = dsp_seed(0xF1A0u + (uint32_t)i);
        voice_clear(v);
        for (int k = 0; k < FLUX_CH; k++) {
            flux_chan_t *c = &v->ch[k];
            c->inc = c->fb = c->flow = c->gl = c->gr = 0.0f;
            c->dly = 2.0f;
            c->pw = 0.5f;
            c->hc = 0.99f;
            c->a1 = 1.0f;
            c->a2 = c->a3 = c->m0 = c->m1 = 0.0f;
            c->m2 = 1.0f;
        }
        v->semi = v->semi_target = 60.0f;
        v->vel = 1.0f;
        v->gate = 0;
    }
    vpoly_init(&e->poly, FLUX_VOICES, sr, &flux_poly_ops, e);
    vpoly_set_mode(&e->poly, VPOLY_LEGATO);
}

static vpoly_t *flux_poly(void *st) { return &((flux_t *)st)->poly; }

static void flux_set_param(void *st, int id, float norm)
{
    flux_t *e = st;
    if (id >= 0 && id < FLUX_P_COUNT)
        e->p[id] = param_from_norm(&flux_params[id], norm);
}

static void flux_pitch_bend(void *st, float bend) { ((flux_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

/* two-sample polyBLEP residual of a unit step at phase 0, for a phase t and an increment dt (cycles) */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

static DSP_FAST_CODE void voice_render(flux_t *e, int vi, float *L, float *R)
{
    const int n = SYNTH_BLOCK;
    const float *p = e->p;
    flux_voice_t *v = &e->v[vi];
    const float sr = e->sr, inv_sr = 1.0f / sr, dt = (float)n * inv_sr, inv_n = 1.0f / (float)n;
    const int mode = (int)p[FLUX_P_MODE], ftype = (int)p[FLUX_P_FILTER_TYPE];
    const float *vm = vpoly_vmod(&e->poly, vi); /* per-voice modulation, microtuning, concert pitch, MPE */

    /* envelope (block rate): exponential towards an overshooting target, as an analog one */
    const float env0 = v->env;
    if (v->gate) {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 1.609f * dt / p[FLUX_P_ATTACK]); /* 0 -> 1 in `attack` */
        v->env = dsp_minf(v->env + (1.25f - v->env) * c, 1.0f);
    } else {
        const float c = 1.0f - fm_exp2(-FM_LOG2E * 3.932f * dt / p[FLUX_P_RELEASE]); /* 1 -> 0 in `release` */
        v->env = dsp_maxf(v->env - (v->env + 0.02f) * c, 0.0f);
    }

    /* glide of the whole machine */
    if (v->semi != v->semi_target) {
        v->semi += (v->semi_target - v->semi) * voice_glide_arrive_coef(p[FLUX_P_GLIDE], n, sr);
        if (dsp_absf(v->semi_target - v->semi) < 1e-3f)
            v->semi = v->semi_target;
    }
    const float semi = v->semi + e->bend * p[FLUX_P_BEND_RANGE] + vm[VM_PITCH];

    /* block-rate settings shared by the channels */
    const float e_pitch = fm_exp2(-3.0f * p[FLUX_P_MAP_PITCH]), e_cut = fm_exp2(-3.0f * p[FLUX_P_MAP_CUTOFF]);
    const float e_time = fm_exp2(-3.0f * p[FLUX_P_MAP_TIME]), e_flow = fm_exp2(-3.0f * p[FLUX_P_MAP_FLOW]);
    const float depth = p[FLUX_P_DEPTH], width = p[FLUX_P_WIDTH], norm = p[FLUX_P_NORMALIZE];
    const float kq = 2.0f - 1.94f * p[FLUX_P_RESO]; /* damping: 2 (none) .. 0.06 */
    const float fold = p[FLUX_P_FOLD], fold_drive = 0.25f * (1.0f + 5.0f * fold);
    const float cross = p[FLUX_P_CROSS];
    const float wa = dsp_minf(dt / (60.0f * fm_exp2(-8.2f * p[FLUX_P_MOTION])), 0.25f); /* walks: 60 s .. 0.2 s */
    const float wk = 2.449f * fm_exp2(0.5f * fm_log2(wa)); /* sqrt(6 a) */
    const float crackle = p[FLUX_P_CRACKLE];
    const float hit_p = crackle > 0.001f ? 0.2f * fm_exp2(9.0f * crackle) * dt : 0.0f; /* impulses per block */
    const float env_c = 1.0f - fm_exp2(-FM_LOG2E / (0.005f * sr));                     /* follower: 5 ms */
    const float flow_max = mode == FLUX_MODE_LOOP ? 4.0f : mode == FLUX_MODE_PRE ? 2.0f : 0.5f;
    const float dly_max = (float)(FLUX_DELAY - 4);

    float d_inc[FLUX_CH], d_dly[FLUX_CH], d_fb[FLUX_CH], d_flow[FLUX_CH], d_gl[FLUX_CH], d_gr[FLUX_CH], d_ng[FLUX_CH];
    for (int k = 0; k < FLUX_CH; k++) {
        flux_chan_t *c = &v->ch[k];
        float w[FLUX_W_COUNT];
        for (int j = 0; j < FLUX_W_COUNT; j++)
            w[j] = depth > 0.0f ? depth * walk_step(&c->w[j], &v->rng, wa, wk) : 0.0f;
        /* oscillator: 5 octaves below the note .. 4 above */
        const float up = map_bar(p[FLUX_CH_P(FLUX_C_PITCH, k)], e_pitch);
        const float inc = dsp_minf(fm_mtof(semi - 60.0f + 108.0f * up + 5.0f * w[FLUX_W_PITCH]) * inv_sr, 0.45f);
        c->pw = 0.03f + 0.47f * p[FLUX_CH_P(FLUX_C_PULSE, k)];
        /* delay: 0.04 .. 42 ms */
        const float ut = map_bar(p[FLUX_CH_P(FLUX_C_TIME, k)], e_time);
        const float dly = dsp_clampf(0.00004f * sr * fm_exp2(10.0f * ut + 0.3f * w[FLUX_W_TIME]), 1.5f, dly_max);
        /* filter: 30 Hz .. 17 kHz */
        const float uc = map_bar(p[FLUX_CH_P(FLUX_C_CUTOFF, k)], e_cut);
        const float fc = dsp_clampf(30.0f * fm_exp2(9.2f * uc + w[FLUX_W_CUTOFF]), 20.0f, 0.45f * sr);
        /* loop high-pass at an eighth of the loop's fundamental; its phase lead there (atan(1/8)) would sharpen the
         * resonance by 2 %: the delay is lengthened by as much (dly_comp) */
        c->hc = dsp_maxf(1.0f - 0.7854f / dly, 0.5f);
        const float g = fm_tan_pi(fc * inv_sr);
        c->a1 = 1.0f / (1.0f + g * (g + kq));
        c->a2 = g * c->a1;
        c->a3 = g * c->a2;
        c->m0 = ftype == 2 ? 1.0f : 0.0f;
        c->m1 = ftype == 1 ? kq : ftype == 2 ? -kq : 0.0f;
        c->m2 = ftype == 0 ? 1.0f : ftype == 2 ? -1.0f : 0.0f;
        /* feedback, flow, the regulator's gain towards its target */
        const float fb = dsp_clampf(p[FLUX_CH_P(FLUX_C_FEEDBACK, k)], 0.0f, 1.0f);
        const float uf = map_bar(p[FLUX_CH_P(FLUX_C_FLOW, k)], e_flow);
        const float flow = flow_max * uf * uf;
        const float want = dsp_minf(FLUX_TARGET / (c->env + 0.01f), 8.0f);
        const float ng = c->ng + 0.2f * (1.0f + norm * (want - 1.0f) - c->ng);
        /* level and place */
        const float lv = dsp_clampf(p[FLUX_CH_P(FLUX_C_LEVEL, k)] * (1.0f + 0.6f * w[FLUX_W_LEVEL]), 0.0f, 1.5f);
        const float side = (k & 1) ? 1.0f : -1.0f;
        const float pos = dsp_clampf(width * side * (0.25f + 0.25f * (float)(k >> 1)) + 0.4f * w[FLUX_W_PAN], -1.0f, 1.0f);
        float gl, gr;
        voice_pan(pos, &gl, &gr);
        gl *= lv, gr *= lv;
        if (v->fresh) {
            c->inc = inc, c->dly = dsp_minf(1.0198f * dly, dly_max), c->fb = fb, c->flow = flow, c->gl = gl, c->gr = gr;
            c->ng = 1.0f + norm * (want - 1.0f);
        }
        d_inc[k] = (inc - c->inc) * inv_n;
        const float dly_comp = dsp_minf(1.0198f * dly, dly_max);
        d_dly[k] = (dly_comp - c->dly) * inv_n;
        d_fb[k] = (fb - c->fb) * inv_n;
        d_flow[k] = (flow - c->flow) * inv_n;
        d_gl[k] = (gl - c->gl) * inv_n;
        d_gr[k] = (gr - c->gr) * inv_n;
        d_ng[k] = (v->fresh ? 0.0f : ng - c->ng) * inv_n;
        /* crackle: at most one impulse per channel and block */
        c->imp_at = -1;
        if (hit_p > 0.0f && dsp_rand_unipolar(&v->rng) < hit_p) {
            c->imp_at = (int)(dsp_rand_unipolar(&v->rng) * (float)n) & (n - 1);
            c->imp = 1.5f * dsp_rand_bipolar(&v->rng);
        }
    }
    v->fresh = 0;

    float xl[SYNTH_BLOCK], xr[SYNTH_BLOCK];
    const float dc = 1.0f - 125.7f * inv_sr; /* DC blocker (20 Hz): asymmetric loops can hold an offset */
    int wr = v->wr;
    for (int i = 0; i < n; i++) {
        wr = (wr + 1) & FLUX_MASK;
        float sl = 0.0f, sr_ = 0.0f;
        for (int k = 0; k < FLUX_CH; k++) {
            flux_chan_t *c = &v->ch[k];
            const float m = v->ch[(k + FLUX_CH - 1) & (FLUX_CH - 1)].y; /* the neighbour's output */
            c->inc += d_inc[k], c->dly += d_dly[k], c->fb += d_fb[k], c->flow += d_flow[k];
            c->gl += d_gl[k], c->gr += d_gr[k], c->ng += d_ng[k];

            /* oscillator */
            float inc = c->inc;
            if (mode == FLUX_MODE_LOOP)
                inc = dsp_minf(dsp_absf(inc * (1.0f + c->flow * m)), 0.45f); /* through zero: it never stalls */
            float ph = c->ph + inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            c->ph = ph;
            float x;
            if (mode == FLUX_MODE_PARA) {
                const float q = ph + ph - 1.0f;
                x = 4.0f * q * (1.0f - dsp_absf(q));
            } else {
                x = (ph < c->pw ? 1.0f : -1.0f) - (c->pw + c->pw - 1.0f); /* mean removed */
                if (inc > 1e-6f) {
                    float t2 = ph - c->pw;
                    if (t2 < 0.0f)
                        t2 += 1.0f;
                    x += blep(ph, inc) - blep(t2, inc);
                }
            }
            if (mode == FLUX_MODE_PRE) { /* amplitude flow, then the filter before the loop */
                x *= 1.0f + c->flow * m;
                const float v3 = x - c->ic2, v1 = c->a1 * c->ic1 + c->a2 * v3, v2 = c->ic2 + c->a2 * c->ic1 + c->a3 * v3;
                c->ic1 = v1 + v1 - c->ic1;
                c->ic2 = v2 + v2 - c->ic2;
                x = c->m0 * x + c->m1 * v1 + c->m2 * v2;
            }

            /* delay read */
            float dl = c->dly;
            if (mode == FLUX_MODE_PARA)
                dl = dsp_clampf(dl * (1.0f + c->flow * m), 1.5f, dly_max);
            const int di = (int)dl;
            const float df = dl - (float)di;
            const float s0 = (float)c->buf[(wr - di) & FLUX_MASK], s1 = (float)c->buf[(wr - di - 1) & FLUX_MASK];
            const float rd = (s0 + df * (s1 - s0)) * (1.0f / 32767.0f);
            float r = rd - c->hx + c->hc * c->hy; /* no DC round the loop */
            c->hx = rd;
            c->hy = r;

            /* in the loop: fold, regulate, filter */
            if (fold > 0.0f)
                r += fold * (fm_sin(fold_drive * r) - r);
            c->env += env_c * (dsp_absf(r) - c->env);
            r *= c->ng;
            if (mode == FLUX_MODE_LOOP) {
                const float v3 = r - c->ic2, v1 = c->a1 * c->ic1 + c->a2 * v3, v2 = c->ic2 + c->a2 * c->ic1 + c->a3 * v3;
                c->ic1 = v1 + v1 - c->ic1;
                c->ic2 = v2 + v2 - c->ic2;
                r = c->m0 * r + c->m1 * v1 + c->m2 * v2;
            }
            const float rn = v->ch[(k + 1) & (FLUX_CH - 1)].r; /* the next channel's loop */
            c->r = r;
            float w = FLUX_EXCITE * x + c->fb * (r + cross * (rn - r));
            if (i == c->imp_at)
                w += c->imp;
            w = fm_tanh(w);
            if (!(w >= -1.0f && w <= 1.0f)) { /* a NaN must not stay in the loop */
                w = 0.0f;
                c->ic1 = c->ic2 = c->env = c->r = c->hx = c->hy = 0.0f;
                e->nan_count++;
            }
            c->buf[wr] = (int16_t)(w * 32767.0f);
            c->y = w;
            sl += w * c->gl;
            sr_ += w * c->gr;
        }
        v->dc_yl = sl - v->dc_xl + dc * v->dc_yl;
        v->dc_xl = sl;
        v->dc_yr = sr_ - v->dc_xr + dc * v->dc_yr;
        v->dc_xr = sr_;
        xl[i] = v->dc_yl;
        xr[i] = v->dc_yr;
    }
    v->wr = wr;

    /* level: envelope, velocity, volume */
    const float vol = fm_db2lin(p[FLUX_P_VOLUME]);
    const float g1 = FLUX_GAIN * v->vel * vol * v->env * v->env;
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

static DSP_FAST_CODE void flux_render(void *st, float *L, float *R)
{
    flux_t *e = st;
    for (int vi = 0; vi < FLUX_VOICES; vi++)
        if (vpoly_active(&e->poly, vi))
            voice_render(e, vi, L, R);
}

/* the signal chain for interfaces (engine_stage_t): one channel drawn, the modulators below */
static const engine_stage_t flux_stages[] = {
    {"osc", "Oscillators", "source", "mode map_pitch pitch_", "", NULL, NULL, 0.0f, 0.0f},
    {"filter", "Filter", "filter", "filter_type resonance map_cutoff cutoff_", "osc", NULL, NULL, 0.0f, 0.0f},
    {"comb", "Delay", "filter", "map_time time_", "filter", NULL, NULL, 0.0f, 0.0f},
    {"loop", "Loop", "filter", "feedback_ normalize fold cross", "comb", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "level_ width amp_attack amp_release velocity volume", "loop", NULL, NULL, 0.0f, 0.0f},
    {"pulse", "Pulse", "mod", "pulse_", "", "osc", NULL, 0.0f, 0.0f},
    {"flow", "Flow", "mod", "map_flow flow_", "", "osc comb", NULL, 0.0f, 0.0f},
    {"crackle", "Crackle", "mod", "crackle", "", "loop", "crackle", 0.0f, 0.3f},
    {"motion", "Motion", "mod", "motion depth", "", "osc filter comb amp", NULL, 0.0f, 0.0f},
    {"voice", "Voice", "voice", "glide bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t flux_engine = {
    .name = "flux",
    .description = "feedback noise machine: eight channels (pulse or parabolic oscillator into a short feedback "
                   "delay with wavefolder, level regulator and filter in the loop) that modulate each other (flow) "
                   "and feed each other's loops (cross); four master controls reshape the eight bars of pitch, "
                   "cutoff, delay time and flow; crackle, slow random drift; from deep atmospheres to harsh noise "
                   "(1 voice, notes slide the machine)",
    .params = flux_params,
    .param_count = FLUX_P_COUNT,
    .voices = FLUX_VOICES,
    .init = flux_init,
    .poly = flux_poly,
    .set_param = flux_set_param,
    .pitch_bend = flux_pitch_bend,
    .render = flux_render,
    .stages = flux_stages,
    .stage_count = (int)(sizeof flux_stages / sizeof flux_stages[0]),
};
