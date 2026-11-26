/* morph.h — a table of filters, morphed: like a wavetable, but each frame is a filter, a cascade of MORPH_SECTIONS
 * second-order sections, each a pole pair (a resonance: its frequency relative to the cutoff and its Q) and a zero
 * pair (a notch or the corner of a low- or high-pass). The position moves between frames by interpolating the poles'
 * frequency (in octaves) and Q and the zeros, never the coefficients, so every point between two frames is a real,
 * stable filter and the position can move fast. Each section is scaled to a set gain at its resonance, so the level
 * stays near when Q rises (the peak rises at a third of Q's rate). After each section but the last a drive saturates
 * (soft, fm_tanh, its ceiling relative to the section's gain): the resonance grunts and breaks. The
 * position can follow the input's level (an envelope follower): the harder it is played, the further it moves.
 * Around the whole cascade a feedback loop, as in a physical model: the output, saturated, goes back to the input
 * with a gain (-1..1, scaled by the table's own gain so that near 1 the loop rings by itself on the table's
 * resonances: a body excited rather than a filter).
 * Coefficients are computed at each block's end and ramped across it. References: pole-zero morphing filters
 * (R. Rossum, "Making digital filters sound analog", ICMC 1992); RBJ biquad cookbook for the section forms. */
#ifndef MORPH_H
#define MORPH_H

#include "../core/dsp.h"

#define MORPH_SECTIONS 4
#define MORPH_FRAMES 4

enum { MORPH_CLASSIC = 0, MORPH_GROWL, MORPH_TABLE_COUNT };

/* one section of a frame: pole frequency (ratio to the cutoff; 0 = the section is off), its Q, the zeros' frequency
 * (ratio; 0 = at DC, MORPH_NYQ = at Nyquist) and depth (0 = no zero, 1 = a full zero), its gain at the pole. A depth
 * of -1 makes a peak: zeros at the pole's frequency, placed so the gain is 1 away from it and `gain` on it. A section
 * keeps its kind (off, peak, other) in every frame of a table. */
#define MORPH_NYQ 1000.0f
typedef struct {
    float pole, q, zero, depth, gain;
} morph_section_t;

typedef struct {
    float b0[MORPH_SECTIONS], b1[MORPH_SECTIONS], b2[MORPH_SECTIONS], a1[MORPH_SECTIONS], a2[MORPH_SECTIONS]; /* now */
    float t0[MORPH_SECTIONS], t1[MORPH_SECTIONS], t2[MORPH_SECTIONS], u1[MORPH_SECTIONS], u2[MORPH_SECTIONS]; /* target */
    float s1[MORPH_SECTIONS], s2[MORPH_SECTIONS]; /* the sections' state (transposed direct form II) */
    float lim[MORPH_SECTIONS]; /* each section's gain at its pole: where its drive's ceiling sits */
    float top;                 /* the cascade's highest gain (at one of its resonances): the feedback's scale */
    float sr, cutoff, position, reso, drive, feedback, follow, env, env_up, env_down, last;
    int active; /* sections in use (the tables keep the off ones at the end) */
    int table, snap;
} morph_t;

void morph_init(morph_t *m, float sample_rate);
void morph_reset(morph_t *m);
/* table (MORPH_CLASSIC, MORPH_GROWL), cutoff in Hz (what a ratio of 1 means), position 0..1 across the frames,
 * reso 0..1 (the sections' Q up to x8), drive 0..1 (0 = linear), feedback -1..1 (the saturated output back into
 * the input), follow 0..1 (how far the input's level pushes the position up). */
void morph_set(morph_t *m, int table, float cutoff_hz, float position, float reso, float drive, float feedback,
               float follow);
void morph_process(morph_t *m, const float *in, float *out, int n);
/* The frames' sections of a table at a position (for displays and tests): MORPH_SECTIONS sections into out. */
void morph_frame(int table, float position, morph_section_t *out);

#endif /* MORPH_H */
