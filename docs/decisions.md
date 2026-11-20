# Decision log

Newest first. Each entry: what was decided, and why.

## 2026-11-18 — ladder filter and classic instruments (M2 done)

- **ZDF ladder** (`dsp/filter/ladder`): 4 TPT one-poles, zero-delay feedback solved per sample,
  2 tanh(u/2) input stage (bounds self-oscillation), LP24/18/12, coefficients interpolated per block,
  optional 2x oversampling with a 47-tap polyphase half-band (12 coefficient pairs, -56 dB stopband).
  Tested: -12 dB at fc, self-oscillation at fc, stable under modulation, aliasing -32 -> -50 dB with 2x.
- **Engines** `acid` (one voice, 18 dB ladder 2x, accent with cumulative sweep, slide via legato),
  `mono` (3 oscillators, footage, osc 3 modulator, mixer drive, 24 dB ladder 2x, ADS contours with
  release switch, glide), `poly` (8 voices, custom osc A with event-placed polyBLEPs and a one-sample
  delay so hard sync and audio-rate poly-mod FM stay clean, poly-mod, 24 dB ladder, unison driving all
  voices from one note). Names avoid trademarks; behaviour from public documentation.
- polyBLEP residuals are exposed in `polyblep.h` for engines that build their own oscillators.
- Not yet: the 6-operator FM engine (M4), which comes next.

## 2026-11-18 — OID 0.2: self-describing, live-aware, role-based

- Each descriptor now carries `role` (shared vocabulary, unique per engine) and `realtime` (formerly
  `modulatable`: safe to move live, i.e. CC-assignable); the separate role mapping file is gone.
- The catalog is keyed by name (`engines.va.params.cutoff`), with `type`, `range`/`choices`, `scale`,
  inline default `cc` and `voices` per engine; non-parameter MIDI under `midi`.
- Patches may use roles as keys (portable patches); controller maps (`controllers/*.json`) map CCs to
  roles/names tried in order, so one layout drives every engine (`synth_play --controller`).
- The table test enforces a unique role per parameter; the validator checks roles against
  `spec/roles.json`, patches/songs against the catalog, and controller targets.

## 2026-11-18 — mono/legato/glide, glitch kit, open format

- **Monophonic play in `vpoly`** (engine-independent): mono / legato, last-note priority with return
  to the previous held key, optional `legato` callback (note change without retrigger), `overlap`
  flag for engines to decide on glide. `va`: `voice_mode`, `glide` (constant-time exponential),
  `glide_mode` (always / legato). Phrases: `~` slide suffix.
- **Glitch kit**: third `drums` kit (gates, sample-and-hold, quantization steps, ring modulation).
  Clicks are now added before their decay is applied (a coefficient of 0 = exactly one sample).
- **CC maps and named notes are engine data** (`engine_ops_t.cc_map`, `.notes`), applied generically
  by synth.c and exported by the catalog; `engine_ops_t` tables use designated initializers.
- **Open format (OID draft 0.1)**: `spec/README.md`, JSON schemas, role vocabulary (`spec/roles.json`)
  and the mapping of this synth (`spec/mappings/synth.json`); `tools/validate.py` checks every file
  (schemas + catalog + mapping) and runs in ctest. Licence of the spec: the owner's choice.

## 2026-11-18 — classic instruments: drum machine engine, step grids, swing, pumping

- **Drum engine `drums`** (12 instruments on GM notes, kits `analog` / `hybrid`, 4 parameters per
  instrument, velocity = accent, closed-hat choke). No trademarks in code or names (CLAUDE.md): the
  kits are described by era and behaviour; behaviour reconstructed from public circuit analyses.
- **Step grids** in songs (`"pattern"`: rows of `x X o .` per instrument or note, sections with
  `repeat`), per-note accents inside chords (`C2!+F#2?`), `swing`, and beat-locked `pump`
  (sidechain-style ducking) per track. Host-side features: the board will get a pattern player later.
- Next classic instrument: a 303-style monophonic acid bass, which needs the 4-pole ladder (M2).

## 2026-11-18 — physical piano, pitch envelope, humanize

