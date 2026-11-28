/* rack.c — the effect rack (see rack.h): descriptors, the order of the places, and the seven effects as stages run
 * in that order. The delay leaves its echoes pending: the reverb right after it takes them into its send (the
 * echoes are reverberated too), anything else has them added first (flush). Mix levels are smoothed at control
 * rate and ramped per sample. */
#include "rack.h"
#include "../core/fastmath.h"

#define SHIMMER_K 5.0f /* gain of the shimmer's return at amount 1, times (1 - decay) */
#define IDLE_AFTER_S 20.0f /* keep computing a muted effect this long so its tail empties */

static const char *const chorus_labels[] = {"off", "I", "II", "I+II", "ensemble", NULL};

static const char *const phaser_stage_labels[] = {"2", "4", "6", "8", "10", "12", NULL};
/* the effects of the rack, by the id of their stage (synth_fx_stages) */
static const char *const rack_labels[] = {"phaser", "flanger", "chorus", "delay", "reverb", "tape", "gain", NULL};
static const char *const shimmer_labels[] = {"octave_up", "fifth_up", "twelfth_up", "octave_down", NULL};
static const float shimmer_semitones[4] = {12.0f, 7.0f, 19.0f, -12.0f};
#define SLOT(K, N) PARAM((K) < 6 ? RACK_P_SLOT_1 + (K) : RACK_P_SLOT_7, "fx_slot" #N, "fx.rack.slot" #N, 0.0f, (float)(RACK_UNITS - 1), (float)K, PARAM_INT, "", 0, \
                           rack_labels, "the effect at this place of the rack (the signal goes through the places in order); an effect named twice keeps its first place, one not named goes just before the effect that usually follows it")

