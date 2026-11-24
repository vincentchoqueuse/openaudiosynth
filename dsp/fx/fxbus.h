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
    int pending;
} fxbus_t;

/* mem/mem_floats: arena for delay + reverb (may be NULL/0: then only the chorus is available). */
void fxbus_init(fxbus_t *f, float sample_rate, float *mem, size_t mem_floats, float max_delay_s, float reverb_size);
void fxbus_set_param(fxbus_t *f, int id, float norm);
/* One block in place (n <= SYNTH_BLOCK), as stages: each at most once per block, between fxbus_begin and a last
 * fxbus_flush. The delay leaves its echoes pending: the reverb right after it reads them itself, anything else
 * calls fxbus_flush first, which adds them to the signal. */
void fxbus_begin(fxbus_t *f);
void fxbus_chorus(fxbus_t *f, float *L, float *R, int n);
void fxbus_delay(fxbus_t *f, float *L, float *R, int n);
void fxbus_reverb(fxbus_t *f, float *L, float *R, int n);
void fxbus_flush(fxbus_t *f, float *L, float *R, int n);

#endif /* FXBUS_H */