- **Modal piano**: stiff-string partials computed from `inharmonicity` (B), up to 32 modes, 12 voices,
  `unison` = 2 detuned strings for the first 8 partials with different decays (Weinreich double decay),
  `aftersound` sets that contrast (0 reproduces the first version exactly: the owner's favourite
  `piano_ambient` is kept bit-identical), `decay_keytrack`, velocity -> hammer hardness.
- **va pitch envelope** (`pitch_env`, `pitch_env_decay`): drums and zaps without a drum engine.
- **Humanize** in phrases (`humanize_velocity`, `humanize_ms`): deterministic, chord notes roll.
- **Descriptor table test**: every table entry i must have id i, valid range, unique name, labels
  matching the range (caught a misplaced entry during this change).

## 2026-11-18 — refactor for extensibility, effect bus, modal engine

- **Engines behind an interface** (`engine_ops_t`, `dsp/engines/engine.h`) and a registry
  (`registry.c`, state union in `registry.h`). `synth.c` has no engine-specific code any more;
  an engine = one file + one registry line (`docs/adding_an_engine.md`).
- **Shared polyphony manager** `vpoly` (allocation, 3 ms steal fade, sustain, voice lifetime): the
  duplicated logic left `sine.c`/`va.c`. Refactor verified by the unchanged, bit-exact test suite.
- **Descriptors carry labels and help** (`PARAM(...)` macro). The host catalog no longer duplicates
  them; `docs/catalog.*` and patch validation derive everything from the DSP tables. Help strings
  can be compiled out on targets (`DSP_PARAM_TEXT=0`).
- **Effect bus** (`dsp/fx/fxbus`): chorus (insert) -> tape delay (send) -> plate reverb (send fed by
  dry + echoes). 15 global parameters appended to `synth.h` after `SYNTH_PARAM_MASTER_DB` (approved
  by the owner), mirrored by static assertions. Standard CCs: 91 reverb, 93 chorus, 94 delay, 69 freeze.
  Effects whose mix stays at 0 are fed silence and stop being computed after 20 s. Memory: static
  arena sized per target; too small = smaller reverb / shorter delay, never a failure.
- **Tape delay**: Hermite-interpolated lines, delay changes glide like tape (but snap when the effect
  starts from idle), wow + flutter + slow random drift, HP/LP/tanh in the loop, ping-pong.
- **Chorus**: BBD-style modes I / II / I+II (antiphase triangle LFOs) and a 3-phase ensemble.
- **Reverb freeze**: tank gain 0.9999, damping off, input closed, smoothed over ~50 ms.
- **Modal engine**: complex one-pole resonators (exact frequency/decay, stable), 8 physical mode-ratio
  models + `stretch` for invented materials; strike = raised-cosine pulse (hardness = contact time)
  + noise burst; bow = filtered noise scaled by sqrt(1 - r^2) per mode so its level does not depend on
  decay. Coefficients rebuilt per block only when a parameter, the bend or the release state changes;
  pitch therefore moves in block steps (phase-continuous), an accepted exception to per-sample pitch
  interpolation.

## 2026-11-17 — supersaw

- **Supersaw as an oscillator wave** (`dsp/osc/supersaw`, `osc*_wave = "supersaw"`): up to 7 polyBLEP
  saws per oscillator (14 per note with both), irregular slightly asymmetric detune offsets (no common
  beat period), random phases per note, side/centre mix normalized by 1/sqrt(1 + (n-1)·side²) so
  loudness stays within ~1 dB, and one slow per-saw LFO on its detune (rates 0.6–1.4x random) for
  motion. Shared parameters `supersaw_*` (appended ids). Cost: ~3x the 2-oscillator voice on the
  host (36 vs 11 ns/voice/sample); to be measured on the boards (F746 budget is the tight one).

## 2026-11-17 — sound-design tooling, LFO 2, plate reverb

- **Sounds are data.** Patches (`patches/*.json`) and songs (`songs/*.json`) are versioned JSON
  validated against the descriptor tables (host side, `host/patch.c`, `host/song.c`). An AI or a
  human designs sounds without writing C. Format: `docs/patch_format.md`.
