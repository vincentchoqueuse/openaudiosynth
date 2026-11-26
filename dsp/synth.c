/* synth.c — public API implementation: timestamped event queue, dispatch to the current engine
 * through engine_ops_t (no engine-specific code here), block splitting, effect bus, master
 * (smoothed gain, DC blocker, soft clipper). Each instance (struct synth) carries its state and its delay/reverb
 * memory; synth_default is a static one (g_synth + g_fx_mem). The engines' read-only data (wavetables) is one
 * static arena shared by every instance (g_engine_mem). The descriptor tables are in global_params.c. */
#include "synth.h"
#include "synth_probe.h"

#include "core/denormal.h"
#include "core/fastmath.h"
#include "core/smooth.h"
#include "engines/engine.h"
#include "engines/registry.h"
#include "filter/dcblock.h"
#include "filter/svf_tpt.h"
#include "fx/dist.h"
#include "fx/rack.h"
#include "fx/limiter.h"
#include "mod/partmod.h"
#include "mod/tempo.h"
#include "global_params.h"
#include "synth_debug.h"

/* Events waiting for the next block (power of 2). A whole sound pushed at once must fit: an engine (up to 165
 * parameters) plus the modulation of a part (225) plus notes; beyond it the newest events are dropped, except
 * the events that end sound, which have reserved slots. */
#ifndef SYNTH_EVQ_SIZE
#define SYNTH_EVQ_SIZE 1024 /* power of two; a target may lower it (16 bytes per event) */
#endif
_Static_assert((SYNTH_EVQ_SIZE & (SYNTH_EVQ_SIZE - 1)) == 0 && SYNTH_EVQ_SIZE >= 64, "SYNTH_EVQ_SIZE: power of two >= 64");
/* the last slots are kept for the events that end sound (note-off, sustain off, all notes / sound off): a burst
 * of parameters or notes can fill the queue, never leave a note hanging */
#define SYNTH_EVQ_RESERVE (SYNTH_EVQ_SIZE / 16)
#define SOFTCLIP_KNEE 0.5f /* linear up to -6 dBFS, then tanh up to 0 dBFS */

/* Effect memory (delay + reverb). A target overrides these and places the arena in a large RAM
 * (SDRAM / AXI SRAM) with DSP_LARGE_DATA. If it is too small, the reverb shrinks, then the delay. */
#ifndef SYNTH_FX_MAX_DELAY_S
#define SYNTH_FX_MAX_DELAY_S 2.0f
#endif
#ifndef SYNTH_FX_REVERB_SIZE
#define SYNTH_FX_REVERB_SIZE 1.5f
#endif
#ifndef SYNTH_FX_MEM_FLOATS
#define SYNTH_FX_MEM_FLOATS 320000 /* ~1.3 MB: 2 s stereo delay + size-1.5 plate at 48 kHz, the shimmer and tape lines */
#endif
/* Engine arena: read-only data of the engines that declare shared_floats (wavetables: ~11.8 MB with the
 * 9 factory tables of 32 frames). A board lowers it (fewer tables are built) and places it in SDRAM. */
#ifndef SYNTH_ENGINE_MEM_FLOATS
#define SYNTH_ENGINE_MEM_FLOATS 2965000
#endif
#ifndef DSP_LARGE_DATA
#define DSP_LARGE_DATA
#endif

enum { EV_MIDI = 0, EV_PARAM, EV_ENGINE };

typedef struct {
    uint32_t time; /* in samples, synth_render clock */
    uint8_t type, len, part;
    uint8_t data[3];
    int16_t id;
    float value;
} synth_event_t;

/* offsets in semitones from equal temperament, by degree above the root (computed from the exact ratios) */
static const float tuning_tables[4][12] = {
    {0.0000f, 0.1173f, 0.0391f, 0.1564f, -0.1369f, -0.0196f, -0.0978f, 0.0196f, 0.1369f, -0.1564f, 0.1760f, -0.1173f},
    {0.0000f, 0.1369f, 0.0391f, -0.0587f, 0.0782f, -0.0196f, 0.1173f, 0.0196f, -0.0782f, 0.0587f, -0.0391f, 0.0978f},
    {0.0000f, -0.2395f, -0.0684f, 0.1026f, -0.1369f, 0.0342f, -0.2053f, -0.0342f, 0.1369f, -0.1026f, 0.0684f, -0.1711f},
    {0.0000f, -0.0978f, -0.0782f, -0.0587f, -0.0977f, -0.0195f, -0.1173f, -0.0391f, -0.0782f, -0.1173f, -0.0391f, -0.0782f},
};

typedef struct {
    int engine; /* SYNTH_ENGINE_NONE = off */
    const engine_ops_t *ops;
    int switching, next_engine;
    float p[SYNTH_PART_COUNT]; /* mixer strip (physical values) */
    smooth_t gain;
    svf_t out_f[2];  /* the output filter, left and right */
    float out_oct;   /* its cutoff (log2 Hz), smoothed per block */
    int out_mode;    /* the mode it was last set to (a change resets it) */
    float crush_hold[2], crush_acc; /* crush: the samples held, the phase of the next one */
    float gain_prev;
    uint8_t sounding_note[128]; /* note sent to the engine for each received note (0xFF: none): transpose-safe note-offs */
    int16_t mpe_note[16];       /* MPE: note playing on each channel (-1: none) */
    float mpe_val[16][3];       /* MPE: last bend, pressure, slide received on each channel */
    partmod_t mod;              /* macros, LFOs, MSEGs, routes */
    const uint8_t *kit;         /* sample kit (synth_part_kit), NULL none */
    size_t kit_bytes;
    engine_state_t eng;
} synth_part_t;

struct synth {
    float sr;
    float *fx_mem; /* delay and reverb memory: SYNTH_FX_MEM_FLOATS */
    synth_part_t part[SYNTH_PARTS];

    synth_event_t evq[SYNTH_EVQ_SIZE];
    uint32_t ev_head, ev_count, ev_dropped;
    uint32_t time_served;   /* samples handed to the caller */
    uint32_t time_rendered; /* start of the next block to compute */

    float outL[SYNTH_BLOCK], outR[SYNTH_BLOCK];
    int out_pos;

