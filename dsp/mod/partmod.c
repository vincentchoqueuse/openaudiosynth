/* partmod.c — modulation layer of a part (see partmod.h). */
#include "partmod.h"
#include "../core/fastmath.h"

static const char *const src_labels[] = {"none", "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7",
                                         "macro8", "mlfo1", "mlfo2", "mseg1", "mseg2", "mod_wheel", "aftertouch",
                                         "pitch_bend", "velocity", "note", "random", NULL};
static const char *const wave_labels[] = {"sine", "tri", "saw", "square", "sh", "fourier", NULL};
static const char *const mode_labels[] = {"oneshot", "loop_held", "loop_free", NULL};
static const char *const time_labels[] = {"seconds", "beats", NULL};
static const char *const vsrc_labels[] = {"none", "vmseg", "vlfo", "velocity", "note", "random", "pressure",
                                          "slide", NULL};
static const char *const vdst_labels[] = {"pitch", "level", "pan", "brightness", "timbre", NULL};
static const char *const point_labels[] = {"none", "1", "2", "3", "4", "5", "6", "7", "8",
                                           "9", "10", "11", "12", "13", "14", "15", "16", NULL};

#define MACRO(K) PARAM(PM_MACRO(K - 1), "macro" #K, "macro" #K ".value", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL, \
                       "macro knob " #K ": moves its targets (map*) and drives the routes whose source is macro" #K)
