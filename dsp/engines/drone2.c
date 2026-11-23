/* drone2.c — drone field engine (see drone2.h). Per block: the notes' envelopes, the wells they dig, who owns which
 * well, a few partials looking for a better one (round robin), then each partial's motion (a glide toward its well
 * at `travel` seconds per octave, with inertia, flock alignment and a temperature wobble), the ecology and the
 * renewal; the audio loop interpolates every sine's frequency and level over the block.
 * A partial scores a free well by its weight over (1 + distance in octaves): the loud low harmonics of the loud
 * notes are filled first, and a partial prefers what is near. It leaves its well for one scoring twice as much. */
#include "drone2.h"
#include "../core/fastmath.h"

#define D2_GAIN 0.08f
#define D2_AMBITION 2.0f  /* a partial changes wells for one scoring this many times more */
#define D2_SCANS 4        /* partials looking for a better well per block */
#define D2_SILENT 1e-4f

const param_desc_t drone2_params[D2_P_COUNT] = {
    PARAM(D2_P_PARTIALS, "partials", "drone2.partials", 4.0f, 32.0f, 20.0f, PARAM_INT, "", 0, NULL,
          "how many partials live in the field (each owns one well: fewer than the wells leaves the weakest empty)"),
    PARAM(D2_P_HARMONICS, "harmonics", "drone2.harmonics", 4.0f, 24.0f, 16.0f, PARAM_INT, "", 0, NULL,
          "wells per note: its first harmonics"),
    PARAM(D2_P_BRIGHTNESS, "brightness", "drone2.brightness", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "tilt of the wells' weights: dark (high harmonics weak, often left empty) .. bright"),
    PARAM(D2_P_STRETCH, "stretch", "drone2.stretch", -0.02f, 0.06f, 0.0f, PARAM_LIN, "", 1, NULL,
          "inharmonicity of the wells: harmonic k at k^(1 + stretch) (negative: compressed, positive: bell-like)"),
    PARAM(D2_P_ODD, "odd", "drone2.odd", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "weakens the even harmonics: 1 = odd harmonics only, a hollow field"),
    PARAM(D2_P_TRAVEL, "travel", "drone2.travel", 0.5f, 60.0f, 8.0f, PARAM_EXP, "s", 1, NULL,
          "how long a partial takes to travel an octave toward its well: the speed of every migration"),
    PARAM(D2_P_TEMPERATURE, "temperature", "drone2.temperature", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "cold (0: exact, still partials) .. hot (1: each wobbles and sits off its well by up to 10 cents)"),
    PARAM(D2_P_RESTLESS, "restless", "drone2.restless", 0.0f, 1.0f, 0.1f, PARAM_LIN, "", 1, NULL,
          "how often a partial leaves its well for another free one on its own (0 = never, 1 = every few seconds)"),
    PARAM(D2_P_FLOCK, "flock", "drone2.flock", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "partials moving in schools: each takes on the speed of its neighbours"),
    PARAM(D2_P_ECOLOGY, "ecology", "drone2.ecology", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "competition between neighbouring partials: their levels rise and fall as one wins room over another"),
    PARAM(D2_P_RENEWAL, "renewal", "drone2.renewal", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "partials die and are reborn near another (0 = they live for ever, 1 = a life of about 8 s)"),
    PARAM(D2_P_BLOOM, "bloom", "drone2.bloom", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "from silence, the partials start in a tight cluster and unfold to their wells (0 = they start in place)"),
    PARAM(D2_P_WIDTH, "width", "drone2.width", 0.0f, 1.0f, 0.7f, PARAM_LIN, "", 1, NULL,
          "stereo spread of the partials"),
    PARAM(D2_P_ATTACK, "amp_attack", "amp.attack", 0.05f, 180.0f, 6.0f, PARAM_EXP, "s", 1, NULL,
          "fade in of a note's wells (up to 3 minutes)"),
    PARAM(D2_P_RELEASE, "amp_release", "amp.release", 0.05f, 180.0f, 10.0f, PARAM_EXP, "s", 1, NULL,
          "fade out of a note's wells (up to 3 minutes)"),
    PARAM(D2_P_VOLUME, "volume", "output.volume", -60.0f, 24.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "volume"),
    PARAM(D2_P_BEND_RANGE, "bend_range", "voice.bend_range", 0.0f, 24.0f, 2.0f, PARAM_INT, "st", 0, NULL,
          "pitch-bend range"),
};

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float absf(float x) { return x < 0.0f ? -x : x; }

/* ---------------- notes (vpoly voices) ---------------- */