    rack_t fx; /* the effect rack */
    tempo_t tempo;
    int delay_sync; /* division index, 0 = off */
    int tuning, tuning_root;
    float tune_cents[12], tuning_st[12];
    float a4_hz;
    float master_db;
    /* the master level and effects as last set (normalized): what a macro target returns them to */
    float gbase[SYNTH_GLOBAL_COUNT];
    float gapplied[SYNTH_GLOBAL_COUNT]; /* the value a macro gave them */
    uint8_t gheld[SYNTH_GLOBAL_COUNT];  /* 1 while a macro target holds them (master, effects, output filter) */
    int out_mode;                       /* output filter: 0 off, 1 lp, 2 hp, 3 bp */
    float out_cutoff, out_q, out_glide; /* Hz, Q, per-block glide of the cutoff */
    int crush_bits;                     /* 24 = off */
    float crush_rate;                   /* Hz, the sample rate itself = off */
    smooth_t master;
    float master_prev;
    dist_t sat[2];                      /* master saturation, per channel */
    int sat_mode, sat_run;              /* the mode chosen, and the one running (kept while fading out) */
    float sat_drive, sat_tone;
    float sat_mix, sat_mix_prev;        /* 1 while on: faded in and out over 8 blocks */
    limiter_t lim;
    int lim_on;
    float lim_mix, lim_mix_prev;        /* 1 while on: the limited signal, faded in and out over 8 blocks */
    float lim_gain, lim_ceiling, lim_release;
    dcblock_t dc[2];
    uint32_t nan_count;
};

static DSP_FAST_DATA synth_t g_synth;
static DSP_LARGE_DATA float g_fx_mem[SYNTH_FX_MEM_FLOATS];
static DSP_LARGE_DATA float g_engine_mem[SYNTH_ENGINE_MEM_FLOATS];
static int g_engine_mem_built; /* the shared data does not depend on the sample rate: built once */

/* Memory of engine id inside g_engine_mem (engines take consecutive slices; the last ones may get less). */
static float *engine_mem(int id, size_t *floats)
{
    size_t off = 0;
    for (int i = 0; i < id; i++)
        off += engine_registry[i]->shared_floats;
    const size_t want = engine_registry[id]->shared_floats;
    *floats = off >= SYNTH_ENGINE_MEM_FLOATS ? 0 : (want < SYNTH_ENGINE_MEM_FLOATS - off ? want : SYNTH_ENGINE_MEM_FLOATS - off);
    return g_engine_mem + (off < SYNTH_ENGINE_MEM_FLOATS ? off : 0);
}

/* ---------------- parts and engines ---------------- */

/* the engine of pt from its init: per-voice modulation, shared tables, kit */
static void engine_boot(synth_t *s, synth_part_t *pt)
{
    pt->ops->init(&pt->eng, s->sr);
    vpoly_set_vmod(pt->ops->poly(&pt->eng), &pt->mod.vcfg); /* per-voice modulation, microtuning, MPE */
    if (pt->ops->shared_attach) {
        size_t floats;
        float *mem = engine_mem(pt->engine, &floats);
        pt->ops->shared_attach(&pt->eng, mem, floats);
    }
    if (pt->ops->kit)
        pt->ops->kit(&pt->eng, pt->kit, pt->kit_bytes);
}

static void engine_start(synth_t *s, synth_part_t *pt, int id)
{
    for (int k = 0; k < 128; k++)
        pt->sounding_note[k] = 0xFF;
    pt->engine = id;
    if (id < 0) {
        pt->ops = 0;
        partmod_engine(&pt->mod, 0);
        return;
    }
    pt->ops = engine_registry[id];
    partmod_engine(&pt->mod, pt->ops);
    engine_boot(s, pt);
}

/* A part whose output was not finite (NaN or Inf: a numerical bug in an engine): its engine starts again from
 * a clean state with the values it had (the modulation layer keeps them), so the sound comes back at the next
 * note instead of the part, and the effects it feeds, staying broken until the next synth_init. */
static void part_recover(synth_t *s, synth_part_t *pt)
{
    for (int k = 0; k < 128; k++)
        pt->sounding_note[k] = 0xFF;
    engine_boot(s, pt);
    for (int i = 0; i < pt->ops->param_count && i < PM_ENGINE_PARAMS; i++)
        pt->ops->set_param(&pt->eng, i, pt->mod.cur[i]);
    s->nan_count++;
}

static void part_init(synth_t *s, int k)
{
    synth_part_t *pt = &s->part[k];
    for (int i = 0; i < SYNTH_PART_COUNT; i++)
        pt->p[i] = synth_part_param_info(i)->def;
    pt->p[SYNTH_PART_CHANNEL - SYNTH_PART_PARAM] = k == 0 ? 0.0f : (float)(k + 1);
    smooth_init(&pt->gain, 0.005f, s->sr / (float)SYNTH_BLOCK, 1.0f);
    pt->gain_prev = 1.0f;
    for (int c = 0; c < 2; c++)
        svf_init(&pt->out_f[c], s->sr);
    pt->out_mode = 0;
    pt->out_oct = 11.0f;
    pt->crush_hold[0] = pt->crush_hold[1] = 0.0f;
    pt->crush_acc = 1.0f;
    pt->switching = 0;
    partmod_init(&pt->mod, s->sr, 0x5EED0u + (uint32_t)k);
    pt->kit = 0;
    pt->kit_bytes = 0;
    for (int c = 0; c < 16; c++)
        pt->mpe_note[c] = -1, pt->mpe_val[c][0] = pt->mpe_val[c][1] = pt->mpe_val[c][2] = 0.0f;
    engine_start(s, pt, k == 0 ? SYNTH_ENGINE_VA : SYNTH_ENGINE_NONE);
    pt->next_engine = pt->engine;
}

static inline float part_p(const synth_part_t *pt, int id) { return pt->p[id - SYNTH_PART_PARAM]; }

/* Does part pt take this channel voice message? */
static int part_listens(const synth_part_t *pt, int channel)
{
    const int ch = (int)part_p(pt, SYNTH_PART_CHANNEL);
    return pt->ops && !pt->switching && (ch == 0 || ch == channel + 1 || part_p(pt, SYNTH_PART_MPE) > 0.5f);
}

static void part_notes_off(synth_part_t *pt)
{
    vpoly_all_notes_off(pt->ops->poly(&pt->eng));
    for (int k = 0; k < 128; k++)
        pt->sounding_note[k] = 0xFF;
    for (int c = 0; c < 16; c++) /* MPE: no channel owns a note any more */
        pt->mpe_note[c] = -1;
    partmod_all_notes_off(&pt->mod);
}

