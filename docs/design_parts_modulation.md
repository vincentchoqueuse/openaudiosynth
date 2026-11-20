# Design: multitimbral parts, tempo, modulation for every engine

Status: approved and implemented (2026-11-18), except the multi file and `synth_play --multi` (next). Patches
set modulation with plain parameters (`route1_destination: "cutoff"`), not separate sections.

## 1. Parts (multitimbral)

- `SYNTH_PARTS` parts (compile-time: 8 on the host and the H745, 4 on the F746; one part's state is at most
  40 KB today, so 8 parts ~ 340 KB). Each part = one engine instance + its own parameters + a mixer strip.
- A part listens to one MIDI channel (1..16) or to all (omni), within a key range (splits, layers).
- Mixer strip per part: level (dB), pan, mute, transpose, key low/high, effect send (0..1).
- Effects stay one shared serial bus (chorus -> delay -> reverb). Each part sends `send x signal` into the bus
  and `(1 - send) x signal` around it. Default send = 1: identical to today.
- **Backward compatible, bit for bit**: at init only part 0 is active, omni, send 1. `synth_set_engine(e)` and
  `synth_set_param(id, v)` keep working and address part 0. Every existing patch and song renders the same
  (checked by `golden`).

New public functions (the only API change):

```c
#define SYNTH_PARTS 8                                     /* overridable by the target */
void synth_part_engine(synth_t *s, int part, synth_engine_t engine);  /* SYNTH_ENGINE_NONE (-1) = part off */
void synth_part_param(synth_t *s, int part, int id, float value);     /* engine ids, part ids, modulation ids */
```

Part parameter ids (one descriptor table, so they appear in the catalog like any other parameter):
`SYNTH_PART_LEVEL, _PAN, _MUTE, _CHANNEL (0 = omni), _KEY_LO, _KEY_HI, _TRANSPOSE, _FX_SEND`.

## 2. Tempo and MIDI clock

- Global parameters `SYNTH_PARAM_TEMPO` (20..300 BPM) and `SYNTH_PARAM_CLOCK` (internal / MIDI).
- `synth_midi` accepts clock messages: 0xF8 (24 per quarter note), start 0xFA, continue 0xFB, stop 0xFC. The
  tempo follows the clock with smoothing (no jitter on the LFOs); start resets the beat position.
- Every engine and the modulation layer can read the tempo and beat position (internal context, not public API).
- Tempo sync: LFOs (division 1/32 .. 8 bars, dotted, triplet) and the tape delay (`SYNTH_PARAM_DELAY_SYNC`).

## 3. Modulation for every engine (per part)

A generic layer between the part's parameters and its engine, evaluated once per block:

- **Sources**: 8 macros (knobs, 0..1), 2 LFOs (tempo-syncable), 2 MSEGs, mod wheel, aftertouch, pitch bend,
  velocity and note of the last note, random value per note, constant.
- **Destinations**: any parameter of the engine marked `realtime` (the catalog already says which).
- **Matrix**: 8 slots `{source, destination, amount, via}` (the `dsp/mod/modmatrix.h` already used by `wt`),
  added in the destination's normalized domain, clamped; the engine receives the modulated value through its
  usual `set_param`, once per block (block ramps = the CLAUDE.md smoothing rule).
- **Macros**: a macro is a source; one knob can drive several destinations with their own amounts. MIDI CCs
  and the controller maps (`controllers/*.json`) point at macros, so one knob on the MiniLab or the Launch
  Control morphs a whole sound.
- **MSEG** (`dsp/env/mseg`, Absynth-style): up to 16 points (time, level, curve -1..1), sustain point, loop
  start/end, modes none / loop while held / free loop, optional tempo sync. Retriggered by the part's notes
  (first note only in legato).
- This layer is per part (one value for all the voices of the part). Per-voice modulation (one envelope per
  note) stays inside the engines (`wt` has its own matrix); the MSEG module is reusable there later.

## 4. Formats and tools

- Patches gain optional `macros`, `modulation` and `mseg` sections. A new file type, the **multi** (a live
  scene), lists up to 8 parts: patch, channel, level, pan, send. Open format OID 0.3: schemas, validator,
  roles, catalog updated; `synth_play --multi scene.json` plays it live.
- Songs keep their current rendering (no sound change).

## 5. Costs and limits

- Modulation layer: a few hundred operations per part per block; negligible.
- CPU: parts share the budget; on the board the number of voices and unison copies per engine will be capped by
  target macros after measurement.
- Tests: parts (routing by channel and key range, mixer, sends, compatibility), MIDI clock (tempo lock, jitter),
  modulation (each source, macro fan-out, clamping), MSEG (times, loops, sustain, curves).