static const char *const reverb_type_labels[] = {"plate", "room", "hall", "spring", NULL};
const param_desc_t rack_params[RACK_P_COUNT] = {
    PARAM(RACK_P_CHORUS_MODE, "chorus_mode", "fx.chorus.mode", 0.0f, 4.0f, 0.0f, PARAM_INT, "", 0, chorus_labels,
          "chorus: I = slow and deep, II = faster, I+II = both, ensemble = 3-phase string-machine shimmer"),
    PARAM(RACK_P_CHORUS_MIX, "chorus_mix", "fx.chorus.mix", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 1, NULL,
          "chorus wet amount added to the dry sound (width, analog lushness)"),
    PARAM(RACK_P_DELAY_MIX, "delay_mix", "fx.delay.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "tape echo level (0 = off): 0.2-0.4 ambient, more for dub"),
    PARAM(RACK_P_DELAY_TIME, "delay_time", "fx.delay.time", 0.02f, 2.0f, 0.375f, PARAM_EXP, "s", 1, NULL,
          "echo time; changing it bends the repeats like a tape machine. Tempo: 60/bpm x beats"),
    PARAM(RACK_P_DELAY_RATIO, "delay_ratio", "fx.delay.stereo_ratio", 0.25f, 2.0f, 1.0f, PARAM_LIN, "", 0, NULL,
          "right echo time = left x ratio (1.5 = dotted feel, 0.75 ...); ignored in ping-pong"),
    PARAM(RACK_P_DELAY_FEEDBACK, "delay_feedback", "fx.delay.feedback", 0.0f, 0.98f, 0.4f, PARAM_LIN, "", 1, NULL,
          "amount of repeats: 0.3 few, 0.6 long trail, 0.9+ near self-oscillation"),
    PARAM(RACK_P_DELAY_TONE, "delay_tone", "fx.delay.tone", 300.0f, 16000.0f, 3500.0f, PARAM_EXP, "Hz", 1, NULL,
          "low-pass inside the loop: each repeat gets darker (tape); 2-4 kHz = warm, 10k+ = digital"),
    PARAM(RACK_P_DELAY_DRIVE, "delay_drive", "fx.delay.drive", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "tape saturation inside the loop: repeats get grainier and compressed"),
    PARAM(RACK_P_DELAY_WOW, "delay_wow", "fx.delay.wow", 0.0f, 1.0f, 0.25f, PARAM_LIN, "", 1, NULL,
          "tape wow and flutter: pitch wobble of the repeats (0.2 subtle, 0.6+ seasick)"),
    PARAM(RACK_P_DELAY_PINGPONG, "delay_pingpong", "fx.delay.pingpong", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "repeats alternate left / right"),
    PARAM(RACK_P_REVERB_MIX, "reverb_mix", "fx.reverb.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "plate reverb level (0 = off): 0.2 room-ish, 0.5 ambient, 0.8+ washed out"),
    PARAM(RACK_P_REVERB_DECAY, "reverb_decay", "fx.reverb.decay", 0.0f, 0.99f, 0.85f, PARAM_LIN, "", 1, NULL,
          "tail length: 0.5 ~1.8 s, 0.85 ~5 s, 0.93 ~10 s, 0.97+ endless"),
    PARAM(RACK_P_REVERB_DAMPING, "reverb_damping", "fx.reverb.damping", 500.0f, 16000.0f, 4500.0f, PARAM_EXP, "Hz", 1, NULL,
          "high-frequency loss in the tail: lower = darker, softer"),
    PARAM(RACK_P_REVERB_PREDELAY, "reverb_predelay", "fx.reverb.predelay", 0.0f, 0.25f, 0.03f, PARAM_LIN, "s", 0, NULL,
          "gap before the reverb: keeps the dry sound clear"),
    PARAM(RACK_P_REVERB_FREEZE, "reverb_freeze", "fx.reverb.freeze", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 1, param_onoff_labels,
          "freeze: the current tail holds forever and new sound stays dry on top (CC 69)"),
    PARAM(RACK_P_PHASER_MIX, "phaser_mix", "fx.phaser.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "phaser amount: 0 = off, 1 = half dry, half phased (the deepest notches)"),
    PARAM(RACK_P_PHASER_RATE, "phaser_rate", "fx.phaser.rate", 0.02f, 10.0f, 0.3f, PARAM_EXP, "Hz", 1, NULL,
          "speed of the sweep: 0.05-0.5 Hz = slow swirl, several Hz = a vibrato-like warble"),
    PARAM(RACK_P_PHASER_CENTER, "phaser_center", "fx.phaser.center", 100.0f, 5000.0f, 800.0f, PARAM_EXP, "Hz", 1, NULL,
          "middle of the sweep: low = throaty, high = airy"),
    PARAM(RACK_P_PHASER_DEPTH, "phaser_depth", "fx.phaser.depth", 0.0f, 1.0f, 0.6f, PARAM_LIN, "", 1, NULL,
          "width of the sweep, up to 4 octaves (2 each side of the centre)"),
    PARAM(RACK_P_PHASER_FEEDBACK, "phaser_feedback", "fx.phaser.feedback", -0.9f, 0.9f, 0.5f, PARAM_LIN, "", 1, NULL,
          "resonance of the notches: 0 = gentle, high = a sharp, singing sweep; negative = the peaks move between the notches"),
    PARAM(RACK_P_PHASER_STAGES, "phaser_stages", "fx.phaser.stages", 0.0f, 5.0f, 1.0f, PARAM_INT, "", 0, phaser_stage_labels,
          "all-pass stages: 4 = the classic small phaser (2 notches), more = more notches, a denser swirl"),
    PARAM(RACK_P_FLANGER_MIX, "flanger_mix", "fx.flanger.mix", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "flanger amount: 0 = off, 1 = half dry, half delayed (the deepest notches, the jet)"),
    PARAM(RACK_P_FLANGER_RATE, "flanger_rate", "fx.flanger.rate", 0.02f, 10.0f, 0.15f, PARAM_EXP, "Hz", 1, NULL,
          "speed of the sweep: 0.05-0.3 Hz = the slow jet, faster = a wobble"),
    PARAM(RACK_P_FLANGER_DELAY, "flanger_delay", "fx.flanger.delay", 0.5f, 10.0f, 4.0f, PARAM_EXP, "ms", 1, NULL,
          "the longest delay of the sweep: short = high, metallic notches, long = lower, towards a chorus"),
    PARAM(RACK_P_FLANGER_DEPTH, "flanger_depth", "fx.flanger.depth", 0.0f, 1.0f, 0.8f, PARAM_LIN, "", 1, NULL,
          "how far the sweep goes, down to a twentieth of the delay"),
    PARAM(RACK_P_FLANGER_FEEDBACK, "flanger_feedback", "fx.flanger.feedback", -0.9f, 0.9f, 0.6f, PARAM_LIN, "", 1, NULL,
          "resonance: high = metallic, ringing; negative = hollow (the resonances at the odd multiples of half the delay's frequency)"),
    SLOT(0, 1), SLOT(1, 2), SLOT(2, 3), SLOT(3, 4), SLOT(4, 5), SLOT(5, 6),
    PARAM(RACK_P_TAPE, "tape", "fx.tape.mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 1, param_onoff_labels,
          "a worn tape machine on the whole signal: pitch that wanders, soft saturation, dull highs, hiss"),
    PARAM(RACK_P_TAPE_WOW, "tape_wow", "fx.tape.wow", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "wow: slow pitch wander of everything (0.2 alive, 0.5 worn cassette, 1 = about 0.5 %, seasick)"),
    PARAM(RACK_P_TAPE_FLUTTER, "tape_flutter", "fx.tape.flutter", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "flutter: fast pitch tremble (about 7 Hz), the grain of a cheap transport"),
    PARAM(RACK_P_TAPE_DRIVE, "tape_drive", "fx.tape.drive", 0.0f, 1.0f, 0.3f, PARAM_LIN, "", 1, NULL,
          "level onto the tape: 0 clean, 0.3 rounded peaks and glue, 0.7 and more compressed and gritty (level kept)"),
    PARAM(RACK_P_TAPE_HISS, "tape_hiss", "fx.tape.hiss", 0.0f, 1.0f, 0.15f, PARAM_LIN, "", 1, NULL,
          "tape hiss under the sound (0 = none; it stays after the notes, like a real machine left running)"),
    PARAM(RACK_P_TAPE_TONE, "tape_tone", "fx.tape.tone", 1000.0f, 18000.0f, 9000.0f, PARAM_EXP, "Hz", 1, NULL,
          "loss of highs: 12 kHz and more a good machine, 6-9 kHz a cassette, 2-4 kHz a tape left in the sun"),
    PARAM(RACK_P_REVERB_SHIMMER, "reverb_shimmer", "fx.reverb.shimmer", 0.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "shimmer: the tail comes back into the reverb transposed, so it climbs and glows (0.3 a halo, 0.6 an organ of light, 1 = it keeps rising); needs a long decay"),
    PARAM(RACK_P_REVERB_SHIMMER_PITCH, "reverb_shimmer_pitch", "fx.reverb.shimmer_pitch", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 0, shimmer_labels,
          "interval of the shimmer: an octave up (the classic), a fifth up (stacks into chords), a twelfth up, or an octave down (the tail sinks)"),
    SLOT(6, 7),
    PARAM(RACK_P_GAIN, "gain_level", "fx.gain.level", -24.0f, 24.0f, 0.0f, PARAM_LIN, "dB", 1, NULL,
          "a plain gain at its place of the rack: louder or softer into what follows (a reverb, the tape's saturation), or after it"),
    PARAM(RACK_P_REVERB_TYPE, "reverb_type", "fx.reverb.type", 0.0f, (float)(REVERB_TYPES - 1), 0.0f, PARAM_INT, "", 0,
          reverb_type_labels,
          "the kind of space: plate (dense and bright, the studio classic), room (small, early reflections, close), "
          "hall (large, slow to build, wide), spring (the guitar amp's tank: metallic, it drips)"),
    PARAM(RACK_P_REVERB_SIZE, "reverb_size", "fx.reverb.size", 0.0f, 1.0f, 0.5f, PARAM_LIN, "", 0, NULL,
          "size of the space around its type's own (0.5): half as big at 0, twice at 1; moving it slides the echoes"),
    PARAM(RACK_P_REVERB_LOWCUT, "reverb_lowcut", "fx.reverb.lowcut", 20.0f, 1000.0f, 20.0f, PARAM_EXP, "Hz", 1, NULL,
          "low cut before the reverb: 20 Hz = off; 150-300 Hz keeps a bass or a kick out of the tail (less mud)"),
    PARAM(RACK_P_REVERB_WIDTH, "reverb_width", "fx.reverb.width", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "stereo width of the tail: 1 its own, 0 mono (centred, behind the sound)"),
    PARAM(RACK_P_TAPE_MIX, "tape_mix", "fx.tape.mix", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "dry / wet of the tape: 1 all through the machine, lower blends the clean signal back in"),
};

static void apply_extra(rack_t *f, int id);

#define RACK_PLATE_SIZE 1.5f /* the plate of the rack (its size when the arena was sized for 1.5) */

void rack_init(rack_t *f, float sr, float *mem, size_t mem_floats, float max_delay_s, float reverb_size)
{
    f->sr = sr;
    for (int i = 0; i < RACK_P_COUNT; i++)
        f->p[i] = rack_params[i].def;
    phaser_init(&f->phaser, sr);
    flanger_init(&f->flanger, sr);
    /* the tape's line from the end of the arena */
    f->has_tape = 0;
    if (mem && mem_floats > TAPE_MEM_FLOATS) {
        mem_floats -= TAPE_MEM_FLOATS;
        tape_init(&f->tape, sr, mem + mem_floats);
        f->has_tape = 1;
    }
    chorus_init(&f->chorus, sr);

    /* the shimmer's line from the end of the arena */
    f->sh_buf = 0;
    if (mem && mem_floats > RACK_SHIMMER_LEN) {
        mem_floats -= RACK_SHIMMER_LEN;
        f->sh_buf = mem + mem_floats;
    }
    /* reverb first (it defines the sound most), shrinking its size if the arena is small */
    f->has_reverb = f->has_delay = 0;
    float size = dsp_clampf(reverb_size, 0.5f, REVERB_MAX_SIZE);
    while (size >= 0.5f && mem && reverb_mem_floats(sr, size) > mem_floats)
        size -= 0.1f;
    if (mem && size >= 0.5f) {
        reverb_init_max(&f->reverb, sr, RACK_PLATE_SIZE, size, mem); /* the plate as always, room for a hall */
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
    smooth_init(&f->sm_gain, 0.005f, ctrl, 1.0f);
    f->dmix_prev = f->rmix_prev = 0.0f;
    f->gain_prev = 1.0f;
    f->tmix_prev = 1.0f;
    f->delay_idle_s = f->reverb_idle_s = IDLE_AFTER_S; /* idle until used */
    f->dirty = 1;
    f->pending = 0;
    for (int i = 0; f->sh_buf && i < RACK_SHIMMER_LEN; i++)
        f->sh_buf[i] = 0.0f;
    for (int i = 0; i < SYNTH_BLOCK; i++)
        f->sh_fb[i] = 0.0f;
    f->sh_w = f->sh_run = 0;
    f->sh_ph = 0.0f;
    for (int id = RACK_P_BUS_COUNT; id < RACK_P_COUNT; id++) /* the settings of the descriptors */
        apply_extra(f, id);
    
}

static void set_shimmer(rack_t *f, float amount, float semitones)
{
    f->sh_amount = dsp_clampf(amount, 0.0f, 1.0f);
    /* a tap whose delay shrinks by (ratio - 1) samples per sample reads the line `ratio` times faster */
    f->sh_rate = (fm_exp2(semitones * (1.0f / 12.0f)) - 1.0f) * (1.0f / (float)(RACK_SHIMMER_LEN - 8));
}

/* The order of the rack from what its places ask for: an effect named twice keeps its first place; an effect no
 * place names goes just before the one that follows it in the usual order (the last one: at the end). So naming
 * only the first place moves that effect to the front and leaves the others as they were, and every effect runs
 * once whatever the values are. */
static void rack_resolve(rack_t *f)
{
    uint8_t used[RACK_UNITS] = {0};
    int n = 0;
    for (int k = 0; k < RACK_UNITS; k++) {
        const int e = (int)f->p[rack_slot_param(k)];
        if (e >= 0 && e < RACK_UNITS && !used[e])
            used[e] = 1, f->order[n++] = (uint8_t)e;
    }
    for (int e = RACK_UNITS - 1; e >= 0; e--) { /* from the last: its follower is always placed already */
        if (used[e])
            continue;
        int at = n;
        for (int i = 0; i < n; i++)
            if (f->order[i] == e + 1)
                at = i;
        for (int i = n; i > at; i--)
            f->order[i] = f->order[i - 1];
        f->order[at] = (uint8_t)e;
        used[e] = 1, n++;
    }
}

/* a parameter outside the chorus, delay and reverb block: given to its effect at once */
static void apply_extra(rack_t *f, int id)
{
    const float *p = f->p;
    if (id <= RACK_P_PHASER_STAGES)
        phaser_set(&f->phaser, p[RACK_P_PHASER_MIX], p[RACK_P_PHASER_RATE], p[RACK_P_PHASER_CENTER], p[RACK_P_PHASER_DEPTH],
                   p[RACK_P_PHASER_FEEDBACK], 2 * ((int)p[RACK_P_PHASER_STAGES] + 1));
    else if (id <= RACK_P_FLANGER_FEEDBACK)
        flanger_set(&f->flanger, p[RACK_P_FLANGER_MIX], p[RACK_P_FLANGER_RATE], p[RACK_P_FLANGER_DELAY],
                    p[RACK_P_FLANGER_DEPTH], p[RACK_P_FLANGER_FEEDBACK]);
    else if (id <= RACK_P_SLOT_6 || id == RACK_P_SLOT_7)
        rack_resolve(f);
    else if (id == RACK_P_GAIN)
        return; /* read each block, smoothed */
    else if (id == RACK_P_TAPE_MIX)
        return; /* read each block, ramped */
    else if (id >= RACK_P_REVERB_TYPE) {
        if (!f->has_reverb)
            return;
        if (id <= RACK_P_REVERB_SIZE)
            reverb_set_type(&f->reverb, (int)p[RACK_P_REVERB_TYPE], p[RACK_P_REVERB_SIZE]);
        else
            reverb_set_tone(&f->reverb, p[RACK_P_REVERB_LOWCUT], p[RACK_P_REVERB_WIDTH]);
    }
    else if (id <= RACK_P_TAPE_TONE) {
        if (f->has_tape)
            tape_set(&f->tape, (int)p[RACK_P_TAPE], p[RACK_P_TAPE_WOW], p[RACK_P_TAPE_FLUTTER], p[RACK_P_TAPE_DRIVE],
                     p[RACK_P_TAPE_HISS], p[RACK_P_TAPE_TONE]);
    } else
        set_shimmer(f, p[RACK_P_REVERB_SHIMMER], shimmer_semitones[dsp_clampi((int)p[RACK_P_REVERB_SHIMMER_PITCH], 0, 3)]);
}

void rack_set_param(rack_t *f, int id, float norm)
{
    if (id < 0 || id >= RACK_P_COUNT)
        return;
    f->p[id] = param_from_norm(&rack_params[id], norm);
    if (id < RACK_P_BUS_COUNT)
        f->dirty = 1; /* chorus, delay, reverb: set together at the next block */
    else
        apply_extra(f, id);
}

void rack_recover(rack_t *f, float *mem, size_t mem_floats, float max_delay_s, float reverb_size)
{
    float p[RACK_P_COUNT];
    for (int i = 0; i < RACK_P_COUNT; i++)
        p[i] = f->p[i];
    rack_init(f, f->sr, mem, mem_floats, max_delay_s, reverb_size);
    for (int i = 0; i < RACK_P_COUNT; i++)
        rack_set_param(f, i, param_to_norm(&rack_params[i], p[i]));
}

static void apply(rack_t *f)
{
    const float *p = f->p;
    chorus_set(&f->chorus, (chorus_mode_t)(int)p[RACK_P_CHORUS_MODE], p[RACK_P_CHORUS_MIX]);
    if (f->has_delay)
        delay_set(&f->delay, dsp_minf(p[RACK_P_DELAY_TIME], f->delay_max_s), p[RACK_P_DELAY_RATIO],
                  p[RACK_P_DELAY_FEEDBACK], p[RACK_P_DELAY_TONE], p[RACK_P_DELAY_DRIVE], p[RACK_P_DELAY_WOW],
                  (int)p[RACK_P_DELAY_PINGPONG]);
    if (f->has_reverb) {
        reverb_set(&f->reverb, p[RACK_P_REVERB_DECAY], p[RACK_P_REVERB_DAMPING], p[RACK_P_REVERB_PREDELAY]);
        reverb_set_freeze(&f->reverb, (int)p[RACK_P_REVERB_FREEZE]);
    }
    f->dirty = 0;
}

static void stage_begin(rack_t *f)
{
    if (f->dirty)
        apply(f);
    f->pending = 0;
}

static DSP_FAST_CODE void flush(rack_t *f, float *L, float *R, int n)
{
    if (!f->pending)
        return;
    for (int i = 0; i < n; i++) {
        L[i] += f->pl[i];
        R[i] += f->pr[i];
    }
    f->pending = 0;
}

static DSP_FAST_CODE void stage_chorus(rack_t *f, float *L, float *R, int n)
{
    flush(f, L, R, n);
    chorus_process(&f->chorus, L, R, L, R, n);
}

/* delay: parallel send, its wet left pending (added by the reverb after it or by fxbus_flush) */
static DSP_FAST_CODE void stage_delay(rack_t *f, float *L, float *R, int n)
{
    float wl[SYNTH_BLOCK], wr[SYNTH_BLOCK];
    flush(f, L, R, n);
    const float dt = (float)n / f->sr;
    const float d0 = f->dmix_prev, d1 = smooth_next(&f->sm_dmix, f->has_delay ? f->p[RACK_P_DELAY_MIX] : 0.0f);
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
static DSP_FAST_CODE void stage_reverb(rack_t *f, float *L, float *R, int n)
{
    float wl[SYNTH_BLOCK], wr[SYNTH_BLOCK], send[SYNTH_BLOCK] = {0.0f}; /* zeroed: n may be short */
    const float dt = (float)n / f->sr;
    const float r0 = f->rmix_prev, r1 = smooth_next(&f->sm_rmix, f->has_reverb ? f->p[RACK_P_REVERB_MIX] : 0.0f);
    f->rmix_prev = r1;
    f->reverb_idle_s = (r0 <= 1e-5f && r1 <= 1e-5f) ? f->reverb_idle_s + dt : 0.0f;
    /* a muted reverb goes idle; if it was frozen, the frozen tail is kept for when it comes back */
    const int reverb_on = f->has_reverb && f->reverb_idle_s < IDLE_AFTER_S;
    if (!reverb_on) {
        f->sh_run = 0; /* nothing of the old tail comes back when the reverb does */
        flush(f, L, R, n);
        return;
    }
    const int muted = r1 <= 1e-5f, echoes = f->pending;
    for (int i = 0; i < n; i++)
        send[i] = muted ? 0.0f : 0.5f * (L[i] + R[i] + (echoes ? f->pl[i] + f->pr[i] : 0.0f));
    /* shimmer: the last block's transposed tail joins the send; then this block's tail is transposed for the next.
     * Two taps half a window apart read the tail faster (or slower) than it is written, cross-faded where each
     * jumps back (the rotating-head transposer); a tanh bounds the loop */
    const int shimmer = f->sh_buf && f->sh_amount > 0.0f;
    if (shimmer && f->sh_run && !muted)
        for (int i = 0; i < n; i++)
            send[i] += f->sh_fb[i];
    reverb_process(&f->reverb, send, wl, wr, n);
    if (shimmer) {
        /* the gain of the loop follows the decay: a long tail returns less at each pass (its reverb gives more
         * back), so the amount means the same at every decay and the loop cannot run away */
        const float dc = f->p[RACK_P_REVERB_DECAY], win = (float)(RACK_SHIMMER_LEN - 8);
        const float g = f->sh_amount * dsp_minf(SHIMMER_K * (1.0f - dc), 0.7f);
        for (int i = 0; i < n; i++) {
            f->sh_buf[f->sh_w] = 0.5f * (wl[i] + wr[i]);
            f->sh_ph = fm_frac(f->sh_ph + f->sh_rate);
            float y = 0.0f;
            for (int k = 0; k < 2; k++) {
                const float ph = fm_frac(f->sh_ph + 0.5f * (float)k);
                const float d = 2.0f + win * (1.0f - ph);
                const int di = (int)d;
                const float fr = d - (float)di;
                const int a0 = (f->sh_w - di) & (RACK_SHIMMER_LEN - 1), a1 = (a0 - 1) & (RACK_SHIMMER_LEN - 1);
                y += (f->sh_buf[a0] + (f->sh_buf[a1] - f->sh_buf[a0]) * fr) * (0.5f - 0.5f * fm_cos(ph));
            }
            f->sh_fb[i] = fm_tanh(g * y);
            f->sh_w = (f->sh_w + 1) & (RACK_SHIMMER_LEN - 1);
        }
        f->sh_run = 1;
    } else
        f->sh_run = 0;
    dsp_ramp_mul(wl, r0, r1, n);
    dsp_ramp_mul(wr, r0, r1, n);
    flush(f, L, R, n);
    for (int i = 0; i < n; i++) {
        L[i] += wl[i];
        R[i] += wr[i];
    }
}

DSP_FAST_CODE void rack_process(rack_t *f, float *L, float *R, int n)
{
    stage_begin(f);
    for (int k = 0; k < RACK_UNITS; k++)
        switch (f->order[k]) {
        case RACK_PHASER:
            flush(f, L, R, n);
            phaser_process(&f->phaser, L, R, n); /* untouched while its mix is 0 */
            break;
        case RACK_FLANGER:
            flush(f, L, R, n);
            flanger_process(&f->flanger, L, R, n);
            break;
        case RACK_CHORUS: stage_chorus(f, L, R, n); break;
        case RACK_DELAY: stage_delay(f, L, R, n); break;
        case RACK_TAPE:
            flush(f, L, R, n);
            if (f->has_tape) {
                const float m0 = f->tmix_prev, m1 = f->p[RACK_P_TAPE_MIX];
                f->tmix_prev = m1;
                if (m0 >= 1.0f && m1 >= 1.0f) { /* all wet: as it always was */
                    tape_process(&f->tape, L, R, n);
                    break;
                }
                float dl[SYNTH_BLOCK], dr[SYNTH_BLOCK];
                for (int i = 0; i < n; i++)
                    dl[i] = L[i], dr[i] = R[i];
                tape_process(&f->tape, L, R, n);
                const float step = (m1 - m0) / (float)n;
                for (int i = 0; i < n; i++) { /* the blend ramps over the block */
                    const float m = m0 + step * (float)(i + 1);
                    L[i] = dl[i] + m * (L[i] - dl[i]);
                    R[i] = dr[i] + m * (R[i] - dr[i]);
                }
            }
            break;
        case RACK_GAIN: {
            flush(f, L, R, n);
            const float g0 = f->gain_prev, g1 = smooth_next(&f->sm_gain, fm_db2lin(f->p[RACK_P_GAIN]));
            f->gain_prev = g1;
            if (g0 != 1.0f || g1 != 1.0f) { /* at 0 dB: untouched */
                dsp_ramp_mul(L, g0, g1, n);
                dsp_ramp_mul(R, g0, g1, n);
            }
            break;
        }
        default: stage_reverb(f, L, R, n); break;
        }
    flush(f, L, R, n);
}