/* MPE expression on a member channel (2..16): goes to the note playing there. what: -1 bend, or VM_SRC_*. */
/* Sets the base value of an engine parameter (0..1): a realtime one glides to it through partmod_block, which
 * also adds its modulation; the others reach the engine at once. */
static void part_engine_param(synth_part_t *pt, int id, float value)
{
    if (!pt->ops || id < 0 || id >= pt->ops->param_count)
        return;
    const int sounding = vpoly_active_count(pt->ops->poly(&pt->eng)) > 0;
    if (!partmod_base(&pt->mod, pt->ops, id, value, sounding))
        pt->ops->set_param(&pt->eng, id, value);
}

static void mpe_expression(synth_part_t *pt, int channel, int what, float value)
{
    pt->mpe_val[channel][what < 0 ? 0 : what == VM_SRC_PRESSURE ? 1 : 2] = value;
    if (pt->mpe_note[channel] >= 0)
        vpoly_expression(pt->ops->poly(&pt->eng), pt->mpe_note[channel], what, value);
}

static void part_midi(synth_part_t *pt, int channel, int type, int d1, int d2, int len)
{
    const int mpe = part_p(pt, SYNTH_PART_MPE) > 0.5f && channel > 0; /* channel 1 stays global */
    const float v = (float)d2 * (1.0f / 127.0f);
    const engine_ops_t *o = pt->ops;
    vpoly_t *poly = o->poly(&pt->eng);

    switch (type) {
    case 0x90:
        if (len < 3)
            return;
        if (d2 > 0) {
            if (d1 < (int)part_p(pt, SYNTH_PART_KEY_LO) || d1 > (int)part_p(pt, SYNTH_PART_KEY_HI))
                return;
            const int note = d1 + (int)part_p(pt, SYNTH_PART_TRANSPOSE);
            if (note < 0 || note > 127)
                return;
            if (pt->sounding_note[d1] != 0xFF) {
                if (pt->sounding_note[d1] != note)
                    vpoly_note_off(poly, pt->sounding_note[d1]); /* transposition changed while held */
                partmod_note_off(&pt->mod);
            }
            pt->sounding_note[d1] = (uint8_t)note;
            partmod_note_on(&pt->mod, note, v);
            vpoly_note_on(poly, note, v);
            if (mpe) { /* the expression sent before the note belongs to it */
                pt->mpe_note[channel] = (int16_t)note;
                vpoly_expression(poly, note, -1, pt->mpe_val[channel][0]);
                vpoly_expression(poly, note, VM_SRC_PRESSURE, pt->mpe_val[channel][1]);
                vpoly_expression(poly, note, VM_SRC_SLIDE, pt->mpe_val[channel][2]);
            }
            return;
        }
        /* velocity 0 = note-off */
        /* fallthrough */
    case 0x80:
        if (len >= 3 && pt->sounding_note[d1] != 0xFF) {
            if (mpe && pt->mpe_note[channel] == pt->sounding_note[d1])
                pt->mpe_note[channel] = -1;
            vpoly_note_off(poly, pt->sounding_note[d1]);
            pt->sounding_note[d1] = 0xFF;
            partmod_note_off(&pt->mod);
        }
        return;
    case 0xB0:
        if (len < 3)
            return;
        if (mpe && d1 == 74) { /* MPE slide */
            mpe_expression(pt, channel, VM_SRC_SLIDE, v);
            return;
        }
        switch (d1) {
        case 1:
            partmod_midi_source(&pt->mod, PM_SRC_WHEEL, v);
            if (o->mod_wheel)
                o->mod_wheel(&pt->eng, v);
            return;
        case 64: vpoly_sustain(poly, d2 >= 64); return;
        case 120: vpoly_all_sound_off(poly); return;
        case 123: part_notes_off(pt); return;
        default: {
            const int mac = partmod_macro_cc(&pt->mod, d1); /* a macro's CC (macro<k>_cc) comes first */
            if (mac >= 0) {
                partmod_set(&pt->mod, PM_MACRO(mac), v);
                return;
            }
        }
            for (int i = 0; i < o->cc_count; i++) /* the engine's declared CC map: like a parameter set */
                if (o->cc_map[i].cc == d1)
                    part_engine_param(pt, o->cc_map[i].param, v);
            return;
        }
    case 0xE0:
        if (len >= 3) {
            const float bend = (float)(((d2 << 7) | d1) - 8192) * (1.0f / 8192.0f); /* 14 bits */
            if (mpe) {
                mpe_expression(pt, channel, -1, bend);
                return;
            }
            partmod_midi_source(&pt->mod, PM_SRC_BEND, bend);
            o->pitch_bend(&pt->eng, bend);
        }
        return;
    case 0xD0: /* channel pressure (MPE: the note's pressure) */
        if (mpe) {
            mpe_expression(pt, channel, VM_SRC_PRESSURE, (float)d1 * (1.0f / 127.0f));
            return;
        }
        partmod_midi_source(&pt->mod, PM_SRC_AFTERTOUCH, (float)d1 * (1.0f / 127.0f));
        return;
    case 0xA0: /* polyphonic pressure: the part follows the last key that moved */
        if (len >= 3)
            partmod_midi_source(&pt->mod, PM_SRC_AFTERTOUCH, v);
        return;
    default:
        return; /* program change: later */
    }
}

static void global_set(synth_t *s, int id, float norm);

static void handle_midi(synth_t *s, const uint8_t *m, int len, uint32_t time)
{
    switch (m[0]) { /* real-time: transport and clock */
    case 0xF8: tempo_clock(&s->tempo, time); return;
    case 0xFA: tempo_start(&s->tempo); return;
    case 0xFB: tempo_continue(&s->tempo); return;
    case 0xFC: tempo_stop(&s->tempo); return;
    default: break;
    }
    if (m[0] >= 0xF0)
        return;
    const int type = m[0] & 0xF0, channel = m[0] & 0x0F;
    const int d1 = len > 1 ? (m[1] & 0x7F) : 0;
    const int d2 = len > 2 ? (m[2] & 0x7F) : 0;
    const float v = (float)d2 * (1.0f / 127.0f);
    if (type == 0xB0 && len >= 3) { /* effect-bus CCs act once, whatever the channel */
        switch (d1) {
        case 69: global_set(s, SYNTH_PARAM_REVERB_FREEZE, d2 >= 64 ? 1.0f : 0.0f); return;
        case 91: global_set(s, SYNTH_PARAM_REVERB_MIX, v); return;
        case 93:
            if (s->fx.p[RACK_P_CHORUS_MODE] < 0.5f && d2 > 0) /* turning the chorus up switches it on */
                global_set(s, SYNTH_PARAM_CHORUS_MODE, 0.75f /* I+II */);
            global_set(s, SYNTH_PARAM_CHORUS_MIX, v);
            return;
        case 94: global_set(s, SYNTH_PARAM_DELAY_MIX, v); return;
        default: break;
        }
    }
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (part_listens(&s->part[k], channel))
            part_midi(&s->part[k], channel, type, d1, d2, len);
}

