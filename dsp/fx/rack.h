/* rack.h — the synth's effect rack: seven effects on the bus (phaser, flanger, chorus, tape delay, plate reverb with
 * its shimmer, tape machine, gain) run in the order of its seven places, with their parameter descriptors. The delay and
 * the reverb add their wet to the signal; the reverb takes what reaches it, the echoes too when it follows the
 * delay. Delay, reverb, shimmer and tape memory comes from a caller-provided arena; if it is too small the reverb
 * size and the maximum delay time shrink. An effect whose mix stays at 0 is fed silence, then stops being
 * computed after its tail (CPU). */
#ifndef RACK_H
#define RACK_H

#include "../core/smooth.h"
#include "../mod/params.h"
#include "chorus.h"
#include "delay.h"
#include "flanger.h"
#include "phaser.h"
#include "reverb.h"
#include "tape.h"

/* parameters: the bus effects first (the order of SYNTH_PARAM_CHORUS_MODE.. in synth.h), then the others (the order
 * of SYNTH_PARAM_PHASER_MIX..) */
enum {
    RACK_P_CHORUS_MODE = 0, RACK_P_CHORUS_MIX,
    RACK_P_DELAY_MIX, RACK_P_DELAY_TIME, RACK_P_DELAY_RATIO, RACK_P_DELAY_FEEDBACK, RACK_P_DELAY_TONE,
    RACK_P_DELAY_DRIVE, RACK_P_DELAY_WOW, RACK_P_DELAY_PINGPONG,
    RACK_P_REVERB_MIX, RACK_P_REVERB_DECAY, RACK_P_REVERB_DAMPING, RACK_P_REVERB_PREDELAY, RACK_P_REVERB_FREEZE,
    RACK_P_BUS_COUNT,
    RACK_P_PHASER_MIX = RACK_P_BUS_COUNT, RACK_P_PHASER_RATE, RACK_P_PHASER_CENTER, RACK_P_PHASER_DEPTH,
    RACK_P_PHASER_FEEDBACK, RACK_P_PHASER_STAGES,
    RACK_P_FLANGER_MIX, RACK_P_FLANGER_RATE, RACK_P_FLANGER_DELAY, RACK_P_FLANGER_DEPTH, RACK_P_FLANGER_FEEDBACK,
    RACK_P_SLOT_1, RACK_P_SLOT_6 = RACK_P_SLOT_1 + 5,
    RACK_P_TAPE, RACK_P_TAPE_WOW, RACK_P_TAPE_FLUTTER, RACK_P_TAPE_DRIVE, RACK_P_TAPE_HISS, RACK_P_TAPE_TONE,
    RACK_P_REVERB_SHIMMER, RACK_P_REVERB_SHIMMER_PITCH,
    RACK_P_SLOT_7, /* the seventh place, for the gain (added after the others: their ids stay) */
    RACK_P_GAIN,
    RACK_P_COUNT
};
extern const param_desc_t rack_params[RACK_P_COUNT];
/* the effects, by the value of a place (RACK_P_SLOT_n) */
enum { RACK_PHASER = 0, RACK_FLANGER, RACK_CHORUS, RACK_DELAY, RACK_REVERB, RACK_TAPE, RACK_GAIN, RACK_UNITS };
/* the parameter of place k (0..RACK_UNITS-1) */
static inline int rack_slot_param(int k) { return k < 6 ? RACK_P_SLOT_1 + k : RACK_P_SLOT_7; }

#define RACK_SHIMMER_LEN 4096 /* a power of two: the transposer's window, in samples */

typedef struct {
    float sr;
    float p[RACK_P_COUNT]; /* physical values */
    uint8_t order[RACK_UNITS]; /* the effects in the order they run: each once (rack_resolve) */
    phaser_t phaser;
    flanger_t flanger;
    tape_t tape;
    int has_tape;
    chorus_t chorus;
    delay_t delay;
    reverb_t reverb;
    int has_delay, has_reverb;
    float delay_max_s;
    smooth_t sm_dmix, sm_rmix, sm_gain;
    float dmix_prev, rmix_prev, gain_prev; /* gain: linear */
    float delay_idle_s, reverb_idle_s; /* time spent with mix 0 */
    int dirty;
    float pl[SYNTH_BLOCK], pr[SYNTH_BLOCK]; /* echoes of this block not added to the signal yet */
    /* shimmer: the reverb's own output, transposed, comes back into it (a block late) */
    float *sh_buf, sh_fb[SYNTH_BLOCK]; /* the transposer's line: the last RACK_SHIMMER_LEN floats of the arena */
    float sh_amount, sh_rate, sh_ph; /* 0..1; delay slope (ratio - 1) / window per sample; phase of the two taps */
    int sh_w, sh_run;
    int pending;
} rack_t;

/* mem/mem_floats: arena for the tape's and the shimmer's lines, the reverb and the delay (may be NULL/0: then only
 * the phaser, the flanger and the chorus are available). */
void rack_init(rack_t *f, float sample_rate, float *mem, size_t mem_floats, float max_delay_s, float reverb_size);
/* Something went non-finite: every line and filter emptied, the settings kept. */
void rack_recover(rack_t *f, float *mem, size_t mem_floats, float max_delay_s, float reverb_size);
/* Parameter index (RACK_P_...), normalized value. */
void rack_set_param(rack_t *f, int id, float norm);
/* Processes one block in place (n <= SYNTH_BLOCK): the seven effects in the order of the places. */
void rack_process(rack_t *f, float *L, float *R, int n);

#endif /* RACK_H */