static void note_start(void *eng, int vi, int note, float vel, int hard)
{
    drone2_note_t *n = &((drone2_t *)eng)->n[vi];
    n->note = note;
    n->vel = vel;
    n->gate = 1;
    if (hard)
        n->env = 0.0f;
}

static void note_release(void *eng, int vi) { ((drone2_t *)eng)->n[vi].gate = 0; }
static float note_level(const void *eng, int vi) { return ((const drone2_t *)eng)->n[vi].env; }
static void note_silence(void *eng, int vi)
{
    drone2_note_t *n = &((drone2_t *)eng)->n[vi];
    n->env = 0.0f;
    n->gate = 0;
}

static const vpoly_ops_t drone2_poly_ops = {note_start, note_release, note_level, note_silence, NULL};

/* ---------------- engine_ops ---------------- */

static void partial_reset(drone2_t *e, drone2_partial_t *q, int i)
{
    q->x = 8.0f;
    q->v = 0.0f;
    q->amp = q->amp_prev = 0.0f;
    q->phase = dsp_rand_unipolar(&e->rng);
    q->inc = 0.0f;
    q->gl = q->gr = 0.0f;
    q->pan = (i & 1 ? 1.0f : -1.0f) * (0.2f + 0.8f * dsp_rand_unipolar(&e->rng));
    q->detune = dsp_rand_bipolar(&e->rng);
    q->walk_x = q->walk_y = 0.0f;
    q->eco = 0.5f + 0.5f * dsp_rand_unipolar(&e->rng);
    q->life = 0.0f;
    q->well = -1;
    q->placed = 0;
    q->dying = 0;
}

static void drone2_init(void *st, float sr)
{
    drone2_t *e = st;
    e->sr = sr;
    for (int i = 0; i < D2_P_COUNT; i++)
        e->p[i] = drone2_params[i].def;
    e->bend = 0.0f;
    e->rng = dsp_seed(0xD2F1u);
    e->nan_count = 0;
    e->next = 0;
    e->gain_prev = 0.0f;
    for (int k = 0; k < D2_WELLS; k++)
        e->wc[k] = e->ww[k] = 0.0f, e->owner[k] = -1;
    for (int i = 0; i < D2_PARTIALS; i++) {
        partial_reset(e, &e->q[i], i);
        for (int j = 0; j < D2_PARTIALS; j++) /* antisymmetric: i beats j as much as j yields to i */
            e->asym[i][j] = j < i ? -e->asym[j][i] : (j == i ? 0.0f : 0.8f * dsp_rand_bipolar(&e->rng));
    }
    for (int vi = 0; vi < D2_NOTES; vi++) {
        e->n[vi].gate = 0, e->n[vi].note = 60;
        e->n[vi].vel = 1.0f, e->n[vi].env = 0.0f, e->n[vi].fade = 1.0f;
    }
    vpoly_init(&e->poly, D2_NOTES, sr, &drone2_poly_ops, e);
}

static vpoly_t *drone2_poly(void *st) { return &((drone2_t *)st)->poly; }

static void drone2_set_param(void *st, int id, float norm)
{
    drone2_t *e = st;
    if (id >= 0 && id < D2_P_COUNT)
        e->p[id] = param_from_norm(&drone2_params[id], norm);
}

static void drone2_pitch_bend(void *st, float bend) { ((drone2_t *)st)->bend = dsp_clampf(bend, -1.0f, 1.0f); }

