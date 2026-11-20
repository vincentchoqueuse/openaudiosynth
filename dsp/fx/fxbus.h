/* fxbus.h — the synth's effect bus: chorus (insert) -> tape delay -> plate reverb (both parallel,
 * wet added to the dry signal), with its parameter descriptors. Delay and reverb memory comes from a
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
    float delay_max_s, reverb_size;
    smooth_t sm_dmix, sm_rmix;
    float dmix_prev, rmix_prev;
    float delay_idle_s, reverb_idle_s; /* time spent with mix 0 */
    int dirty;
} fxbus_t;

/* Arena size for a delay of max_delay_s and a reverb of the given size at this sample rate. */
size_t fxbus_mem_floats(float sample_rate, float max_delay_s, float reverb_size);
/* mem/mem_floats: arena for delay + reverb (may be NULL/0: then only the chorus is available). */
void fxbus_init(fxbus_t *f, float sample_rate, float *mem, size_t mem_floats, float max_delay_s, float reverb_size);
void fxbus_set_param(fxbus_t *f, int id, float norm);
/* Processes one block in place (n <= SYNTH_BLOCK). */
void fxbus_process(fxbus_t *f, float *L, float *R, int n);

#endif /* FXBUS_H */