static void part_set(synth_part_t *pt, int id, float value)
{
    if (id >= SYNTH_PART_PARAM && id < SYNTH_PART_PARAM_END) {
        const int i = id - SYNTH_PART_PARAM;
        const float old_ch = pt->p[SYNTH_PART_CHANNEL - SYNTH_PART_PARAM];
        pt->p[i] = param_from_norm(synth_part_param_info(i), value);
        if (pt->ops && (id == SYNTH_PART_CHANNEL ? pt->p[i] != old_ch : id == SYNTH_PART_MUTE && pt->p[i] > 0.5f))
            part_notes_off(pt); /* no stuck notes when re-routed or muted */
        partmod_mpe(&pt->mod, part_p(pt, SYNTH_PART_MPE) > 0.5f, part_p(pt, SYNTH_PART_MPE_BEND));
    } else if (id >= SYNTH_MOD_PARAM && id < SYNTH_MOD_PARAM + PM_COUNT) {
        partmod_set(&pt->mod, id - SYNTH_MOD_PARAM, value);
    } else if (id >= 0 && id < SYNTH_PARAM_GLOBAL) {
        part_engine_param(pt, id, value);
    }
}

/* ---------------- microtuning ---------------- */

static void tuning_apply(synth_t *s)
{
    /* concert pitch as one offset on every degree (log2 once per change, never per sample) */
    const float a4_st = s->a4_hz == 440.0f ? 0.0f : 12.0f * fm_log2(s->a4_hz * (1.0f / 440.0f));
    for (int k = 0; k < 12; k++)
        s->tuning_st[k] = a4_st + (s->tuning == 5   ? 0.01f * s->tune_cents[k]
                                   : s->tuning > 0 ? tuning_tables[s->tuning - 1][k]
                                                   : 0.0f);
    const int plain = s->tuning == 0 && a4_st == 0.0f; /* equal at 440: no offset at all (bit-exact plain path) */
    for (int k = 0; k < SYNTH_PARTS; k++)
        partmod_tuning(&s->part[k].mod, plain ? 0 : s->tuning_st, s->tuning_root);
}

static void tuning_set(synth_t *s, int id, float v)
{
    if (id == SYNTH_PARAM_TUNING)
        s->tuning = (int)v;
    else if (id == SYNTH_PARAM_TUNING_ROOT)
        s->tuning_root = (int)v;
    else if (id == SYNTH_PARAM_A4_HZ)
        s->a4_hz = v;
    else
        s->tune_cents[id - SYNTH_PARAM_TUNE_0] = v;
    tuning_apply(s);
}

/* ---------------- event queue ---------------- */

/* an event that ends sound: note-off (or note-on velocity 0), sustain pedal up, all sound / notes off */
static int ev_ends_sound(const synth_event_t *ev)
{
    if (ev->type != EV_MIDI || ev->len < 3)
        return 0;
    const uint8_t st = ev->data[0] & 0xF0u;
    return st == 0x80u || (st == 0x90u && ev->data[2] == 0) ||
           (st == 0xB0u && ((ev->data[1] == 64 && ev->data[2] < 64) || ev->data[1] == 120 || ev->data[1] == 123));
}

static void ev_push(synth_t *s, const synth_event_t *ev)
{
    const uint32_t room = ev_ends_sound(ev) ? SYNTH_EVQ_SIZE : SYNTH_EVQ_SIZE - SYNTH_EVQ_RESERVE;
    if (s->ev_count >= room) {
        s->ev_dropped++;
        return;
    }
    s->evq[(s->ev_head + s->ev_count) & (SYNTH_EVQ_SIZE - 1)] = *ev;
    s->ev_count++;
}

static int any_switching(const synth_t *s)
{
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (s->part[k].switching)
            return 1;
    return 0;
}

/* the master level or an effect parameter (id SYNTH_PARAM_MASTER_DB .. the effect bus), normalized */
static void global_apply(synth_t *s, int id, float norm)
{
    if (id == SYNTH_PARAM_MASTER_DB)
        s->master_db = param_from_norm(synth_global_param(SYNTH_PARAM_MASTER_DB - SYNTH_PARAM_GLOBAL), norm);
    else if (id == SYNTH_PARAM_OUT_FILTER)
        s->out_mode = (int)param_from_norm(synth_global_param(SYNTH_PARAM_OUT_FILTER - SYNTH_PARAM_GLOBAL), norm);
    else if (id == SYNTH_PARAM_OUT_CUTOFF)
        s->out_cutoff = param_from_norm(synth_global_param(SYNTH_PARAM_OUT_CUTOFF - SYNTH_PARAM_GLOBAL), norm);
    else if (id == SYNTH_PARAM_CRUSH_BITS)
        s->crush_bits = (int)param_from_norm(synth_global_param(SYNTH_PARAM_CRUSH_BITS - SYNTH_PARAM_GLOBAL), norm);
    else if (id == SYNTH_PARAM_CRUSH_RATE)
        s->crush_rate = param_from_norm(synth_global_param(SYNTH_PARAM_CRUSH_RATE - SYNTH_PARAM_GLOBAL), norm);
    else if (id == SYNTH_PARAM_OUT_RESO)
        s->out_q = 0.5f * fm_exp2(4.3f * param_from_norm(synth_global_param(SYNTH_PARAM_OUT_RESO - SYNTH_PARAM_GLOBAL), norm)); /* 0.5 .. ~10 */
    else if (id >= SYNTH_PARAM_SATURATION && id <= SYNTH_PARAM_SATURATION_TONE) {
        const float v = param_from_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), norm);
        if (id == SYNTH_PARAM_SATURATION)
            s->sat_mode = (int)v;
        else if (id == SYNTH_PARAM_SATURATION_DRIVE)
            s->sat_drive = v;
        else
            s->sat_tone = v;
        static const dist_mode_t modes[4] = {DIST_OFF, DIST_SOFT, DIST_TUBE, DIST_HARD};
        if (s->sat_mode > 0)
            s->sat_run = dsp_clampi(s->sat_mode, 1, 3);
        s->sat_mix = s->sat_mode > 0 ? 1.0f : 0.0f;
        for (int c = 0; c < 2; c++)
            dist_set(&s->sat[c], modes[s->sat_run], s->sat_drive, s->sat_tone, 1.0f);
    } else if (id >= SYNTH_PARAM_LIMITER && id <= SYNTH_PARAM_LIMITER_RELEASE) {
        const float v = param_from_norm(synth_global_param(id - SYNTH_PARAM_GLOBAL), norm);
        if (id == SYNTH_PARAM_LIMITER)
            s->lim_on = (int)v;
        else if (id == SYNTH_PARAM_LIMITER_GAIN)
            s->lim_gain = v;
        else if (id == SYNTH_PARAM_LIMITER_CEILING)
            s->lim_ceiling = v;
        else
            s->lim_release = v;
        s->lim_mix = s->lim_on ? 1.0f : 0.0f;
        limiter_set(&s->lim, s->lim_gain, s->lim_ceiling, s->lim_release);
    } else
        rack_set_param(&s->fx, rack_index(id), norm);
}

