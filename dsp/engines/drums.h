/* drums.h — drum machine engine: 12 instruments on General-MIDI note numbers, four kits:
 * "analog" (in the spirit of the classic early-80s analog machines: resonant sine kick with a long
 * boom, two-tone + noise snare, 6 detuned square "metal" oscillators for hats/cymbal, multi-burst clap,
 * cowbell from two squares), "hybrid" (mid-80s analog/sample hybrids: punchy waveshaped kick with a
 * deep pitch sweep and click, brighter snappy snare, grittier 6-bit-quantized metal) and "glitch"
 * (digital microsound: gated sub sine pulses, single-sample clicks, pure high beeps, white and
 * sample-rate-reduced data noise, ring-modulated modem chirps, on precise grids).
 * Behaviour reconstructed from published circuit analyses and listening; no code or samples reused.
 * A fourth kit, "trap": short punchy kick, tight cracking snare, layered roomy clap, crisp bright hats. Under
 * the drum map (notes 0..34) a melodic bass drum plays at the note's pitch: a long sine boom with drive, one at a
 * time, sliding from the previous one when the notes overlap (glide).
 * Velocity acts as accent. The closed hat chokes the open hat. */
#ifndef ENGINE_DRUMS_H
#define ENGINE_DRUMS_H

#include "../filter/resonator.h"
#include "../filter/svf_tpt.h"
#include "../osc/polyblep.h"
#include "engine.h"

#define DRUMS_VOICES 16

enum {
    DR_KICK = 0, DR_SNARE, DR_CLAP, DR_RIM, DR_CHAT, DR_OHAT,
    DR_LTOM, DR_MTOM, DR_HTOM, DR_CYMBAL, DR_COWBELL, DR_CLAVE,
    DR_BASS, /* melodic bass drum, notes 0..34 */
    DR_COUNT
};

/* Global parameters, then 4 per instrument: DRUMS_P(inst, DRUMS_LEVEL..DRUMS_TONE). */
enum { DRUMS_P_KIT = 0, DRUMS_P_ACCENT, DRUMS_P_DRIVE, DRUMS_P_STEREO, DRUMS_P_VOLUME, DRUMS_P_INST_BASE };
enum { DRUMS_LEVEL = 0, DRUMS_TUNE, DRUMS_DECAY, DRUMS_TONE, DRUMS_PER_INST };
#define DRUMS_P(inst, k) (DRUMS_P_INST_BASE + DRUMS_PER_INST * (inst) + (k))
#define DRUMS_P_GLIDE DRUMS_P(DR_COUNT, 0) /* bass drum slide time, after the instruments */
#define DRUMS_P_COUNT (DRUMS_P_GLIDE + 1)

extern const param_desc_t drums_params[DRUMS_P_COUNT];
extern const engine_ops_t drums_engine;
/* Instrument names ("kick", "snare", ...) and the main MIDI note of each (for sequencers). */
extern const char *const drums_instrument_names[DR_COUNT];
extern const uint8_t drums_instrument_notes[DR_COUNT];
/* Instrument for a MIDI note (GM drum map), or -1. */
int drums_instrument_for_note(int note);

typedef struct {
    int inst, kit;
    float gain, pan_l, pan_r;
    float ph[2], f[2];                    /* tone oscillators: phase (cycles), base frequency */
    float pe_a, pe_a_coef, pe_a_st;       /* fast pitch envelope (level, per-block coef, semitones) */
    float pe_b, pe_b_coef, pe_b_st;       /* slow pitch envelope */
    float env_tone, c_tone;               /* amplitude envelopes (per-sample coefficients) */
    float env_noise, c_noise;
    float env_metal, c_metal;
    float env_metal2, c_metal2;           /* second, slower metal stage (cowbell) */
    float env_click, c_click;
    float env_burst, c_burst;             /* clap bursts */
    int burst_left, burst_period, burst_timer;
    float mix_tone, mix_tone2, mix_noise, mix_metal, mix_click, metal_noise, shape, shape_norm;
    osc_t metal[6];
    int metal_n;
    float qsteps;                         /* amplitude quantization steps (32 = 6 bits), 0 = off */
    int hold_tone, hold_noise;            /* gate: samples at full level before the decay starts */
    int sh_period, sh_count;              /* noise sample-and-hold (sample-rate reduction) */
    float sh_val;
    int ring;                             /* tone osc 2 ring-modulates osc 1 */
    int held;                             /* its key is down (bass drum: a new note slides from it) */
    int noise_f2;                         /* noise also goes through f2 (snare: high-pass + low-pass) */
    float noise_att, noise_att_step;      /* noise attack ramp (softens the wire burst) */
    svf_t f1, f2;
    reso_t reso[2];
    uint32_t rng;
    float level;                          /* rough current amplitude, for stealing and voice end */
} drum_voice_t;

typedef struct {
    float sr;
    float p[DRUMS_P_COUNT];
    uint32_t rng;
    vpoly_t poly;
    drum_voice_t v[DRUMS_VOICES];
    float bass_pitch; /* the last bass drum note (MIDI, with its tune), where the next one slides from */
} drums_t;

#endif /* ENGINE_DRUMS_H */