/* the notes' envelopes (block rate, exponential as an analog one) and the wells they dig */
static void notes_and_wells(drone2_t *e, float dt)
{
    const float *p = e->p;
    const int nh = (int)p[D2_P_HARMONICS];
    const float tilt = lerpf(1.3f, 0.15f, p[D2_P_BRIGHTNESS]), stretch = 1.0f + p[D2_P_STRETCH];
    const float even = 1.0f - 0.97f * p[D2_P_ODD], top = fm_log2(0.45f * e->sr);
    const float ca = 1.0f - fm_exp2(-FM_LOG2E * 1.609f * dt / p[D2_P_ATTACK]);
    const float cr = 1.0f - fm_exp2(-FM_LOG2E * 3.932f * dt / p[D2_P_RELEASE]);
    for (int vi = 0; vi < D2_NOTES; vi++) {
        drone2_note_t *n = &e->n[vi];
        float *wc = &e->wc[vi * D2_HARM], *ww = &e->ww[vi * D2_HARM];
        if (!vpoly_active(&e->poly, vi)) {
            for (int k = 0; k < D2_HARM; k++)
                ww[k] = 0.0f;
            continue;
        }
        if (n->gate)
            n->env = dsp_minf(n->env + (1.25f - n->env) * ca, 1.0f);
        else
            n->env = dsp_maxf(n->env + (-0.02f - n->env) * cr, 0.0f);
        float g[SYNTH_BLOCK];
        n->fade = vpoly_fade(&e->poly, vi, g, SYNTH_BLOCK) ? g[SYNTH_BLOCK - 1] : 1.0f; /* a stolen note fades */
        const float *vm = vpoly_vmod(&e->poly, vi); /* microtuning, concert pitch, per-voice pitch */
        const float base = fm_log2(fm_mtof((float)n->note + vm[VM_PITCH]));
        const float level = n->env * n->fade * (0.4f + 0.6f * n->vel) * fm_db2lin(vm[VM_LEVEL]);
        for (int k = 0; k < D2_HARM; k++) {
            const float lk = fm_log2((float)(k + 1));
            wc[k] = base + stretch * lk;
            ww[k] = (k < nh && wc[k] < top && level > D2_SILENT)
                        ? level * fm_exp2(-tilt * lk) * ((k & 1) ? even : 1.0f) : 0.0f;
        }
        vpoly_end_block(&e->poly, vi, n->gate || n->env > 0.0f);
    }
}

static float score(const drone2_partial_t *q, float wc, float ww)
{
    if (!q->placed)
        return ww;
    const float d = q->x - wc;
    return ww / (1.0f + (d < 0.0f ? -d : d));
}

/* the best free well for partial i (other than its own) within `reach` octaves of it, -1 if none */
static int best_free(const drone2_t *e, int i, float reach, float *best)
{
    const drone2_partial_t *q = &e->q[i];
    int bk = -1;
    *best = 0.0f;
    for (int k = 0; k < D2_WELLS; k++) {
        if (e->ww[k] <= 0.0f || e->owner[k] >= 0 || (q->placed && absf(e->wc[k] - q->x) > reach))
            continue;
        const float s = score(q, e->wc[k], e->ww[k]);
        if (s > *best)
            *best = s, bk = k;
    }
    return bk;
}

static void take(drone2_t *e, int i, int k)
{
    drone2_partial_t *q = &e->q[i];
    if (q->well >= 0 && e->owner[q->well] == i)
        e->owner[q->well] = -1;
    q->well = k;
    e->owner[k] = (int8_t)i;
    if (!q->placed) { /* from silence: start in the cluster (bloom) or in place */
        const float fog = e->wc[(k / D2_HARM) * D2_HARM] + 2.2f + 0.12f * dsp_rand_bipolar(&e->rng);
        q->x = lerpf(e->wc[k], fog, e->p[D2_P_BLOOM]);
        q->v = 0.0f;
        q->placed = 1;
    }
}

/* ownership after the wells moved or vanished, then a few partials look for a better well */
static void assign(drone2_t *e, int count, float dt)
{
    for (int k = 0; k < D2_WELLS; k++)
        e->owner[k] = -1;
    for (int i = 0; i < D2_PARTIALS; i++) {
        drone2_partial_t *q = &e->q[i];
        if (q->well >= 0 && (i >= count || q->dying || e->ww[q->well] <= 0.0f || e->owner[q->well] >= 0))
            q->well = -1;
        if (q->well >= 0)
            e->owner[q->well] = (int8_t)i;
    }
    const float restless = e->p[D2_P_RESTLESS];
    const float p_move = restless * restless * 0.25f * dt * (float)count / (float)D2_SCANS; /* per scan */
    for (int s = 0; s < D2_SCANS; s++) {
        const int i = e->next;
        e->next = (e->next + 1) % count;
        drone2_partial_t *q = &e->q[i];
        if (q->dying)
            continue;
        float best;
        const int k = best_free(e, i, 16.0f, &best);
        if (k < 0)
            continue;
        if (q->well < 0)
            take(e, i, k);
        else {
            const float own = score(q, e->wc[q->well], e->ww[q->well]);
            float near;
            int kn;
            if (best > D2_AMBITION * own) /* a clearly better well: the chord changed */
                take(e, i, k);
            else if (dsp_rand_unipolar(&e->rng) < p_move && (kn = best_free(e, i, 0.5f, &near)) >= 0 &&
                     near > 0.3f * own) /* on its own: only to a free well nearby (within half an octave) */
                take(e, i, kn);
        }
    }
    /* homeless partials do not wait for their turn */
    for (int i = 0; i < count; i++) {
        drone2_partial_t *q = &e->q[i];
        float best;
        int k;
        if (q->well < 0 && !q->dying && (k = best_free(e, i, 16.0f, &best)) >= 0)
            take(e, i, k);
    }
}

