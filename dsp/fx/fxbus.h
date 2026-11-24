/* fxbus.h — three effects of the synth's bus and their parameter descriptors: chorus (insert), tape delay and
 * plate reverb (their wet added to the signal). They are stages the caller runs in any order among other effects
 * (the rack of synth.c): fxbus_begin, then fxbus_chorus, fxbus_delay and fxbus_reverb once each, fxbus_flush before
 * anything else reads the signal and at the end. Delay and reverb memory comes from a
 * caller-provided arena; if the arena is too small the reverb size and maximum delay time shrink.
 * An effect whose mix stays at 0 is fed silence, then stops being computed after its tail (CPU). */
#ifndef FXBUS_H
#define FXBUS_H

#include "../core/smooth.h"
#include "../mod/params.h"
#include "chorus.h"
#include "delay.h"
#include "reverb.h"

enum {
    FXB_P_CHORUS_MODE = 0, FXB_P_CHORUS_MIX,
    FXB_P_DELAY_MIX, FXB_P_DELAY_TIME, FXB_P_DELAY_RATIO, FXB_P_DELAY_FEEDBACK, FXB_P_DELAY_TONE,
    FXB_P_DELAY_DRIVE, FXB_P_DELAY_WOW, FXB_P_DELAY_PINGPONG,
    FXB_P_REVERB_MIX, FXB_P_REVERB_DECAY, FXB_P_REVERB_DAMPING, FXB_P_REVERB_PREDELAY, FXB_P_REVERB_FREEZE,
    FXB_P_COUNT
};
extern const param_desc_t fxbus_params[FXB_P_COUNT];

#define FXB_SHIMMER_LEN 4096 /* a power of two: the transposer's window, in samples */

typedef struct {
    float sr;
    float p[FXB_P_COUNT]; /* physical values */
    chorus_t chorus;
    delay_t delay;
    reverb_t reverb;
    int has_delay, has_reverb;
    float delay_max_s;
    smooth_t sm_dmix, sm_rmix;
    float dmix_prev, rmix_prev;
    float delay_idle_s, reverb_idle_s; /* time spent with mix 0 */
    int dirty;
    float pl[SYNTH_BLOCK], pr[SYNTH_BLOCK]; /* echoes of this block not added to the signal yet */
    /* shimmer: the reverb's own output, transposed, comes back into it (a block late) */
    float *sh_buf, sh_fb[SYNTH_BLOCK]; /* the transposer's line: the last FXB_SHIMMER_LEN floats of the arena */
    float sh_amount, sh_rate, sh_ph; /* 0..1; delay slope (ratio - 1) / window per sample; phase of the two taps */
    int sh_w, sh_run;
    int pending;
} fxbus_t;

/* mem/mem_floats: arena for the shimmer's line, the reverb and the delay (may be NULL/0: then only the chorus is
 * available). */
void fxbus_init(fxbus_t *f, float sample_rate, float *mem, size_t mem_floats, float max_delay_s, float reverb_size);
void fxbus_set_param(fxbus_t *f, int id, float norm);
/* Shimmer: how much of the reverb's output comes back into it transposed (0 = none, 1 = a tail that keeps
 * climbing), and by how many semitones (12 = the classic octave up; negative goes down). */
void fxbus_set_shimmer(fxbus_t *f, float amount, float semitones);
/* One block in place (n <= SYNTH_BLOCK), as stages: each at most once per block, between fxbus_begin and a last
 * fxbus_flush. The delay leaves its echoes pending: the reverb right after it reads them itself, anything else
 * calls fxbus_flush first, which adds them to the signal. */
void fxbus_begin(fxbus_t *f);
void fxbus_chorus(fxbus_t *f, float *L, float *R, int n);
void fxbus_delay(fxbus_t *f, float *L, float *R, int n);
void fxbus_reverb(fxbus_t *f, float *L, float *R, int n);
void fxbus_flush(fxbus_t *f, float *L, float *R, int n);

#endif /* FXBUS_H */
