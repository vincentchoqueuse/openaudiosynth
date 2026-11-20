/* fm_op.h — one FM operator: sine oscillator with a phase-modulation input (phase modulation, as on the
 * classic digital FM instruments), frequency and amplitude interpolated linearly over each block.
 * Algorithms (who modulates whom) belong to the engine. Reference: J. Chowning, "The Synthesis of Complex
 * Audio Spectra by Means of Frequency Modulation" (JAES 1973). */
#ifndef FM_OP_H
#define FM_OP_H

#include "../core/fastmath.h"

typedef struct {
    float phase;          /* cycles, [0, 1) */
    float inc, amp;       /* running values inside the block */
    float dinc, damp;     /* per-sample steps */
    float inc_end, amp_end; /* targets of the current block (the next block starts from them) */
} fm_op_t;

/* New note: phase, and no pitch glide from the previous note (the frequency starts on its target; the amplitude
 * ramps from zero over the first block, which is what avoids a click). */
static inline void fm_op_reset(fm_op_t *o, float phase)
{
    o->phase = phase;
    o->inc_end = o->amp_end = 0.0f;
}

/* Once per block: ramp to frequency inc1 (cycles per sample) and amplitude amp1 over the block (inv_n = 1/n). */
static inline void fm_op_block(fm_op_t *o, float inc1, float amp1, float inv_n)
{
    o->amp = o->amp_end;
    o->damp = (amp1 - o->amp_end) * inv_n;
    o->amp_end = amp1;
    const float inc0 = o->inc_end > 0.0f ? o->inc_end : inc1; /* first block of a note: no glide */
    o->inc = inc0;
    o->dinc = (inc1 - inc0) * inv_n;
    o->inc_end = inc1;
}

/* One sample with phase modulation pm (cycles). */
static inline float fm_op_tick(fm_op_t *o, float pm)
{
    o->amp += o->damp;
    o->inc += o->dinc;
    o->phase += o->inc;
    if (o->phase >= 1.0f)
        o->phase -= 1.0f;
    return o->amp > 0.0f ? fm_sin(o->phase + pm) * o->amp : 0.0f;
}

#endif /* FM_OP_H */