#define LFO(K, rate)                                                                                                \
    PARAM(PM_LFO(K - 1, PM_LFO_RATE), "mlfo" #K "_rate", "mlfo" #K ".rate", 0.01f, 50.0f, rate, PARAM_EXP, "Hz", 1,  \
          NULL, "part LFO " #K " rate when not synced"),                                                           \
    PARAM(PM_LFO(K - 1, PM_LFO_SYNC), "mlfo" #K "_sync", "mlfo" #K ".sync", 0.0f, (float)(TEMPO_DIV_COUNT - 1),     \
          0.0f, PARAM_INT, "", 0, tempo_div_labels, "off = free rate; a division = one cycle per division, locked to the beat"), \
    PARAM(PM_LFO(K - 1, PM_LFO_WAVE), "mlfo" #K "_wave", "mlfo" #K ".waveform", 0.0f, 5.0f, 0.0f, PARAM_INT, "", 0,  \
          wave_labels, "part LFO " #K " shape (bipolar; sh = random steps; fourier = its harmonics f1..f8)"),       \
    PARAM(PM_LFO(K - 1, PM_LFO_RETRIG), "mlfo" #K "_retrig", "mlfo" #K ".retrigger", 0.0f, 1.0f, 0.0f, PARAM_INT,  \
          "", 0, param_onoff_labels, "free LFO restarts when a note starts on a silent part")

/* Fourier coefficients of harmonic H of LFO L (named "<lfo>_a<H>", "<lfo>_b<H>"); default: b1 = 1 (a sine) */
#define FOURIER(L, NAME, H)                                                                                        \
    PARAM(PM_FOURIER(L, H - 1, 0), NAME "_a" #H, NAME ".harmonic" #H ".a", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,    \
          "Fourier shape: cosine coefficient of harmonic " #H),                                                     \
    PARAM(PM_FOURIER(L, H - 1, 1), NAME "_b" #H, NAME ".harmonic" #H ".b", -1.0f, 1.0f, H == 1 ? 1.0f : 0.0f, PARAM_LIN, \
          "", 1, NULL, "Fourier shape: sine coefficient of harmonic " #H)

#define PT(K, I, t, l)                                                                                               \
    PARAM(PM_MSEG_PT(K - 1, I - 1, PM_PT_TIME), "mseg" #K "_t" #I, "mseg" #K ".point" #I ".time", 0.0f, 32.0f, t,    \
          PARAM_LIN, "", 1, NULL, "time to reach point " #I " from the previous one (seconds or beats, see sync)"), \
    PARAM(PM_MSEG_PT(K - 1, I - 1, PM_PT_LEVEL), "mseg" #K "_l" #I, "mseg" #K ".point" #I ".level", 0.0f, 1.0f, l,   \
          PARAM_LIN, "", 1, NULL, "level of point " #I),                                                           \
    PARAM(PM_MSEG_PT(K - 1, I - 1, PM_PT_CURVE), "mseg" #K "_c" #I, "mseg" #K ".point" #I ".curve", -1.0f, 1.0f,     \
          0.0f, PARAM_LIN, "", 1, NULL, "curve of the segment ending at point " #I " (0 straight, + fast start, - slow start)")

#define MSEG(K)                                                                                                      \
    PARAM(PM_MSEG(K - 1, PM_MSEG_MODE), "mseg" #K "_mode", "mseg" #K ".mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0,    \
          mode_labels, "oneshot; loop_held = repeat loop_start..loop_end while a note is held; loop_free = always"), \
    PARAM(PM_MSEG(K - 1, PM_MSEG_SYNC), "mseg" #K "_sync", "mseg" #K ".sync", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0,    \
          time_labels, "point times in seconds or in beats (follows the tempo)"),                                  \
    PARAM(PM_MSEG(K - 1, PM_MSEG_POINTS), "mseg" #K "_points", "mseg" #K ".points", 2.0f, 16.0f, 4.0f, PARAM_INT, "",\
          0, NULL, "number of points used"),                                                                        \
    PARAM(PM_MSEG(K - 1, PM_MSEG_SUSTAIN), "mseg" #K "_sustain", "mseg" #K ".sustain", 0.0f, 16.0f, 3.0f, PARAM_INT, \
          "", 0, point_labels, "point held while a note is held (none = no hold)"),                                \
    PARAM(PM_MSEG(K - 1, PM_MSEG_LOOP_START), "mseg" #K "_loop_start", "mseg" #K ".loop_start", 0.0f, 16.0f, 0.0f,   \
          PARAM_INT, "", 0, point_labels, "first point of the loop"),                                              \
    PARAM(PM_MSEG(K - 1, PM_MSEG_LOOP_END), "mseg" #K "_loop_end", "mseg" #K ".loop_end", 0.0f, 16.0f, 0.0f,         \
          PARAM_INT, "", 0, point_labels, "last point of the loop"),                                               \
    PT(K, 1, 0.0f, 0.0f), PT(K, 2, 0.01f, 1.0f), PT(K, 3, 0.3f, 0.6f), PT(K, 4, 0.5f, 0.0f), PT(K, 5, 0.1f, 0.0f),     \
    PT(K, 6, 0.1f, 0.0f), PT(K, 7, 0.1f, 0.0f), PT(K, 8, 0.1f, 0.0f), PT(K, 9, 0.1f, 0.0f), PT(K, 10, 0.1f, 0.0f),     \
    PT(K, 11, 0.1f, 0.0f), PT(K, 12, 0.1f, 0.0f), PT(K, 13, 0.1f, 0.0f), PT(K, 14, 0.1f, 0.0f), PT(K, 15, 0.1f, 0.0f), \
    PT(K, 16, 0.1f, 0.0f)

#define ROUTE(R)                                                                                                     \
    PARAM(PM_ROUTE(R - 1, PM_R_SRC), "route" #R "_source", "route" #R ".source", 0.0f, (float)(PM_SRC_COUNT - 1),    \
          0.0f, PARAM_INT, "", 0, src_labels, "route " #R ": modulation source"),                                   \
    PARAM(PM_ROUTE(R - 1, PM_R_DST), "route" #R "_destination", "route" #R ".destination", 0.0f,                    \
          (float)PM_ENGINE_PARAMS, 0.0f, PARAM_INT, "", 0, NULL,                                                    \
          "route " #R ": the engine parameter modulated (by name in patches; must be realtime), none = 0"),        \
    PARAM(PM_ROUTE(R - 1, PM_R_AMOUNT), "route" #R "_amount", "route" #R ".amount", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", \
          1, NULL, "route " #R ": amount, in fractions of the destination's whole range (its knob travel)"),       \
    PARAM(PM_ROUTE(R - 1, PM_R_VIA), "route" #R "_via", "route" #R ".via", 0.0f, (float)(PM_SRC_COUNT - 1), 0.0f,    \
          PARAM_INT, "", 0, src_labels, "route " #R ": optional second source scaling the amount")

static const char *const macro_labels[] = {"none", "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7",
                                           "macro8", NULL};

/* A macro target: while its macro is set, the destination follows the macro from `from` (macro at 0) to `to`
 * (macro at 1), in fractions of the destination's range, so it moves along the destination's own curve (Hz
 * exponentially...). Several targets on one destination add their travel to the first one's `from`. Routes add
 * on top. In patches: the "macros" section (docs/patch_format.md), physical values. */
#define MAP(K)                                                                                                       \
    PARAM(PM_MAP(K - 1, PM_M_MACRO), "map" #K "_macro", "map" #K ".macro", 0.0f, (float)PM_MACROS, 0.0f, PARAM_INT,  \
          "", 0, macro_labels, "macro target " #K ": the macro that drives it (none = off)"),                      \
    PARAM(PM_MAP(K - 1, PM_M_DST), "map" #K "_destination", "map" #K ".destination", 0.0f, (float)PM_MAP_DST_MAX,    \
          0.0f, PARAM_INT, "", 0, NULL,                                                                             \
          "macro target " #K ": the parameter moved, an engine one or the master / an effect (by name in patches)"), \
    PARAM(PM_MAP(K - 1, PM_M_FROM), "map" #K "_from", "map" #K ".from", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,      \
          "macro target " #K ": the destination's value with the macro at 0, as a fraction of its range"),          \
    PARAM(PM_MAP(K - 1, PM_M_TO), "map" #K "_to", "map" #K ".to", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,            \
          "macro target " #K ": the destination's value with the macro at 1, as a fraction of its range")

#define VPT(I, t, l)                                                                                                \
    PARAM(PM_VMSEG_PT(I - 1, PM_PT_TIME), "vmseg_t" #I, "vmseg.point" #I ".time", 0.0f, 32.0f, t, PARAM_LIN, "", 1,   \
          NULL, "per-voice MSEG: time to reach point " #I " (seconds or beats)"),                                  \
    PARAM(PM_VMSEG_PT(I - 1, PM_PT_LEVEL), "vmseg_l" #I, "vmseg.point" #I ".level", 0.0f, 1.0f, l, PARAM_LIN, "", 1, \
          NULL, "per-voice MSEG: level of point " #I),                                                              \
    PARAM(PM_VMSEG_PT(I - 1, PM_PT_CURVE), "vmseg_c" #I, "vmseg.point" #I ".curve", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", \
          1, NULL, "per-voice MSEG: curve of the segment ending at point " #I)
#define VROUTE(R)                                                                                                    \
    PARAM(PM_VROUTE(R - 1, PM_VR_SRC), "vroute" #R "_source", "vroute" #R ".source", 0.0f, (float)(VM_SRC_COUNT - 1), \
          0.0f, PARAM_INT, "", 0, vsrc_labels, "per-voice route " #R ": source (each note has its own value)"),     \
    PARAM(PM_VROUTE(R - 1, PM_VR_DST), "vroute" #R "_destination", "vroute" #R ".destination", 0.0f,                 \
          (float)(VM_DST_COUNT - 1), 0.0f, PARAM_INT, "", 0, vdst_labels,                                           \
          "per-voice route " #R ": pitch (+/-12 st), level (+/-24 dB), pan, brightness (+/-4 oct), timbre"),       \
    PARAM(PM_VROUTE(R - 1, PM_VR_AMOUNT), "vroute" #R "_amount", "vroute" #R ".amount", -1.0f, 1.0f, 0.0f, PARAM_LIN, \
          "", 1, NULL, "per-voice route " #R ": amount")

#define MACRO_CC(K)                                                                                                  \
    PARAM(PM_MACRO_CC(K - 1), "macro" #K "_cc", "macro" #K ".cc", 0.0f, 119.0f, (float)(101 + K), PARAM_INT, "", 0,  \
          NULL, "MIDI CC that turns macro " #K " (0 = none; default 102..109, a free range)")

const param_desc_t partmod_params[PM_COUNT] = {
    MACRO(1), MACRO(2), MACRO(3), MACRO(4), MACRO(5), MACRO(6), MACRO(7), MACRO(8),
    LFO(1, 0.5f), LFO(2, 0.1f),
    MSEG(1), MSEG(2),
    ROUTE(1), ROUTE(2), ROUTE(3), ROUTE(4), ROUTE(5), ROUTE(6), ROUTE(7), ROUTE(8),
    PARAM(PM_VMSEG(PM_MSEG_MODE), "vmseg_mode", "vmseg.mode", 0.0f, 2.0f, 0.0f, PARAM_INT, "", 0, mode_labels,
          "per-voice MSEG (one per note, starts with the note): oneshot, loop_held, loop_free"),
    PARAM(PM_VMSEG(PM_MSEG_SYNC), "vmseg_sync", "vmseg.sync", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, time_labels,
          "per-voice MSEG times in seconds or beats"),
    PARAM(PM_VMSEG(PM_MSEG_POINTS), "vmseg_points", "vmseg.points", 2.0f, 16.0f, 4.0f, PARAM_INT, "", 0, NULL,
          "per-voice MSEG: number of points"),
    PARAM(PM_VMSEG(PM_MSEG_SUSTAIN), "vmseg_sustain", "vmseg.sustain", 0.0f, 16.0f, 3.0f, PARAM_INT, "", 0, point_labels,
          "per-voice MSEG: point held while the key is down"),
    PARAM(PM_VMSEG(PM_MSEG_LOOP_START), "vmseg_loop_start", "vmseg.loop_start", 0.0f, 16.0f, 0.0f, PARAM_INT, "", 0,
          point_labels, "per-voice MSEG: first point of the loop"),
    PARAM(PM_VMSEG(PM_MSEG_LOOP_END), "vmseg_loop_end", "vmseg.loop_end", 0.0f, 16.0f, 0.0f, PARAM_INT, "", 0,
          point_labels, "per-voice MSEG: last point of the loop"),
    VPT(1, 0.0f, 0.0f), VPT(2, 0.01f, 1.0f), VPT(3, 0.3f, 0.6f), VPT(4, 0.5f, 0.0f), VPT(5, 0.1f, 0.0f), VPT(6, 0.1f, 0.0f),
    VPT(7, 0.1f, 0.0f), VPT(8, 0.1f, 0.0f), VPT(9, 0.1f, 0.0f), VPT(10, 0.1f, 0.0f), VPT(11, 0.1f, 0.0f),
    VPT(12, 0.1f, 0.0f), VPT(13, 0.1f, 0.0f), VPT(14, 0.1f, 0.0f), VPT(15, 0.1f, 0.0f), VPT(16, 0.1f, 0.0f),
    PARAM(PM_VLFO(PM_VLFO_RATE), "vlfo_rate", "vlfo.rate", 0.01f, 50.0f, 5.0f, PARAM_EXP, "Hz", 1, NULL,
          "per-voice LFO rate (each note's LFO starts at its note-on)"),
    PARAM(PM_VLFO(PM_VLFO_SYNC), "vlfo_sync", "vlfo.sync", 0.0f, (float)(TEMPO_DIV_COUNT - 1), 0.0f, PARAM_INT, "", 0,
          tempo_div_labels, "per-voice LFO: off = free rate, or a tempo division"),
    PARAM(PM_VLFO(PM_VLFO_WAVE), "vlfo_wave", "vlfo.waveform", 0.0f, 5.0f, 0.0f, PARAM_INT, "", 0, wave_labels,
          "per-voice LFO shape (fourier = its harmonics f1..f8)"),
    VROUTE(1), VROUTE(2), VROUTE(3), VROUTE(4),
    MAP(1),  MAP(2),  MAP(3),  MAP(4),  MAP(5),  MAP(6),  MAP(7),  MAP(8),
    MAP(9),  MAP(10), MAP(11), MAP(12), MAP(13), MAP(14), MAP(15), MAP(16),
    MAP(17), MAP(18), MAP(19), MAP(20), MAP(21), MAP(22), MAP(23), MAP(24),
    MAP(25), MAP(26), MAP(27), MAP(28), MAP(29), MAP(30), MAP(31), MAP(32),
    MACRO_CC(1), MACRO_CC(2), MACRO_CC(3), MACRO_CC(4), MACRO_CC(5), MACRO_CC(6), MACRO_CC(7), MACRO_CC(8),
    FOURIER(0, "mlfo1", 1), FOURIER(0, "mlfo1", 2), FOURIER(0, "mlfo1", 3), FOURIER(0, "mlfo1", 4),
    FOURIER(0, "mlfo1", 5), FOURIER(0, "mlfo1", 6), FOURIER(0, "mlfo1", 7), FOURIER(0, "mlfo1", 8),
    FOURIER(1, "mlfo2", 1), FOURIER(1, "mlfo2", 2), FOURIER(1, "mlfo2", 3), FOURIER(1, "mlfo2", 4),
    FOURIER(1, "mlfo2", 5), FOURIER(1, "mlfo2", 6), FOURIER(1, "mlfo2", 7), FOURIER(1, "mlfo2", 8),
    FOURIER(2, "vlfo", 1), FOURIER(2, "vlfo", 2), FOURIER(2, "vlfo", 3), FOURIER(2, "vlfo", 4),
    FOURIER(2, "vlfo", 5), FOURIER(2, "vlfo", 6), FOURIER(2, "vlfo", 7), FOURIER(2, "vlfo", 8),
};
_Static_assert(VM_ROUTES == 4, "partmod_params lists 4 per-voice routes");

static void build_vcfg(partmod_t *m)
{
    voicemod_cfg_t *c = &m->vcfg;
    mseg_shape_t *s = &c->shape;
    s->count = (int)m->p[PM_VMSEG(PM_MSEG_POINTS)];
    s->mode = (int)m->p[PM_VMSEG(PM_MSEG_MODE)];
    s->sustain = (int)m->p[PM_VMSEG(PM_MSEG_SUSTAIN)] - 1;
    s->loop_start = (int)m->p[PM_VMSEG(PM_MSEG_LOOP_START)] - 1;
    s->loop_end = (int)m->p[PM_VMSEG(PM_MSEG_LOOP_END)] - 1;
    for (int i = 0; i < MSEG_POINTS; i++) {
        s->time[i] = m->p[PM_VMSEG_PT(i, PM_PT_TIME)];
        s->level[i] = m->p[PM_VMSEG_PT(i, PM_PT_LEVEL)];
        s->curve[i] = m->p[PM_VMSEG_PT(i, PM_PT_CURVE)];
    }
    c->lfo_div = (int)m->p[PM_VLFO(PM_VLFO_SYNC)];
    c->lfo_rate = c->lfo_div > 0 ? tempo_div_beats[c->lfo_div] : m->p[PM_VLFO(PM_VLFO_RATE)];
    c->lfo_wave = (int)m->p[PM_VLFO(PM_VLFO_WAVE)];
    for (int i = 0; i < 2 * PM_FOURIER_H; i++)
        c->fourier[i] = m->p[PM_FOURIER(PM_LFOS, 0, 0) + i];
    c->routes = 0;
    for (int r = 0; r < VM_ROUTES; r++) {
        c->src[r] = (int)m->p[PM_VROUTE(r, PM_VR_SRC)];
        c->dst[r] = (int)m->p[PM_VROUTE(r, PM_VR_DST)];
        c->amount[r] = m->p[PM_VROUTE(r, PM_VR_AMOUNT)];
        if (c->src[r] > 0 && c->amount[r] != 0.0f)
            c->routes++;
    }
    m->vcfg_dirty = 0;
}

void partmod_tuning(partmod_t *m, const float *offsets, int root)
{
    m->vcfg.tuning = offsets;
    m->vcfg.tuning_root = root;
}

void partmod_mpe(partmod_t *m, int on, float bend_range)
{
    m->vcfg.mpe = on;
    m->vcfg.mpe_bend_range = bend_range;
}

static void count_routes(partmod_t *m)
{
    m->n_routes = 0;
    for (int r = 0; r < PM_ROUTES; r++)
        if ((int)m->p[PM_ROUTE(r, PM_R_SRC)] > 0 && (int)m->p[PM_ROUTE(r, PM_R_DST)] > 0 && m->p[PM_ROUTE(r, PM_R_AMOUNT)] != 0.0f)
            m->n_routes++;
}

static void count_maps(partmod_t *m)
{
    m->n_maps = 0;
    for (int k = 0; k < PM_MAPS; k++)
        if ((int)m->p[PM_MAP(k, PM_M_MACRO)] > 0 && (int)m->p[PM_MAP(k, PM_M_DST)] > 0)
            m->n_maps++;
}

static void build_shapes(partmod_t *m)
{
    for (int k = 0; k < PM_MSEGS; k++) {
        mseg_shape_t *s = &m->shape[k];
        s->count = (int)m->p[PM_MSEG(k, PM_MSEG_POINTS)];
        s->mode = (int)m->p[PM_MSEG(k, PM_MSEG_MODE)];
        s->sustain = (int)m->p[PM_MSEG(k, PM_MSEG_SUSTAIN)] - 1; /* 1-based in the parameters, 0 = none */
        s->loop_start = (int)m->p[PM_MSEG(k, PM_MSEG_LOOP_START)] - 1;
        s->loop_end = (int)m->p[PM_MSEG(k, PM_MSEG_LOOP_END)] - 1;
        for (int i = 0; i < MSEG_POINTS; i++) {
            s->time[i] = m->p[PM_MSEG_PT(k, i, PM_PT_TIME)];
            s->level[i] = m->p[PM_MSEG_PT(k, i, PM_PT_LEVEL)];
            s->curve[i] = m->p[PM_MSEG_PT(k, i, PM_PT_CURVE)];
        }
    }
    m->shape_dirty = 0;
}

void partmod_init(partmod_t *m, float sample_rate, uint32_t seed)
{
    m->sr = sample_rate;
    m->rng = dsp_seed(seed);
    for (int i = 0; i < PM_COUNT; i++)
        m->p[i] = partmod_params[i].def;
    for (int i = 0; i < PM_SRC_COUNT; i++)
        m->src[i] = 0.0f;
    for (int k = 0; k < PM_LFOS; k++)
        m->lfo_phase[k] = m->lfo_sh[k] = 0.0f;
    build_shapes(m);
    for (int k = 0; k < PM_MSEGS; k++)
        mseg_reset(&m->mseg[k], &m->shape[k]);
    m->n_live = m->n_routes = m->n_maps = m->held = 0;
    for (int i = 0; i < PM_ENGINE_PARAMS; i++)
        m->base[i] = m->cur[i] = 0.0f, m->is_live[i] = 0;
    m->smooth_k = 1.0f - fm_exp2(-FM_LOG2E * (float)SYNTH_BLOCK / (PM_SMOOTH_S * sample_rate));
    m->vcfg.tuning = 0;
    m->vcfg.tuning_root = 0;
    m->vcfg.mpe = 0;
    m->vcfg.mpe_bend_range = 48.0f;
    m->vcfg.dt = (float)SYNTH_BLOCK / sample_rate;
    m->vcfg.beats_per_s = 2.0f;
    build_vcfg(m);
}

void partmod_engine(partmod_t *m, const engine_ops_t *ops)
{
    m->n_live = 0;
    m->held = 0;
    for (int i = 0; i < PM_ENGINE_PARAMS; i++) {
        m->base[i] = m->cur[i] = ops && i < ops->param_count ? param_to_norm(&ops->params[i], ops->params[i].def) : 0.0f;
        m->is_live[i] = 0;
    }
    /* a route's destination is an index into the previous engine's parameters: it means something else (or
     * nothing) in this one, so the routes lose their destination (sources and amounts stay) */
    for (int r = 0; r < PM_ROUTES; r++)
        m->p[PM_ROUTE(r, PM_R_DST)] = 0.0f;
    for (int k = 0; k < PM_MAPS; k++) /* the same for the macro targets */
        m->p[PM_MAP(k, PM_M_DST)] = 0.0f;
    count_routes(m);
    count_maps(m);
}

void partmod_set(partmod_t *m, int index, float norm)
{
    if (index < 0 || index >= PM_COUNT)
        return;
    m->p[index] = param_from_norm(&partmod_params[index], norm);
    if (index >= PM_MSEG(0, 0) && index < PM_ROUTE(0, 0))
        m->shape_dirty = 1;
    else if (index >= PM_MAP(0, 0) && index < PM_MACRO_CC(0))
        count_maps(m);
    else if (index >= PM_VBASE)
        m->vcfg_dirty = 1;
    else if (index >= PM_ROUTE(0, 0))
        count_routes(m);
}

static int make_live(partmod_t *m, int id)
{
    if (m->is_live[id])
        return 1;
    if (m->n_live >= PM_LIVE_MAX)
        return 0;
    m->live[m->n_live++] = (int16_t)id;
    m->is_live[id] = 1;
    return 1;
}

int partmod_base(partmod_t *m, const engine_ops_t *ops, int id, float norm, int sounding)
{
    if (!ops || id < 0 || id >= PM_ENGINE_PARAMS || id >= ops->param_count)
        return 0;
    m->base[id] = norm;
    if (m->is_live[id])
        return 1; /* already gliding or modulated: the block applies the new target */
    if (sounding && ops->params[id].realtime && make_live(m, id))
        return 1;
    m->cur[id] = norm; /* switches, or nothing sounding: at once */
    return 0;
}

void partmod_note_on(partmod_t *m, int note, float velocity)
{
    m->src[PM_SRC_VELOCITY] = velocity;
    m->src[PM_SRC_NOTE] = ((float)note - 60.0f) * (1.0f / 60.0f);
    m->src[PM_SRC_RANDOM] = dsp_rand_bipolar(&m->rng);
    if (m->held++ == 0) { /* first note on a silent part (legato: later notes do not retrigger) */
        if (m->shape_dirty)
            build_shapes(m);
        for (int k = 0; k < PM_MSEGS; k++)
            mseg_gate(&m->mseg[k], &m->shape[k], 1);
        for (int k = 0; k < PM_LFOS; k++)
            if ((int)m->p[PM_LFO(k, PM_LFO_RETRIG)])
                m->lfo_phase[k] = 0.0f;
    }
}

void partmod_note_off(partmod_t *m)
{
    if (m->held > 0 && --m->held == 0)
        for (int k = 0; k < PM_MSEGS; k++)
            mseg_gate(&m->mseg[k], &m->shape[k], 0);
}

void partmod_all_notes_off(partmod_t *m)
{
    if (m->held > 0) {
        m->held = 1;
        partmod_note_off(m);
    }
}

static float lfo_shape(int wave, float ph, float sh, const float *fourier)
{
    switch (wave) {
    case PM_WAVE_FOURIER: return voicemod_fourier(fourier, ph);
    case 1: return 1.0f - 4.0f * dsp_absf(ph - 0.5f);
    case 2: return 2.0f * ph - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return sh;
    default: return fm_sin(ph);
    }
}

void partmod_block(partmod_t *m, const tempo_t *t, int n, const engine_ops_t *ops, void *eng)
{
    if (m->shape_dirty)
        build_shapes(m);
    if (m->vcfg_dirty)
        build_vcfg(m);
    m->vcfg.dt = (float)n / m->sr;
    m->vcfg.beats_per_s = t->bpm * (1.0f / 60.0f);
    const float dt_s = (float)n / m->sr;
    /* sources */
    for (int k = 0; k < PM_MACROS; k++)
        m->src[PM_SRC_MACRO1 + k] = m->p[PM_MACRO(k)];
    for (int k = 0; k < PM_LFOS; k++) {
        const int div = (int)m->p[PM_LFO(k, PM_LFO_SYNC)];
        const float prev = m->lfo_phase[k];
        if (div > 0)
            m->lfo_phase[k] = fm_frac(t->beat / tempo_div_beats[div]);
        else
            m->lfo_phase[k] = fm_frac(m->lfo_phase[k] + m->p[PM_LFO(k, PM_LFO_RATE)] * dt_s);
        if (m->lfo_phase[k] < prev) /* new cycle */
            m->lfo_sh[k] = dsp_rand_bipolar(&m->rng);
        m->src[PM_SRC_LFO1 + k] = lfo_shape((int)m->p[PM_LFO(k, PM_LFO_WAVE)], m->lfo_phase[k], m->lfo_sh[k],
                                            &m->p[PM_FOURIER(k, 0, 0)]);
    }
    const float dt_beats = t->bpm * (1.0f / 60.0f) * dt_s;
    for (int k = 0; k < PM_MSEGS; k++)
        m->src[PM_SRC_MSEG1 + k] =
            mseg_advance(&m->mseg[k], &m->shape[k], (int)m->p[PM_MSEG(k, PM_MSEG_SYNC)] ? dt_beats : dt_s);
    if (!ops || (m->n_routes == 0 && m->n_live == 0 && m->n_maps == 0))
        return; /* nothing moving: the engine is untouched */

    /* macro targets, gathered per destination: the first one's `from`, plus each one's travel x its macro */
    float mval[PM_MAPS];
    int mdst[PM_MAPS], nm = 0;
    for (int k = 0; m->n_maps && k < PM_MAPS; k++) {
        const int mac = (int)m->p[PM_MAP(k, PM_M_MACRO)], d = (int)m->p[PM_MAP(k, PM_M_DST)] - 1;
        if (mac <= 0 || mac > PM_MACROS || d < 0 || d >= ops->param_count || !ops->params[d].realtime)
            continue;
        const float from = m->p[PM_MAP(k, PM_M_FROM)], to = m->p[PM_MAP(k, PM_M_TO)];
        int j = 0;
        while (j < nm && mdst[j] != d)
            j++;
        if (j == nm) {
            if (!make_live(m, d))
                continue;
            mdst[nm] = d, mval[nm++] = from;
        }
        mval[j] += (to - from) * m->p[PM_MACRO(mac - 1)];
    }

    /* route offsets, gathered per destination (destinations join the live list) */
    float off[PM_ROUTES];
    int dst[PM_ROUTES], nd = 0;
    for (int r = 0; r < PM_ROUTES; r++) {
        const int s = (int)m->p[PM_ROUTE(r, PM_R_SRC)], d = (int)m->p[PM_ROUTE(r, PM_R_DST)] - 1;
        const int via = (int)m->p[PM_ROUTE(r, PM_R_VIA)];
        const float amount = m->p[PM_ROUTE(r, PM_R_AMOUNT)];
        if (s <= 0 || s >= PM_SRC_COUNT || d < 0 || d >= ops->param_count || amount == 0.0f || !ops->params[d].realtime)
            continue;
        const float v = amount * m->src[s] * (via > 0 && via < PM_SRC_COUNT ? m->src[via] : 1.0f);
        int j = 0;
        while (j < nd && dst[j] != d)
            j++;
        if (j == nd) {
            if (!make_live(m, d))
                continue;
            dst[nd] = d, off[nd++] = 0.0f;
        }
        off[j] += v;
    }
    /* every live parameter glides to base + its modulation; settled, unmodulated ones leave the list */
    for (int a = 0; a < m->n_live;) {
        const int id = m->live[a];
        float target = m->base[id];
        int routed = 0;
        for (int j = 0; j < nm; j++)
            if (mdst[j] == id) /* a macro target replaces the base value */
                target = dsp_clampf(mval[j], 0.0f, 1.0f), routed = 1;
        for (int j = 0; j < nd; j++)
            if (dst[j] == id)
                target = dsp_clampf(target + off[j], 0.0f, 1.0f), routed = 1;
        float c = m->cur[id] + (target - m->cur[id]) * m->smooth_k;
        if (dsp_absf(target - c) < 1e-5f)
            c = target;
        if (c != m->cur[id]) {
            m->cur[id] = c;
            ops->set_param(eng, id, c);
        }
        if (!routed && c == target) {
            m->is_live[id] = 0;
            m->live[a] = m->live[--m->n_live];
            continue;
        }
        a++;
    }
}

int partmod_macro_cc(const partmod_t *m, int cc)
{
    for (int k = 0; cc > 0 && k < PM_MACROS; k++)
        if ((int)m->p[PM_MACRO_CC(k)] == cc)
            return k;
    return -1;
}

void partmod_global_targets(const partmod_t *m, float *val, uint8_t *set)
{
    for (int k = 0; m->n_maps && k < PM_MAPS; k++) {
        const int mac = (int)m->p[PM_MAP(k, PM_M_MACRO)], id = (int)m->p[PM_MAP(k, PM_M_DST)] - 1;
        if (mac <= 0 || mac > PM_MACROS || id < SYNTH_PARAM_GLOBAL || id >= PM_MAP_DST_MAX)
            continue;
        const int g = id - SYNTH_PARAM_GLOBAL;
        const float from = m->p[PM_MAP(k, PM_M_FROM)], to = m->p[PM_MAP(k, PM_M_TO)];
        if (!set[g])
            val[g] = from, set[g] = 1;
        val[g] += (to - from) * m->p[PM_MACRO(mac - 1)];
    }
}
