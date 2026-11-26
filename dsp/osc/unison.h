/* unison.h — a stack of 1 to 16 detuned copies of one polyBLEP oscillator (sine, saw, pulse, triangle), spread in
 * pitch and across the stereo field like a modern unison: the voices sit at symmetric positions from -1 to +1, set
 * apart by `detune` (the outermost voice's offset, in cents) along a curve that keeps the inner ones a little closer,
 * with a small fixed irregularity so the stack never shares a beat period (cf. A. Szabo, "How to
 * Emulate the Super Saw", 2010). Blend sets the side voices against the centre, the sum kept at a constant loudness;
 * width pans them (constant power), in a scrambled order so each side gets low and high voices. Each voice can drift
 * on its own slow LFO (motion). Gains and pitch are set per block and ramped (gains) or interpolated (pitch). */
#ifndef UNISON_H
#define UNISON_H

#include "polyblep.h"

#define UNISON_MAX 16

typedef struct {
    osc_t osc[UNISON_MAX];
    float lfo_phase[UNISON_MAX];
    float lfo_factor[UNISON_MAX]; /* per-voice motion rate multiplier, random in [0.6, 1.4] */
    float gl[UNISON_MAX], gr[UNISON_MAX];           /* left and right gain of each voice at the block's end */
    float gl_prev[UNISON_MAX], gr_prev[UNISON_MAX]; /* ... and at its start */
    int count;
    int fresh; /* the next block starts at its gains (note start) */
    uint32_t rng;
} unison_t;

void unison_init(unison_t *u, float sample_rate, uint32_t seed);
/* New note: each voice restarts at a random phase (random = 1) or all at phase 0 (a hard, identical attack); new
 * motion rates and phases. */
void unison_reset(unison_t *u, int random);
/* Once per block, before unison_process: centre frequency (Hz), wave (osc_wave_t), pulse width, voices (1..16),
 * detune (cents of the outermost voice), blend (0 = the centre alone .. 1 = every voice as loud), width (0 = mono ..
 * 1 = from hard left to hard right), motion (cents) and its average rate (Hz), the block length in samples. */
void unison_set(unison_t *u, float freq_hz, int wave, float pw, int voices, float detune_cents, float blend, float width,
                float motion_cents, float motion_hz, int block);
/* Writes n samples (n <= SYNTH_BLOCK) into out_l and out_r, the voices panned (constant power, scaled so that a
 * centred voice has its full gain on each side: the caller pans the pair as it would a mono signal); with out_r NULL,
 * the mono sum into out_l (for a width of 0, where both sides are equal). */
void unison_process(unison_t *u, float *restrict out_l, float *restrict out_r, int n);

#endif /* UNISON_H */
