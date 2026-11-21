/* voicemod.h — per-voice modulation, the same for every engine: each voice has its own MSEG and LFO, its note,
 * velocity and random value, and its MPE expression (per-note pitch bend, pressure, slide); 4 routes send
 * them to five voice destinations every engine understands: pitch (semitones), level (dB), pan, brightness
 * (octaves of the engine's main brightness control) and timbre (-1..1 on the engine's main timbre control).
 * Microtuning is a per-voice pitch offset too. Lives in vpoly; engines read vpoly_vmod(). */
#ifndef VOICEMOD_H
#define VOICEMOD_H

#include "../core/fastmath.h"
#include "../env/mseg.h"

enum { VM_PITCH = 0, VM_LEVEL, VM_PAN, VM_BRIGHT, VM_TIMBRE, VM_DST_COUNT };
enum { VM_SRC_NONE = 0, VM_SRC_MSEG, VM_SRC_LFO, VM_SRC_VELOCITY, VM_SRC_NOTE, VM_SRC_RANDOM, VM_SRC_PRESSURE,
       VM_SRC_SLIDE, VM_SRC_COUNT };
#define VM_ROUTES 4

/* Full-scale effect of amount 1 on each destination. */
#define VM_PITCH_RANGE 12.0f /* semitones */
#define VM_LEVEL_RANGE 24.0f /* dB */
#define VM_BRIGHT_RANGE 4.0f /* octaves */

/* Fourier LFO shape: value at phase ph (cycles) of sum(a cos 2 pi h ph + b sin 2 pi h ph) over harmonics h = 1..8,
 * c[2(h-1)] = a, c[2(h-1)+1] = b, divided by sum(|a| + |b|) when that exceeds 1 (it stays within -1..1). */
/* Wiener LFO shape: a periodic random walk. A Brownian bridge (a random walk over one cycle, brought back to where it
 * started, so the cycle joins without a jump) of VM_WIENER_N points, centred and scaled to a peak of 1, read with
 * linear interpolation: slow random drift with the spectrum of Wiener noise, the same at every cycle. */
#define VM_WIENER_N 32
static inline void voicemod_wiener_build(float *w, uint32_t seed)
{
    uint32_t r = dsp_seed(seed);
    float x = 0.0f, mean = 0.0f, peak = 1e-6f;
    for (int k = 0; k < VM_WIENER_N; k++) {
        w[k] = x;
        x += dsp_rand_bipolar(&r);
    }
    for (int k = 0; k < VM_WIENER_N; k++) { /* the bridge: the end comes back to the start */
        w[k] -= x * (float)k * (1.0f / (float)VM_WIENER_N);
        mean += w[k];
    }
    mean *= 1.0f / (float)VM_WIENER_N;
    for (int k = 0; k < VM_WIENER_N; k++)
        peak = dsp_maxf(peak, dsp_absf(w[k] - mean));
    for (int k = 0; k < VM_WIENER_N; k++)
        w[k] = (w[k] - mean) / peak;
}
static inline float voicemod_wiener(const float *w, float ph)
{
    const float x = ph * (float)VM_WIENER_N;
    const int i = (int)x & (VM_WIENER_N - 1);
    return w[i] + (w[(i + 1) & (VM_WIENER_N - 1)] - w[i]) * (x - (float)(int)x);
}

#define VM_FOURIER_H 8
static inline float voicemod_fourier(const float *c, float ph)
{
    float y = 0.0f, norm = 0.0f;
    for (int k = 0; k < VM_FOURIER_H; k++) {
        const float a = c[2 * k], b = c[2 * k + 1];
        if (a == 0.0f && b == 0.0f)
            continue;
        const float p = ph * (float)(k + 1);
        y += a * fm_cos(p) + b * fm_sin(p);
        norm += dsp_absf(a) + dsp_absf(b);
    }
    return norm > 1.0f ? y / norm : y;
}

typedef struct {
    mseg_shape_t shape;               /* the per-voice MSEG */
    float lfo_rate;                   /* Hz, or a division when lfo_div > 0 */
    int lfo_div, lfo_wave;            /* tempo division (0 = free), shape 0..6 (sine, tri, saw, square, sh, fourier, wiener) */
    float fourier[2 * VM_FOURIER_H];  /* the fourier shape's a, b of harmonics 1..8 */
    float wiener[VM_WIENER_N];        /* the wiener shape */
    int src[VM_ROUTES], dst[VM_ROUTES];
    float amount[VM_ROUTES];
    int routes;                       /* number of usable routes (0: no per-voice modulation) */
    const float *tuning;              /* 12 offsets in semitones by pitch class from tuning_root, or NULL */
    int tuning_root;
    int mpe;                          /* per-note pitch bend, pressure, slide are used */
    float mpe_bend_range;             /* semitones */
    float dt, beats_per_s;            /* block length (s) and tempo (beats per second), set each block */
} voicemod_cfg_t;

typedef struct {
    mseg_t env;
    float lfo_phase, lfo_sh;
    float src[VM_SRC_COUNT];
    float tune;                       /* microtuning offset of the note (semitones) */
    float bend;                       /* MPE per-note bend, -1..1 */
    float out[VM_DST_COUNT];
    float gain_prev;                  /* linear level reached at the end of the previous block */
    float pan_prev;                   /* pan offset (-1..1) reached at the end of the previous block */
    int fresh;                        /* the level ramp starts from the first value */
    uint32_t rng;
} voicemod_t;

void voicemod_init(voicemod_t *m, uint32_t seed);
void voicemod_start(voicemod_t *m, const voicemod_cfg_t *c, int note, float velocity, int hard);
void voicemod_note(voicemod_t *m, const voicemod_cfg_t *c, int note); /* legato: new note, envelope goes on */
void voicemod_release(voicemod_t *m, const voicemod_cfg_t *c);
/* Once per block: updates out[] (all zero when nothing is configured). */
void voicemod_block(voicemod_t *m, const voicemod_cfg_t *c);
/* 1 if this configuration can move anything at all (routes, tuning, MPE). */
int voicemod_cfg_active(const voicemod_cfg_t *c);

#endif /* VOICEMOD_H */
