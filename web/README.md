# web — browser prototype (WebAssembly + Web Audio)

The `dsp/` engine compiled as is to WebAssembly and played in an **AudioWorklet** at 48 kHz, with a Svelte
interface generated from the parameter descriptor tables. For prototyping sounds and sharing them with no install.

```sh
brew install emscripten && just setup      # once
just web                                    # builds synth.wasm, then http://localhost:5173 (Chrome or Edge for MIDI)
just web-dist                               # static build in web/ui/dist (wasm and patches included)
node web/test_wasm.mjs                      # smoke test: every patch loads and renders under Node (in `just check`)
```

## Features

- Sound library as on the board: instruments first (one per engine, with its number of presets), the presets
  of the open one below; the search box lists matching sounds of every instrument. Each instrument has its
  color (the same as on the board's screen, `lib/engines.js`) and the whole interface takes the color of the
  one playing. Space bar or ▶ plays the patch's audition phrase, or a phrase you type with the same syntax
  (`C3 E3! _ G3~ C3+E3 -`), optionally looped.
- The current sound: name, description, how many parameters were moved since it was loaded, **Revert**.
- Output: waveform (zero-crossing trigger, automatic gain), log-frequency spectrum with falling peaks, level
  meter with peak hold.
- **Save to library** (development server only): writes the current sound into `patches/` (name checked, no
  silent overwrite), from which the board's presets are generated: `just build`, then `just flash`.
- Every engine parameter, grouped by name prefix (`osc1_` → *Osc 1*); tabs for effects, part and modulation.
  Double-click a label or slider to reset it; changed parameters are highlighted; the help text is the tooltip.
- **Layout**, from top to bottom: the sound's header and player, the macros, the output (wave, spectrum, level),
  the signal chain, then the editors in tabs (Sound, Effects, Part, Modulation, Sequence, JSON); the transport on top.
- **Signal chain** (always in view, above the editors): a display-only view of the engine's stages and the effect bus along the signal,
  modulators below with dashed links to what they act on (the active part routes too); a stage that is off is
  drawn dashed. Below it, the parameters are grouped by stage, and a stage that can be switched off has an on/off
  switch in its section header (Sound and Effects tabs). The chain comes from the stage descriptors
  (`engine_stage_t`, `dsp/engines/engine.h`): `va`, `selfosc` and the effect bus so far; other engines group
  their parameters by name.
- **Macros**, always in view under the player: the sound's 8 live controls with their names (the patch's
  `macros`), a knob each (drag up/down, wheel, arrows; double-click returns to the patch's value) and, under it,
  the parameters it moves with a bar showing where each one is in its range. A click on a name opens the editor:
  rename, set the MIDI CC that turns it, add or remove targets (engine parameters, the master or an effect), set
  each target's value at 0 and at 1 in its own units. A knob follows its CC from a MIDI controller. *Save to
  library* and *Export* write them back as the readable `macros` section.
- **JSON tab**: the current sound as its patch file, to read or edit by hand in a code editor (CodeMirror 6:
  line numbers, the cursor's line highlighted, JSON colors, matching brackets, folding, search, syntax errors
  underlined where they are; loaded with the tab, outside the main bundle). The text is checked as JSON while typing; *Apply* (Cmd/Ctrl+Enter) loads it through the engine's own
  patch reader, which says what is wrong (an unknown parameter, a value out of range, a target that cannot move
  live). *From the sound* rewrites the text from what plays.
- **Kit** (the `sampler` engine's Sound tab): its 16 pads (click one to play and select it), the selected pad's
  waveform with its start and end points dragged with the pointer and the region played lit, its settings and
  the kit's. Drop audio files on the panel, or use *Load samples…*, to make a new kit (the browser decodes WAV,
  AIFF, FLAC or MP3 and resamples to 48 kHz; `36_kick.wav` keeps its note, the others follow from C2). A patch
  that names a kit loads it from `kits/NAME.oidk` (the demo kits, built by `just build`). A dropped kit cannot
  be saved with the patch yet: the patch keeps the name of a built kit only.
- **Modulation tab**: a graphical editor for each multi-segment envelope (part MSEG 1 and 2, per-note MSEG):
  drag points (Shift snaps), drag a segment's middle handle to bend its curve (the same curve as `dsp/env/mseg.c`),
  double-click to add or remove a point, click a point to make it the sustain point or a loop boundary, loop mode
  (one shot, loop held, loop free), seconds or beats, starting shapes. Under it, the routes that envelope drives;
  then every route as a readable row (source → parameter by name, amount shown in the destination's units, via).
- **Transport** (on top, always in view): play / stop the clip (Space), tempo, loop on / off (off: it plays once),
  position (bar . beat . sixteenth).
- **Sequence tab**: a MIDI clip played with the current sound, on a piano roll: click to draw a note and drag to
  stretch it, drag a note to move it (time and pitch), its right edge to change its length, Alt-drag for its
  velocity, double-click or right-click to delete; everything snaps to the grid (Shift: finer). The clip's length
  is its loop (the brace on the ruler, drag its end, or the Length menu); the key colors the rows. It plays through
  the worklet's sample-accurate player (`w_notes`), edits take over in place, and it keeps playing while the sound
  is edited and through patch loads. Clips are kept in this browser (New, Save, the saved list; the step grids of
  before are converted) and **Export song** writes a self-contained song (`songs/` format, the sound inline).
- **JSON tab**: the documents the page is made of, in an explorer (the sound as its patch file, the clip), opened
  in a code editor and applied back (Cmd/Ctrl+Enter). The clip is plain MIDI, one note per line:

  ```json
  { "name": "riff", "bpm": 96, "length": 8, "loop": true, "grid": "1/16", "key": "E minor",
    "notes": [
      { "pitch": 64, "start": 0, "length": 1.5, "velocity": 100 },
      { "pitch": 59, "start": 2, "length": 0.5, "velocity": 127 }
    ] }
  ```

  pitch: MIDI (60 = C4); start and length in beats; velocity 1..127 (`docText` / `fromDoc` in `lib/seqExport.js`;
  a wrong document says what is wrong and where). The same note fields as OAHub's clips.
- **Code**: `lib/session.svelte.js` (the sound and its actions) and `lib/sequence.svelte.js` (the sequence) hold
  the state; the components read them (Transport, SoundHeader, AuditionBar, MacroStrip, Scope, ChainView,
  EditorTabs, ParamGroups, SequenceTab, PianoRoll, JsonWorkspace, Keyboard, Sidebar).
- **Export** downloads (and copies) the current sound as a patch file in the format of
  `docs/patch_format.md`, ready for `patches/` and `synth_render`.
- Web MIDI input (MiniLab 3 included), on-screen keyboard, computer keyboard (physical key positions, so
  AZERTY and QWERTY both work; arrow keys shift octaves).

## Architecture

| File | Role |
|---|---|
| `wasm/synth_wasm.c` | C exports: render, MIDI, parameters, catalog as JSON, patches (`host/patch.c`), audition and sequencer player |
| `wasm/CMakeLists.txt` | standalone wasm module (`STANDALONE_WASM`), no emscripten JS runtime |
| `ui/public/synth-worklet.js` | AudioWorkletProcessor: instantiates the module, handles port messages, renders each quantum |
| `ui/src/lib/synth.js` | AudioContext, loading, normalized/physical conversions, Web MIDI |
| `ui/vite.config.js` | serves `synth.wasm` from `build-wasm` and the sounds from `patches/` (index generated), no copies; `POST /api/patches` saves a sound (dev only) |
| `ui/src/App.svelte` | interface; `lib/Library.svelte` (sounds), `lib/Scope.svelte` (output), `lib/engines.js` (order, colors), `lib/MsegEditor.svelte` + `lib/mseg.js` (envelopes), `lib/Routes.svelte`, `lib/ModPanel.svelte`, `lib/Sequencer.svelte` |

- The main thread compiles the module and hands it to the worklet. Every API call goes through the worklet's
  port, so the API stays single-threaded as `synth.h` requires.
- A patch loads exactly as in `synth_render` (fresh `synth_init`, then engine and parameters), so it sounds
  the same as the WAV renders.
- The wasm output is **bit-identical** to the host (checked on `acid_bass`, `fm6_bass`, `wt_digital_pad`,
  `kit_analog`, `modal_alien_bell`), thanks to `-ffp-contract=off`. WebAssembly has no flush-to-zero, so only
  denormals (below −300 dB) could ever differ.
- Measured costs: first init (wavetables) 136 ms in the worklet constructor; loading a patch 0.6 ms (less
  than one 2.7 ms quantum); rendering about 200× faster than real time under Node.
