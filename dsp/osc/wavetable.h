/* wavetable.h — band-limited wavetable oscillator. A table is WT_FRAMES single-cycle frames; each frame is
 * stored as WT_LEVELS mipmaps (one per octave, 512 harmonics down to 1), built at init by FFT from a
 * generator function. Playback: linear interpolation inside a frame and between neighbouring frames
 * ("position"), mip level chosen per block so harmonics stay below 0.55 x SR (what folds back lands above
 * 20 kHz), unison stack, and phase warps: hard sync (polyBLEP-corrected reset), bend, mirror, phase
 * modulation and ring modulation by an external signal.
 * References: P. Burk, "Band Limited Oscillators Using Wave Table Synthesis" (Audio Anecdotes, 2004);
 * V. Valimaki et al. on polyBLEP for the sync discontinuity. */
#ifndef WAVETABLE_H
#define WAVETABLE_H

#include "../core/dsp.h"

#ifndef WT_FRAMES
#define WT_FRAMES 32 /* frames per table (a board may define 16 to halve the memory) */
#endif
#define WT_LEVELS 10
#define WT_FRAME_FLOATS 10250 /* sum of (mip size + 1 guard sample) over the levels */
#define WT_TABLE_FLOATS (WT_FRAMES * WT_FRAME_FLOATS)
#define WT_GEN_SIZE 4096 /* resolution of a generated frame before band-limiting */
#define WT_SCRATCH_FLOATS (2 * WT_GEN_SIZE + 2 * 2048)
#define WT_UNISON_MAX 8

/* Fills out[0..n-1] with one cycle of the frame at position t (0..1); any amplitude, DC is removed. */
typedef void (*wt_gen_fn)(float t, float *out, int n);

/* Builds one table (WT_TABLE_FLOATS) from a generator. Init only (FFT); scratch: WT_SCRATCH_FLOATS. */
void wt_build(float *table, wt_gen_fn gen, float *scratch);

typedef enum { WT_WARP_OFF = 0, WT_WARP_SYNC, WT_WARP_BEND, WT_WARP_MIRROR, WT_WARP_FM, WT_WARP_RING, WT_WARP_COUNT } wt_warp_t;

typedef struct {
    float phase[WT_UNISON_MAX];
    float yprev[WT_UNISON_MAX]; /* output is one sample late: room for the sync polyBLEP */
    float pos, level;           /* values reached at the end of the previous block */
    int fresh;                  /* first block after a reset: no ramp */
} wt_osc_t;

typedef struct {
    float freq_hz;      /* centre frequency */
    float detune_cents; /* unison: outermost voices at +/- this */
    float position;     /* 0..1 across the frames */
    float warp;         /* 0..1 */
    float level;        /* linear output gain */
    int unison;         /* 1..WT_UNISON_MAX */
    int warp_mode;      /* wt_warp_t */
} wt_osc_set_t;

/* New note: random start phases from rng (unison voices must not start in phase). */
void wt_osc_reset(wt_osc_t *o, uint32_t *rng);
/* Adds n samples into out. mod: external signal for the FM and RING warps (may be NULL otherwise).
 * Position and level ramp linearly over the block; warp and pitch are block-constant. */
void wt_osc_process(wt_osc_t *o, const float *table, const wt_osc_set_t *s, float inv_sr, const float *mod,
                    float *out, int n);

#endif /* WAVETABLE_H */