static void motion(drone2_t *e, int count, float dt)
{
    const float *p = e->p;
    const float speed = 1.0f / p[D2_P_TRAVEL], inertia = dsp_minf(1.0f, dt / 0.3f);
    const float temp = p[D2_P_TEMPERATURE];
    const float wa = dt / lerpf(14.0f, 2.5f, temp), wk = 1.7320508f * fm_exp2(0.5f * fm_log2(2.0f * wa));
    const float flock = p[D2_P_FLOCK], eco = p[D2_P_ECOLOGY], renewal = p[D2_P_RENEWAL];
    float vbar[D2_PARTIALS];
    if (flock > 0.0f)
        for (int i = 0; i < count; i++) {
            float sw = 1e-6f, sv = 0.0f;
            for (int j = 0; j < count; j++) {
                if (j == i)
                    continue;
                const float d = (e->q[j].x - e->q[i].x) * (1.0f / 0.35f);
                const float w = e->q[j].amp / (1.0f + d * d);
                sw += w, sv += w * e->q[j].v;
            }
            vbar[i] = sv / sw;
        }
    for (int i = 0; i < count; i++) {
        drone2_partial_t *q = &e->q[i];
        float vd = 0.0f;
        if (q->well >= 0)
            vd = speed * fm_tanh(3.0f * (e->wc[q->well] + temp * q->detune * (10.0f / 1200.0f) - q->x));
        q->v += (vd - q->v) * inertia;
        if (flock > 0.0f)
            q->v += (vbar[i] - q->v) * dsp_minf(1.0f, 4.0f * flock * dt);
        q->x += q->v * dt;
        q->walk_x += wk * dsp_rand_bipolar(&e->rng) - wa * q->walk_x;
        q->walk_y += 2.0f * wa * (q->walk_x - q->walk_y);
    }
    if (eco > 0.0f) /* competitive Lotka-Volterra: alpha_ij = niche overlap (1 within ~1/6 octave) x (1 + asym) */
        for (int i = 0; i < count; i++) {
            drone2_partial_t *q = &e->q[i];
            float c = q->eco;
            for (int j = 0; j < count; j++) {
                if (j == i || e->q[j].well < 0)
                    continue;
                const float d = (e->q[j].x - q->x) * (1.0f / 0.15f);
                c += e->q[j].eco * (1.0f + e->asym[i][j]) / (1.0f + d * d);
            }
            q->eco = dsp_clampf(q->eco + (0.3f * q->eco * (1.0f - c) + 0.003f) * dt, 0.0f, 1.5f);
        }
    for (int i = 0; i < count; i++) {
        drone2_partial_t *q = &e->q[i];
        if (renewal > 0.0f && q->well >= 0) {
            if (q->life <= 0.0f) /* a life of ~3 min (renewal near 0) .. ~8 s (1), spread */
                q->life = 180.0f * fm_exp2(-4.5f * renewal) * (0.3f + 1.4f * dsp_rand_unipolar(&e->rng));
            else if ((q->life -= dt) <= 0.0f)
                q->dying = 1;
        }
        if (q->dying && q->amp < 0.002f) { /* reborn silent next to its nearest living neighbour */
            int j = -1;
            float dj = 1e9f;
            for (int m = 0; m < count; m++) {
                const float d = absf(e->q[m].x - q->x);
                if (m != i && e->q[m].well >= 0 && d < dj)
                    dj = d, j = m;
            }
            if (j >= 0)
                q->x = e->q[j].x + 0.05f * dsp_rand_bipolar(&e->rng);
            q->v = 0.0f;
            q->dying = 0;
            q->life = 0.0f;
        }
    }
}