/* a new value for the master or an effect (event, effect CC): kept as the base, applied unless a macro holds it */
static void global_set(synth_t *s, int id, float norm)
{
    s->gbase[id - SYNTH_PARAM_GLOBAL] = norm;
    if (!s->gheld[id - SYNTH_PARAM_GLOBAL])
        global_apply(s, id, norm);
}

/* Macro targets and routes on the master and the effects (several parts or macros add their travel; routes add
 * their offset, an LFO or an MSEG moving an effect): applied once per block on top of the value last set, which
 * comes back when nothing moves it any more. */
static void global_targets(synth_t *s)
{
    enum { G = SYNTH_GLOBAL_COUNT };
    float val[G], off[G];
    uint8_t set[G] = {0}, routed[G] = {0};
    for (int g = 0; g < G; g++)
        off[g] = 0.0f;
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (s->part[k].ops)
            partmod_global_targets(&s->part[k].mod, val, set, off, routed);
    for (int g = 0; g < G; g++) {
        if (set[g] || routed[g]) {
            const float want = dsp_clampf((set[g] ? val[g] : s->gbase[g]) + off[g], 0.0f, 1.0f);
            if (!s->gheld[g] || want != s->gapplied[g])
                global_apply(s, SYNTH_PARAM_GLOBAL + g, want);
            s->gapplied[g] = want;
            s->gheld[g] = 1;
        } else if (s->gheld[g]) { /* released: back to the value last set */
            global_apply(s, SYNTH_PARAM_GLOBAL + g, s->gbase[g]);
            s->gheld[g] = 0;
        }
    }
}

/* Applies, in arrival order, the events due before the end of the block starting at t0. */
static void set_onset(synth_t *s, int at)
{
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (s->part[k].ops && s->part[k].ops->poly)
            s->part[k].ops->poly(&s->part[k].eng)->onset = at;
}

static void ev_process(synth_t *s, uint32_t t0)
{
    while (s->ev_count > 0 && !any_switching(s)) {
        const synth_event_t *ev = &s->evq[s->ev_head];
        if ((int32_t)(ev->time - t0) >= SYNTH_BLOCK)
            break; /* not due yet: keep the order */
        synth_part_t *pt = &s->part[ev->part];
        switch (ev->type) {
        case EV_MIDI: {
            /* the event's sample in this block, for the engines that start notes on it (vpoly_t.onset) */
            const int32_t at = (int32_t)(ev->time - t0);
            set_onset(s, at > 0 ? (int)at : 0);
            handle_midi(s, ev->data, ev->len, ev->time);
            set_onset(s, 0);
            break;
        }
        case EV_PARAM:
            if (pm_global_target(ev->id))
                global_set(s, ev->id, ev->value);
            else if (ev->id == SYNTH_PARAM_TEMPO)
                tempo_set_bpm(&s->tempo, param_from_norm(synth_global_param(SYNTH_PARAM_TEMPO - SYNTH_PARAM_GLOBAL), ev->value));
            else if (ev->id == SYNTH_PARAM_CLOCK)
                tempo_set_external(&s->tempo, (int)param_from_norm(synth_global_param(SYNTH_PARAM_CLOCK - SYNTH_PARAM_GLOBAL), ev->value));
            else if (ev->id == SYNTH_PARAM_DELAY_SYNC)
                s->delay_sync = (int)param_from_norm(synth_global_param(SYNTH_PARAM_DELAY_SYNC - SYNTH_PARAM_GLOBAL), ev->value);
            else if (ev->id >= SYNTH_PARAM_TUNING && ev->id < SYNTH_PARAM_GLOBAL_END)
                tuning_set(s, ev->id, param_from_norm(synth_global_param(ev->id - SYNTH_PARAM_GLOBAL), ev->value));
            else
                part_set(pt, ev->id, ev->value);
            break;
        case EV_ENGINE:
            if (ev->id != pt->engine) {
                if (pt->ops && vpoly_active_count(pt->ops->poly(&pt->eng)) > 0) {
                    pt->switching = 1; /* fade during this block, see render_block */
                    pt->next_engine = ev->id;
                } else {
                    engine_start(s, pt, ev->id);
                }
            }
            break;
        default:
            break;
        }
        s->ev_head = (s->ev_head + 1) & (SYNTH_EVQ_SIZE - 1);
        s->ev_count--;
    }
}

/* ---------------- master ---------------- */

static inline float softclip(float x)
{
    const float a = dsp_absf(x);
    if (a <= SOFTCLIP_KNEE)
        return x;
    const float y = SOFTCLIP_KNEE + (1.0f - SOFTCLIP_KNEE) * fm_tanh((a - SOFTCLIP_KNEE) * (1.0f / (1.0f - SOFTCLIP_KNEE)));
    return x < 0.0f ? -y : y;
}

/* The saturation of the mix (dist_t, made for a voice's typical peak of 0.7) at the master's nominal peak of
 * 0.25 (-12 dBFS): scaled up into it and back down. Faded in and out over 8 blocks (its oversampling delays the
 * signal); it runs only while on or fading. */
#define SAT_SCALE 2.8f
#define MASTER_FADE_STEP 0.125f /* the saturation and the limiter fade in and out over 8 blocks */