- **Catalog is generated** (`synth_catalog` -> `docs/catalog.md` / `.json`) from the descriptors plus
  one-line help and labels in `host/catalog.c`, so it never drifts from the code.
- **Measurement loop**: `tools/analyze.py` (envelope, pitch, centroid, noisiness, bands, movement,
  stereo, distance to a reference) + spectrogram PNG. Measurements are taken on a single-note probe;
  phrases are for listening. Claude skill: `.claude/skills/sound-design/SKILL.md`.
- **va gets its second LFO** (planned for `va`): one instance per voice, modulating the detune of
  osc 2, with per-voice phase/rate spread, and LFO 1 modulating its rate (octaves) and depth
  ("LFO of LFO"). New parameter ids are appended (ids of existing parameters unchanged).
  `lfo_rate` minimum lowered from 0.05 to 0.01 Hz for very slow meta-modulation.
- **LFOs 3 and 4** (same day, owner's request): per voice, on osc 1 detune and on the whole voice
  pitch. Default rates in irrational ratios to LFO 2 (x0.764, x0.236), independent random phase and
  rate per LFO and voice, and LFO 1 pushes their rates in different directions (+1, -1, +0.5 octave
  factor) so the three never line up. Unused LFOs (depth 0) are skipped.
- **Dattorro plate reverb pulled forward from M6** (`dsp/fx/reverb`), memory supplied by the caller.
  Used by the song renderer only; **not yet on the synth master bus** — adding it there means new
  global parameters in `synth.h`, to be agreed first.

## 2026-11-17 — M0 + M1

- **All code in English** (identifiers, comments, messages), at the owner's request. Replaces the
  former rule "comments in French".
- **API without a handle.** `synth.h` exposes `synth_init(float sr)`, `synth_set_engine`,
  `synth_midi(msg, len, sample_offset)`, `synth_set_param(id, norm)`, `synth_render(L, R, n)`.
  The state therefore lives in a single static instance (`g_synth` in `synth.c`); every internal
  function takes a pointer, so a handle can be added later without touching the modules.
- **One thread.** All `synth_*` calls come from the audio thread. Everything (MIDI, parameters,
  engine switch) goes through a 256-entry time-stamped event queue, applied at block boundaries in
  arrival order. Timing resolution is one block (0.67 ms at 48 kHz). The host forwards MIDI through
  lock-free SPSC rings.
- **`synth_set_param` takes a normalized value** (0..1). The descriptor table (`param_desc_t`) converts
  it to physical units; engines only see physical units. Ids 0..255 belong to the current engine,
  >= 256 are global (`SYNTH_PARAM_MASTER_DB`).
- **Engine switch** fades the current engine out over one block when voices are sounding, then
  re-initialises the new one; queued events wait for the switch.
- **Sine table** is a generated `const` array (`tools/gen_sin_table.py`), not filled at init:
  no mutable global, lives in flash on the board.
- **Filter coefficients interpolated linearly** (a1, a2, a3, k) over the block instead of
  recomputing `1/(1+g(g+k))` per sample: no division in the inner loop. End points are exact.
- **Oscillator 1/dt interpolated linearly** over the block (same reason), residual position clamped.
- **ADSR** uses the "target ratio" one-pole: attack target 1.3, decay/release ratio 1e-4. Set times
  are exact segment durations (tested at ±2 %). The amp envelope runs per sample (0.5 ms attacks
  are shorter than a block); the filter envelope is sampled at block end.
- **Master**: smoothed gain → DC blocker (10 Hz) → soft clipper, linear up to -6 dBFS then
  tanh knee up to 0 dBFS. Voices at -18 dBFS.
- **Voice stealing**: 3 ms linear fade, new note starts after the fade (≤ 3 ms latency on stolen voices).
- **Aliasing target** for 2-point polyBLEP: aliases below 10 kHz under -50 dB (f0 ≤ 4 kHz) and at
  least 8 dB better than naive over the whole band. Near Nyquist 2-point polyBLEP only gains ~9 dB
  (aliases around -30 dB for high notes, above 20 kHz). Improve later with a 4-point BLEP or 2×
  oversampling if needed.

## 2026-11-18 — fm6 (6-operator FM)
- Native 0..99 parameter scales, so imported sysex voices map 1:1 and the help text explains each scale.
- Envelopes run in dB at control rate (96 dB sweep in 60 s x 2^(-rate/7.3)), amplitude interpolated per sample.
  The curve is tuned by ear, not bit-exact with the original hardware.
- Operator key sync resets phases only when a voice starts from silence (no click on a repeated note).
- Carriers are mixed with a 1/sqrt(n) gain. The sysex reader lives on the host (`host/dx7_sysex.c`); no factory banks are shipped.

## 2026-11-18 — wavetable engine `wt`
- Tables are generated from formulas (no sampled material) and band-limited by FFT at init, one mip level per
  octave (512 harmonics down to 1); levels with few harmonics are stored on >= 512 samples so the images of
  linear interpolation stay below -75 dB (measured: -91 dB for a square at 1760 Hz, -78 dB for `digital`).
- Mip level chosen so harmonics stay below 0.55 x SR: what folds back lands above 20 kHz.
- Sync warp: reset step polyBLEP-corrected (+9 dB); the slope break is not (-50 dB near 20 kHz). A polyBLAMP
  correction would be the next refinement.
- Read-only engine data lives in a shared arena of synth.c (`engine_ops_t.shared_*`), built once, outside the
  audio path; engines cope with less memory. Voices stay mono (architecture rule): no stereo unison.
- Generic modulation matrix `dsp/mod/modmatrix.h` (slots src/dst/amount/via) — first piece of M5.

## 2026-11-18 — DSP elements out of the engines
- An engine wires modules and holds its parameters; DSP elements live in dsp/: `filter/resonator.h` (complex
  one-pole mode, used by `modal` and `drums`), `osc/fm_op.h` (operator with phase-modulation input),
  `env/env_rl` (rate/level envelope). `drums` keeps generic voice parts and one recipe function per instrument.
- Each move was checked bit-exact with tools/golden.py (76 renders). Remaining candidate: the strike and bow
  exciters of `modal` (`osc/exciter`).

## 2026-11-18 — parts, tempo, modulation (docs/design_parts_modulation.md)
- 8 parts (compile-time SYNTH_PARTS), one shared serial effect bus with one send per part; the default state
  (part 0 alone, omni, send 1, 0 dB, centre balance) is the old single-engine path, bit for bit.
- MIDI clock: tick period smoothed (one-pole, outliers restart the estimate), beat locked to the tick count and
  extrapolated between ticks. Tempo-synced: delay time, part LFOs, MSEG times in beats.
- Modulation per part (not per voice) so it works for every engine without touching them: routes write
  base + offset (normalized, clamped) through the engine's set_param once per block, only for realtime
  parameters; the base comes back when a route goes away. Untouched when nothing is routed.

## 2026-11-18 — glide layer, per-voice modulation, tuning, MPE, comb
- One glide layer for every engine (partmod): realtime parameters changed while notes sound go through a
  5 ms one-pole; with nothing sounding they apply at once, so patch loading stays bit-exact.
- Per-voice modulation lives in vpoly (shared by every engine): five destinations every engine understands
  (pitch, level, pan, brightness, timbre); level and pan are applied in voice_out, the rest in each engine,
  always as a term added at the end of the existing expression (bit-exact when nothing is configured).
- Microtuning and MPE are per-voice pitch offsets on the same path; tunings are 12-degree octave scales set by
  global parameters (no API change); non-12-note scales are out of scope for now.
- Comb: 512-sample delay line (fits a board's DTCM); notes below ~95 Hz resonate an octave up.

## 2026-11-18 — filter characters
- `vfilter` (one slot = SVF lp/bp/hp/notch, formant, MS-20) is shared by va (two slots) and wt; the SVF path calls
  exactly the old functions (bit-exact). The comb stays in wt only (its 2 KB delay line per voice).
- MS-20 style filter: Korg 35 topology (two TPT low-passes, high-pass in the feedback), ZDF solved per sample,
  saturating node, 2x oversampled by default; K up to 2.2 because the feedback band-pass peaks at 0.5.
- The ladder's half-band is now the shared `halfband.h` (bit-exact).

## 2026-11-18 — coupled-oscillator engine
- Phases stored brought back to the fundamental (theta = phi / h, wrapped in [0, 1)): float-exact for any
  length; audio phase = h theta (h integer); the coupling compares thetas (n:m harmonic coupling).
- Phase pull relative to the partial's frequency (like the detune, in cents): same capture range for every
  partial (an absolute pull in Hz could not lock the high partials; found by the locking test).
- Couplings averaged over the neighbours (reach does not change their strength); the diffusive self term is
  capped just below the self-excitation so freeze sinks partials without ever silencing a note.
- Couplings integrated at the block rate (1.5 kHz), sines and levels interpolated per sample.

## 2026-11-18 — modal string nonlinearity (tension)
- Tension modulation as a rotation of every mode (speed-up 1 + g T, T = sum (f_k / f0)^2 y_k^2): stable by
  construction. A first version adding a restoring force to the modes diverged (found by the tests).
- Measured: a loud C4 glides 5 cents at tension 0.6, partial 8 with it. The transverse-only model does not
  produce audible phantom partials (they come mostly from longitudinal waves in a real piano): not claimed.
- Default 0: the linear path is untouched (piano_ambient bit-identical).

## 2026-11-18 — no implicit FMA (-ffp-contract=off on dsp/ and midi/)
- Implicit a*b+c fusion depends on the compiler and the optimization level, so Release, Debug and the board
  rounded differently (found when piano_tension's hash differed between Release and Debug). With it off, the
  same code gives the same bits everywhere; all 90 reference hashes were re-recorded (owner's decision: the
  difference on piano_ambient is inaudible, -63 dB, but it is no longer bit-identical to the old reference).
- Cost on the host: +10..40 % per engine (bench.md). Later: explicit fused multiply-adds in the hot loops would
  give the speed back and stay deterministic (an explicit FMA rounds the same on every FPU that has it).

## 2026-11-18 — self-oscillating oscillator (selfosc)
- Integrator: symmetric Strang split, exact half dampings around a leapfrog spring (second order, stable for
  any drive). A first-order split was 0.6 % (10 cents) off at 440 Hz and worse higher: found by the tuning test.
- The free cycle's pitch drop with the drive is compensated by a table measured at init with the same
  integrator; consecutive half dampings are merged (one exponential per sub-step).
- 4x oversampling (two half-band stages): -50 dB aliasing at 1760 Hz, drive 5. Cost 124 ns/voice/sample on the
  host: for the board, 2x oversampling or shorter decimation filters.
- Stiffness is hardening only (0..2): a softening spring lets the cycle escape and stick.

## Web host (WebAssembly)

A third host next to macOS and the boards: `dsp/` compiled as is by emscripten into a standalone wasm module (no JS runtime, which does not load inside an AudioWorklet), played in an AudioWorklet. For prototyping and sharing sounds with no install. It changes nothing in `dsp/`: if the web ever required it, that would be a portability bug. The real-time reference stays `synth_play`.

## Plugin: CLAP

The plugin host is CLAP (a C API, MIT license, headers in `third_party/clap`) rather than JUCE (C++, AGPLv3 or a paid
license): it is written in C11 like the engine and stays a thin layer over `dsp/synth.h`, like `synth_play` and the
web worklet. Its state is a patch file. VST3 / AU, when needed, come from a wrapper around it. Each plugin
instance owns an engine instance (see "A handle per instance").

## 2026-11-19 — A handle per instance

The API now takes the instance it acts on (`synth_t *`): `synth_create(mem, bytes, sr)` places one in memory the
caller provides (`synth_size()` bytes: state and delay/reverb memory; `dsp/` still never allocates),
`synth_default()` is the static instance (`DSP_FAST_DATA` state, `DSP_LARGE_DATA` effect memory) for the board,
the tools and the tests. Supersedes "API without a handle" (2026-11-17): the internal functions already took a
pointer, so only the seven public functions changed, and no sound did (golden hashes unchanged). Why: a plugin
host runs several instances, possibly on several threads. The engines' read-only tables stay one shared static
arena, built by the first `synth_init`.