static DSP_FAST_CODE void drone2_render(void *st, float *L, float *R)
{
    drone2_t *e = st;
    const float *p = e->p;
    const int n = SYNTH_BLOCK, count = (int)p[D2_P_PARTIALS];
    const float sr = e->sr, dt = (float)n / sr, inv_n = 1.0f / (float)n, inv_sr = 1.0f / sr;

    notes_and_wells(e, dt);
    assign(e, count, dt);
    motion(e, count, dt);

    /* levels: the well's weight (the note's envelope inside), the ecology; then the sines */
    const float eco = p[D2_P_ECOLOGY], width = p[D2_P_WIDTH], temp = p[D2_P_TEMPERATURE];
    const float a_amp = dsp_minf(1.0f, dt / 0.15f), fmax = 0.45f * sr;
    const float bend = e->bend * p[D2_P_BEND_RANGE] * (1.0f / 12.0f);
    float xl[SYNTH_BLOCK], xr[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        xl[i] = xr[i] = 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < D2_PARTIALS; i++) {
        drone2_partial_t *q = &e->q[i];
        float target = 0.0f;
        if (q->well >= 0 && !q->dying && i < count)
            target = e->ww[q->well] * (1.0f - eco + eco * dsp_minf(q->eco, 1.2f));
        q->amp_prev = q->amp;
        q->amp += (target - q->amp) * a_amp;
        if (q->amp < 1e-6f && target == 0.0f)
            q->amp = 0.0f;
        sum += q->amp;
        const float f = fm_exp2(q->x + bend + temp * 14.0f * 1.7f * q->walk_y * (1.0f / 1200.0f));
        const float amp = f < fmax ? q->amp : 0.0f;
        float pl, pr;
        voice_pan(q->pan * width, &pl, &pr);
        const float gl1 = amp * pl, gr1 = amp * pr, inc1 = dsp_minf(f, fmax) * inv_sr;
        if (gl1 == 0.0f && gr1 == 0.0f && q->gl == 0.0f && q->gr == 0.0f) {
            q->phase += (float)n * inc1;
            q->phase -= (float)(int)q->phase;
            q->inc = inc1;
            continue;
        }
        const float di = (inc1 - q->inc) * inv_n, dl = (gl1 - q->gl) * inv_n, dr = (gr1 - q->gr) * inv_n;
        float ph = q->phase, inc = q->inc, gl = q->gl, gr = q->gr;
        for (int s = 0; s < n; s++) {
            inc += di, gl += dl, gr += dr;
            ph += inc;
            ph -= (float)(int)ph;
            const float y = fm_sin_pos(ph);
            xl[s] += gl * y;
            xr[s] += gr * y;
        }
        q->phase = ph, q->inc = inc1, q->gl = gl1, q->gr = gr1;
    }
    /* back to silence: the next note blooms again */
    if (sum < D2_SILENT) {
        int held = 0;
        for (int vi = 0; vi < D2_NOTES; vi++)
            held |= vpoly_active(&e->poly, vi);
        if (!held)
            for (int i = 0; i < D2_PARTIALS; i++)
                e->q[i].placed = 0, e->q[i].well = -1, e->q[i].dying = 0, e->q[i].life = 0.0f;
    }

    /* fixed gain for the count (the partials do not add up coherently), interpolated over the block */
    const float g1 = D2_GAIN * fm_db2lin(p[D2_P_VOLUME]) * fm_exp2(-0.5f * fm_log2((float)count * 0.05f));
    dsp_guard_block(xl, n, &e->nan_count);
    dsp_guard_block(xr, n, &e->nan_count);
    const float dg = (g1 - e->gain_prev) * inv_n;
    float g = e->gain_prev;
    for (int s = 0; s < n; s++) {
        g += dg;
        L[s] += xl[s] * g;
        R[s] += xr[s] * g;
    }
    e->gain_prev = g1;
}

/* the signal chain for interfaces (engine_stage_t) */
static const engine_stage_t drone2_stages[] = {
    {"wells", "Wells", "source", "harmonics brightness stretch odd", "", NULL, NULL, 0.0f, 0.0f},
    {"partials", "Partials", "source", "partials bloom", "wells", NULL, NULL, 0.0f, 0.0f},
    {"amp", "Amp", "amp", "amp_attack amp_release volume width", "partials", NULL, NULL, 0.0f, 0.0f},
    {"behaviour", "Behaviour", "mod", "travel temperature restless flock ecology renewal", "", "partials", NULL,
     0.0f, 0.0f},
    {"voice", "Voice", "voice", "bend_range", "", NULL, NULL, 0.0f, 0.0f},
};

const engine_ops_t drone2_engine = {
    .name = "drone2",
    .description = "drone field: one field of sine partials that behave; the held notes dig wells (their harmonics), "
                   "each partial owns one and travels to it, so chords make the partials migrate; temperature, "
                   "travel, restlessness, flock, ecology, renewal, bloom; envelopes up to 3 minutes (8 notes)",
    .params = drone2_params,
    .param_count = D2_P_COUNT,
    .voices = D2_NOTES,
    .init = drone2_init,
    .poly = drone2_poly,
    .set_param = drone2_set_param,
    .pitch_bend = drone2_pitch_bend,
    .render = drone2_render,
    .stages = drone2_stages,
    .stage_count = (int)(sizeof drone2_stages / sizeof drone2_stages[0]),
};
