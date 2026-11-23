# Architecture

```
host (CoreMIDI thread) ──SPSC ring──┐
host (UI / keyboard)   ──SPSC ring──┤
                                    ▼  audio thread
synth_midi / synth_set_param / synth_set_engine ──► event queue (time-stamped, applied per block)
synth_render(s, L, R, n) ── split into blocks of 32 ──► render_block():
    events → engine->render (voices summed into L/R) → engine-switch fade
    → fxbus: chorus (insert) → tape delay (send) → plate reverb (send, fed by dry + echoes)
    → master gain → saturation (2x oversampled, off by default) → DC blocker
    → look-ahead limiter (1.5 ms, off by default) → soft clipper
```

## Layers

| Layer | Role | Depends on |
|---|---|---|
| `dsp/core` | block size, memory macros, fast math, smoothing, FTZ | nothing |
| `dsp/osc`, `filter`, `env`, `mod`, `fx` | DSP building blocks, each `x_init / x_reset / x_set / x_process` in physical units | core |
| `dsp/voice` | `voice_alloc` (who gets the note), `vpoly` (stealing fade, sustain, voice lifetime), `drift` | core |
| `dsp/engines` | engines implementing `engine_ops_t` + `registry.c` | building blocks, voice |
| `dsp/fx/fxbus` | effect bus with its own parameter table | fx |
| `dsp/synth.c` | public API, event queue, dispatch, master — **no engine-specific code** | engines (via registry), fxbus |
| `host/` | macOS players/renderers, JSON patches and songs, catalog | dsp |

## Self-description (single source of truth)

Every parameter is a `param_desc_t` (`dsp/mod/params.h`): name, range, default, unit, curve,
labels of enumerated values, one-line help. Engines expose their table through `engine_ops_t`;
the master and effect parameters through `dsp/global_params.h`. Everything else is derived:

- `synth_set_param(s, id, 0..1)` maps through the descriptor curve;
- `host/catalog.c` resolves names for patches (engine parameters, then global ones);
- `synth_catalog` generates `docs/catalog.md` / `.json` for humans and AI agents;
- patch loading validates names, labels and ranges and lists valid choices in its errors.

## Engines

An engine owns per-voice DSP state and four `vpoly` callbacks (start, release, level, silence);
`vpoly` does allocation, click-free stealing (3 ms fade), sustain pedal and voice lifetime. Per block
the engine computes control values once (parameters smoothed at SR/32), then renders each active
voice, hands it to `voice_out` (steal fade, NaN guard, pan; `dsp/voice/voice.h`), and calls `vpoly_end_block`. See `docs/adding_an_engine.md`.

| Engine | Per voice | Notes |
|---|---|---|
| `sine` | sine + ADSR | reference, smallest example |
| `va` | 2 osc (sine/saw/pulse/tri/supersaw) + noise → SVF → ADSR; 3 detune LFOs, drift | global LFO 1 drives LFO-of-LFO |
| `modal` | up to 16 complex one-pole resonators; strike (pulse + noise) and bow (noise) | coefficients rebuilt only when parameters change |

## Memory

An instance (`synth_t`, in memory the caller provides or the static `g_synth`) holds 8 parts of ~40 KB each
(the size of the largest engine), the event queue and the effect control state: ~340 KB with `SYNTH_PARTS` 8,
too large for the DTCM of the boards (128 KB on the H745, 64 KB on the F746): a target lowers `SYNTH_PARTS`.
Next to it, the effect arena `g_fx_mem` (delay + reverb, 1.3 MB by default) and the engines' shared read-only
tables `g_engine_mem` (wavetables, 11.9 MB by default). Targets size the arena with `SYNTH_FX_MEM_FLOATS`,
`SYNTH_FX_MAX_DELAY_S`, `SYNTH_FX_REVERB_SIZE` and place it with `DSP_LARGE_DATA` (SDRAM / AXI SRAM);
a smaller arena shrinks the reverb then the maximum delay time, it never fails. Help strings can be
compiled out with `-DDSP_PARAM_TEXT=0`.

## Safety nets

- `tools/check_rules.py` (ctest `rules`): the code rules of CLAUDE.md, checked mechanically.
- `tools/golden.py` (ctest `golden`): SHA-256 of the audio of every patch and song (`tests/golden.json`).
  A refactoring must keep every hash (identical in Debug and RelWithDebInfo builds); an intended sound
  change is recorded with `--update` and is visible in the diff of `golden.json`.

## Shared engine memory

An engine may declare `shared_floats` / `shared_build` / `shared_attach` in its `engine_ops_t`: read-only
data (the wavetables of `wt`) built once by the first `synth_init` into `g_engine_mem` (synth.c), outside the
audio path, then attached to the state at each engine start. The arena size is `SYNTH_ENGINE_MEM_FLOATS`.