/* the fade of a master stage one block further toward its target (0 off, 1 on) */
static inline float fade_step(float m0, float target)
{
    return target > m0 ? dsp_minf(m0 + MASTER_FADE_STEP, target) : dsp_maxf(m0 - MASTER_FADE_STEP, target);
}

/* the processed signal y (times post) mixed into L and R by a fade ramping from m0 to m1 over the block */
static inline void fade_into(float *L, float *R, const float *yl, const float *yr, float post, float m0, float m1, int n)
{
    const float dm = (m1 - m0) / (float)n;
    for (int i = 0; i < n; i++) {
        const float m = m0 + dm * (float)(i + 1);
        L[i] += (yl[i] * post - L[i]) * m;
        R[i] += (yr[i] * post - R[i]) * m;
    }
}

static void master_saturate(synth_t *s, float *L, float *R, int n)
{
    const float m0 = s->sat_mix_prev, m1 = fade_step(m0, s->sat_mix);
    s->sat_mix_prev = m1;
    if (m0 <= 0.0f && m1 <= 0.0f)
        return;
    if (m0 <= 0.0f)
        dist_reset(&s->sat[0]), dist_reset(&s->sat[1]);
    float yl[SYNTH_BLOCK], yr[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        yl[i] = L[i] * SAT_SCALE, yr[i] = R[i] * SAT_SCALE;
    dist_process(&s->sat[0], yl, yl, n);
    dist_process(&s->sat[1], yr, yr, n);
    fade_into(L, R, yl, yr, 1.0f / SAT_SCALE, m0, m1, n);
}

/* The limiter, faded in and out over 8 blocks (about 5 ms) when switched, since it delays the signal by its
 * look-ahead; it runs only while on or fading, and starts empty. */
static void master_limit(synth_t *s, float *L, float *R, int n)
{
    const float m0 = s->lim_mix_prev, m1 = fade_step(m0, s->lim_mix);
    s->lim_mix_prev = m1;
    if (m0 <= 0.0f && m1 <= 0.0f)
        return;
    if (m0 <= 0.0f)
        limiter_reset(&s->lim);
    if (m0 >= 1.0f && m1 >= 1.0f) {
        limiter_process(&s->lim, L, R, n);
        return;
    }
    float yl[SYNTH_BLOCK], yr[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        yl[i] = L[i], yr[i] = R[i];
    limiter_process(&s->lim, yl, yr, n);
    fade_into(L, R, yl, yr, 1.0f, m0, m1, n);
}

/* 1 if every sample of the stereo block is finite: x * 0 is 0 for any finite x, NaN for NaN and Inf */
static inline int block_finite(const float *L, const float *R, int n)
{
    float z = 0.0f;
    for (int i = 0; i < n; i++)
        z += L[i] * 0.0f + R[i] * 0.0f;
    return z == 0.0f;
}

/* The effects and the master filters got a non-finite value: emptied (delay lines, reverb), settings kept. */
static void fx_recover(synth_t *s)
{
    rack_recover(&s->fx, s->fx_mem, SYNTH_FX_MEM_FLOATS, SYNTH_FX_MAX_DELAY_S, SYNTH_FX_REVERB_SIZE);
    dcblock_reset(&s->dc[0]);
    dcblock_reset(&s->dc[1]);
    dist_reset(&s->sat[0]);
    dist_reset(&s->sat[1]);
    limiter_reset(&s->lim);
    s->nan_count++;
}

/* Renders part pt and mixes it: send x signal into (busL, busR), (1 - send) x signal into (dryL, dryR).
 * Returns 1 if something went around the bus. */
static DSP_FAST_CODE int part_render(synth_t *s, synth_part_t *pt, float *busL, float *busR, float *dryL, float *dryR)
{
    const int n = SYNTH_BLOCK;
    float L[SYNTH_BLOCK], R[SYNTH_BLOCK];
    for (int i = 0; i < n; i++)
        L[i] = R[i] = 0.0f;
    partmod_block(&pt->mod, &s->tempo, n, pt->ops, &pt->eng);
    if (pt->ops->tempo)
        pt->ops->tempo(&pt->eng, s->tempo.bpm, s->tempo.beat);
    pt->ops->render(&pt->eng, L, R);
    if (!block_finite(L, R, n)) { /* every build: one bad voice must not silence the synth for good */
        for (int i = 0; i < n; i++)
            L[i] = R[i] = 0.0f;
        part_recover(s, pt);
    }
    if (pt->switching) {
        /* fade the old engine out over this block, then switch (the effects keep their tails) */
        dsp_ramp_mul(L, 1.0f, 0.0f, n);
        dsp_ramp_mul(R, 1.0f, 0.0f, n);
        engine_start(s, pt, pt->next_engine);
        pt->switching = 0;
    }
    /* output filter: the same setting on every part, each with its own state; the cutoff glides (about 5 ms) */
    if (s->out_mode > 0) {
        if (pt->out_mode != s->out_mode) {
            static const svf_mode_t mode[4] = {SVF_LP, SVF_LP, SVF_HP, SVF_BP};
            for (int c = 0; c < 2; c++) {
                svf_reset(&pt->out_f[c]);
                svf_set_mode(&pt->out_f[c], mode[s->out_mode]);
            }
            pt->out_mode = s->out_mode;
            pt->out_oct = fm_log2(s->out_cutoff);
        }
        pt->out_oct += (fm_log2(s->out_cutoff) - pt->out_oct) * s->out_glide;
        const float fc = dsp_minf(fm_exp2(pt->out_oct), 0.45f * s->sr);
        for (int c = 0; c < 2; c++)
            svf_set(&pt->out_f[c], fc, s->out_q);
        svf_process(&pt->out_f[0], L, L, n);
        svf_process(&pt->out_f[1], R, R, n);
    } else {
        pt->out_mode = 0;
    }
    /* crush: sample and hold at crush_rate, then the amplitude rounded to crush_bits (deliberately aliasing) */
    const int crush_rate_on = s->crush_rate < 47900.0f; /* the top of its range is off, whatever the sample rate */
    if (s->crush_bits < 24 || crush_rate_on) {
        const float step = crush_rate_on ? dsp_minf(s->crush_rate / s->sr, 1.0f) : 1.0f;
        /* the steps span the instruments' nominal level (-12 dBFS), not full scale: a few bits stay audible */
        const float q = (float)(1 << (dsp_clampi(s->crush_bits, 2, 24) - 1)) * 4.0f, iq = 1.0f / q;
        for (int i = 0; i < n; i++) {
            pt->crush_acc += step;
            if (pt->crush_acc >= 1.0f) {
                pt->crush_acc -= 1.0f;
                pt->crush_hold[0] = L[i], pt->crush_hold[1] = R[i];
                if (s->crush_bits < 24)
                    for (int c = 0; c < 2; c++) { /* round to the nearest step */
                        const float v = pt->crush_hold[c] * q;
                        pt->crush_hold[c] = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) * iq;
                    }
            }
            L[i] = pt->crush_hold[0], R[i] = pt->crush_hold[1];
        }
    }
    /* level (smoothed, mute = silent) and balance: centre keeps both sides at gain 1 */
    const float target = part_p(pt, SYNTH_PART_MUTE) > 0.5f ? 0.0f : fm_db2lin(part_p(pt, SYNTH_PART_LEVEL));
    const float g0 = pt->gain_prev, g1 = smooth_next(&pt->gain, target);
    pt->gain_prev = g1;
    const float pan = part_p(pt, SYNTH_PART_PAN);
    const float bl = pan > 0.0f ? 1.0f - pan : 1.0f, br = pan < 0.0f ? 1.0f + pan : 1.0f;
    const float send = part_p(pt, SYNTH_PART_FX_SEND);
    const float dg = (g1 - g0) / (float)n;
    float g = g0;
    if (send >= 1.0f) {
        for (int i = 0; i < n; i++) {
            g += dg;
            busL[i] += L[i] * (g * bl);
            busR[i] += R[i] * (g * br);
        }
        return 0;
    }
    const float dry = 1.0f - send;
    for (int i = 0; i < n; i++) {
        g += dg;
        const float l = L[i] * (g * bl), r = R[i] * (g * br);
        busL[i] += send * l, busR[i] += send * r;
        dryL[i] += dry * l, dryR[i] += dry * r;
    }
    return 1;
}

static DSP_FAST_CODE void render_block(synth_t *s)
{
    const int n = SYNTH_BLOCK;
    float *L = s->outL, *R = s->outR;
    float dryL[SYNTH_BLOCK], dryR[SYNTH_BLOCK];

    ev_process(s, s->time_rendered);
    global_targets(s);
    tempo_advance(&s->tempo, n);
    if (s->delay_sync > 0) { /* echo time follows the tempo */
        const float sec = dsp_clampf(tempo_div_seconds(&s->tempo, s->delay_sync), 0.02f, 2.0f);
        if (dsp_absf(sec - s->fx.p[RACK_P_DELAY_TIME]) > 1e-4f)
            rack_set_param(&s->fx, RACK_P_DELAY_TIME, param_to_norm(&rack_params[RACK_P_DELAY_TIME], sec));
    }

    for (int i = 0; i < n; i++)
        L[i] = R[i] = dryL[i] = dryR[i] = 0.0f;
    int has_dry = 0;
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (s->part[k].ops)
            has_dry |= part_render(s, &s->part[k], L, R, dryL, dryR);

    rack_process(&s->fx, L, R, n);
    if (has_dry)
        for (int i = 0; i < n; i++)
            L[i] += dryL[i], R[i] += dryR[i];
    if (!block_finite(L, R, n)) { /* the parts are checked: an effect went wrong on its own */
        for (int i = 0; i < n; i++)
            L[i] = R[i] = 0.0f;
        fx_recover(s);
    }

    const float g0 = s->master_prev;
    const float g1 = smooth_next(&s->master, fm_db2lin(s->master_db));
    s->master_prev = g1;
    dsp_ramp_mul(L, g0, g1, n);
    dsp_ramp_mul(R, g0, g1, n);

    master_saturate(s, L, R, n);
    dcblock_process(&s->dc[0], L, L, n);
    dcblock_process(&s->dc[1], R, R, n);
    master_limit(s, L, R, n);
    for (int i = 0; i < n; i++) {
        L[i] = softclip(L[i]);
        R[i] = softclip(R[i]);
    }
    dsp_guard_block(L, n, &s->nan_count);
    dsp_guard_block(R, n, &s->nan_count);

    s->time_rendered += SYNTH_BLOCK;
}

/* ---------------- diagnostics (synth_debug.h) ---------------- */

void *synth_debug_engine_state(synth_t *s, int part)
{
    return part >= 0 && part < SYNTH_PARTS && s->part[part].ops ? (void *)&s->part[part].eng : 0;
}

/* the master's guard, plus each part's voices (counted in its vpoly by the voice guard) */
uint32_t synth_debug_nan_count(const synth_t *s)
{
    uint32_t n = s->nan_count;
    for (int k = 0; k < SYNTH_PARTS; k++)
        if (s->part[k].ops)
            n += s->part[k].ops->poly((void *)&s->part[k].eng)->nan_count;
    return n;
}

/* ---------------- public API ---------------- */

#define SYNTH_STATE_BYTES ((sizeof(synth_t) + 15u) & ~(size_t)15u)

size_t synth_size(void) { return SYNTH_STATE_BYTES + sizeof(float) * SYNTH_FX_MEM_FLOATS; }

synth_t *synth_create(void *mem, size_t bytes, float sample_rate)
{
    if (!mem || ((uintptr_t)mem & 15u) || bytes < synth_size())
        return 0;
    synth_t *s = mem;
    s->fx_mem = (float *)((uint8_t *)mem + SYNTH_STATE_BYTES);
    synth_init(s, sample_rate);
    return s;
}

synth_t *synth_default(void)
{
    g_synth.fx_mem = g_fx_mem;
    return &g_synth;
}

void synth_init(synth_t *s, float sample_rate)
{
    dsp_denormals_off();
    s->sr = sample_rate;
    s->out_glide = 1.0f - fm_exp2(-FM_LOG2E * (float)SYNTH_BLOCK / (0.005f * sample_rate)); /* cutoff glide, 5 ms */
    if (!g_engine_mem_built) {
        for (int id = 0; id < SYNTH_ENGINE_COUNT; id++)
            if (engine_registry[id]->shared_build) {
                size_t floats;
                float *mem = engine_mem(id, &floats);
                engine_registry[id]->shared_build(mem, floats);
            }
        g_engine_mem_built = 1;
    }
    for (int k = 0; k < SYNTH_PARTS; k++)
        part_init(s, k);
    s->ev_head = s->ev_count = s->ev_dropped = 0;
    s->time_served = s->time_rendered = 0;
    s->out_pos = SYNTH_BLOCK; /* empty buffer */
    rack_init(&s->fx, sample_rate, s->fx_mem, SYNTH_FX_MEM_FLOATS, SYNTH_FX_MAX_DELAY_S, SYNTH_FX_REVERB_SIZE);
    tempo_init(&s->tempo, sample_rate, synth_global_param(SYNTH_PARAM_TEMPO - SYNTH_PARAM_GLOBAL)->def);
    s->delay_sync = 0;
    s->tuning = s->tuning_root = 0;
    for (int k = 0; k < 12; k++)
        s->tune_cents[k] = 0.0f;
    s->a4_hz = 440.0f;
    tuning_apply(s);
    s->master_db = synth_global_param(SYNTH_PARAM_MASTER_DB - SYNTH_PARAM_GLOBAL)->def;
    for (int c = 0; c < 2; c++)
        dist_init(&s->sat[c], sample_rate);
    limiter_init(&s->lim, sample_rate);
    s->lim_mix = s->lim_mix_prev = 0.0f;
    s->sat_run = 1;
    s->sat_mix = s->sat_mix_prev = 0.0f;
    for (int g = 0; g < SYNTH_GLOBAL_COUNT; g++) {
        const param_desc_t *d = synth_global_param(g);
        s->gbase[g] = param_to_norm(d, d->def);
        s->gapplied[g] = s->gbase[g];
        s->gheld[g] = 0;
        if (pm_global_target(SYNTH_PARAM_GLOBAL + g) && g > RACK_P_BUS_COUNT)
            global_apply(s, SYNTH_PARAM_GLOBAL + g, s->gbase[g]); /* the output filter's defaults */
    }
    smooth_init(&s->master, 0.005f, sample_rate / (float)SYNTH_BLOCK, fm_db2lin(s->master_db));
    s->master_prev = s->master.y;
    dcblock_init(&s->dc[0], sample_rate);
    dcblock_init(&s->dc[1], sample_rate);
    s->nan_count = 0;
}

void synth_part_engine(synth_t *s, int part, synth_engine_t engine)
{
    if (part < 0 || part >= SYNTH_PARTS || (int)engine < SYNTH_ENGINE_NONE || (int)engine >= SYNTH_ENGINE_COUNT)
        return;
    synth_event_t ev = {0};
    ev.type = EV_ENGINE;
    ev.part = (uint8_t)part;
    ev.id = (int16_t)engine;
    ev.time = s->time_served;
    ev_push(s, &ev);
}

void synth_part_kit(synth_t *s, int part, const void *kit, size_t bytes)
{
    if (part < 0 || part >= SYNTH_PARTS)
        return;
    synth_part_t *pt = &s->part[part];
    pt->kit = kit;
    pt->kit_bytes = kit ? bytes : 0;
    if (pt->ops && pt->ops->kit)
        pt->ops->kit(&pt->eng, pt->kit, pt->kit_bytes);
}

void synth_set_engine(synth_t *s, synth_engine_t engine)
{
    if (engine != SYNTH_ENGINE_NONE)
        synth_part_engine(s, 0, engine);
}

void synth_midi(synth_t *s, const uint8_t *msg, int len, int sample_offset)
{
    if (!msg || len < 1 || len > 3 || msg[0] < 0x80)
        return;
    synth_event_t ev = {0};
    ev.type = EV_MIDI;
    ev.len = (uint8_t)len;
    for (int i = 0; i < len; i++)
        ev.data[i] = msg[i];
    ev.time = s->time_served + (uint32_t)(sample_offset > 0 ? sample_offset : 0);
    ev_push(s, &ev);
}

void synth_part_param(synth_t *s, int part, int id, float value)
{
    /* unknown ids and NaN are ignored here: the id is stored in 16 bits, and a NaN would reach integer casts */
    if (part < 0 || part >= SYNTH_PARTS || id < 0 || id >= SYNTH_MOD_PARAM + PM_COUNT || !(value == value))
        return;
    synth_event_t ev = {0};
    ev.type = EV_PARAM;
    ev.part = (uint8_t)part;
    ev.id = (int16_t)id;
    ev.value = value;
    ev.time = s->time_served;
    ev_push(s, &ev);
}

void synth_set_param(synth_t *s, int id, float value) { synth_part_param(s, 0, id, value); }

DSP_FAST_CODE void synth_render(synth_t *s, float *L, float *R, int n)
{
    dsp_denormals_off(); /* per-thread setting: the host's audio thread is not the init thread */
    int done = 0;
    while (done < n) {
        if (s->out_pos >= SYNTH_BLOCK) {
            render_block(s);
            s->out_pos = 0;
        }
        int k = SYNTH_BLOCK - s->out_pos;
        if (k > n - done)
            k = n - done;
        for (int i = 0; i < k; i++) {
            L[done + i] = s->outL[s->out_pos + i];
            R[done + i] = s->outR[s->out_pos + i];
        }
        s->out_pos += k;
        done += k;
        s->time_served += (uint32_t)k;
    }
}

/* ---- read-only views for displays (synth_probe.h) ---- */

int synth_probe_wavetable(const synth_t *s, int part, int osc, int frame, float *out, int n)
{
    if (!s || part < 0 || part >= SYNTH_PARTS || !s->part[part].ops || !s->part[part].ops->probe_table)
        return 0;
    return s->part[part].ops->probe_table(&s->part[part].eng, osc, frame, out, n);
}

float synth_probe_mseg(const synth_t *s, int part, int which)
{
    if (!s || part < 0 || part >= SYNTH_PARTS)
        return -1.0f;
    const synth_part_t *pt = &s->part[part];
    if (which == 0 || which == 1)
        return mseg_progress(&pt->mod.mseg[which], &pt->mod.shape[which]);
    if (which != 2 || !pt->ops || !pt->ops->poly)
        return -1.0f;
    vpoly_t *p = pt->ops->poly((void *)&pt->eng);
    if (!p || !p->vm_cfg)
        return -1.0f;
    float best = -1.0f, best_t = 1e30f; /* the note least far along its envelope: usually the last one */
    for (int v = 0; v < p->n; v++) {
        if (!p->v[v].active)
            continue;
        const float x = mseg_progress(&p->vm[v].env, &p->vm_cfg->shape);
        if (x >= 0.0f && x < best_t)
            best = x, best_t = x;
    }
    return best;
}
