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
  Double-click a label or slider to reset it; changed parameters are highlighted; the help text is the tooltip. Sliders are exact to set (`lib/slider.js`): Shift while dragging is ten times finer, the arrows step 1 % (Shift 0.1 %, Page Up / Down 10 %), a double-click on the value types it. The top bar walks the library (previous / next), shows a dot when the sound is edited, and A/B switches between the sound as loaded and the edit.
- **Signal chain** (always in view, above the editors): a display-only view of the engine's stages and the effect bus along the signal,
  modulators below with dashed links to what they act on (the active part routes too); a stage that is off is
  drawn dashed. Below it, the parameters are grouped by stage, and a stage that can be switched off has an on/off
  switch in its section header (Sound and Effects tabs). The chain comes from the stage descriptors
  (`engine_stage_t`, `dsp/engines/engine.h`): `va`, `drone` and the effect bus among others; other engines group
  their parameters by name.
- **Macros**, always in view under the player: the sound's 8 live controls with their names (the patch's
  `macros`), a knob each (drag up/down, wheel, arrows; double-click returns to the patch's value) and, under it,
  the parameters it moves with a bar showing where each one is in its range. A click on a name opens the editor:
  rename, set the MIDI CC that turns it, add or remove targets (engine parameters, the master or an effect), set
  each target's value at 0 and at 1 in its own units. A knob follows its CC from a MIDI controller. *Save to
  library* and *Export* write them back as the readable `macros` section.
- **Undo / redo** (Cmd+Z, Shift+Cmd+Z, the arrows in the transport): every edit of the sound (parameters, engine,
  preset loaded) and of the clip; a knob turned or a note dragged is one step, a key or a button one step
  (`lib/history.svelte.js`). Text fields and the code editor keep their own undo.
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
- **Layout**: the sound library on the left (it also picks the instrument); one top bar, read from the left: the Menu (Save, Export sound, Export
  song, Revert, Instrument defaults, Share (QR code), Undo / Redo, Shortcuts, Report a bug), a rule, the three views, pills of one width (**Sequence**, **Synth**, **Code**), a rule
  and Preset with the sound's name; on the right the
  CPU load (time spent rendering over the audio's time, measured in the worklet) and the output level as meters,
  the volume and Sequencer ▶, each set apart by a rule (play / stop). **Sequence**: Live, wave or spectrum, then the sequencer
  (tempo, loop, position, piano roll). **Synth**: Structure always on top, its blocks open their settings, then
  smaller pills of one width with an icon, each with its own tabs under it, even a single one: Macros (Names;
  Assignments, 16 fixed rows), Engine (the instrument, in the order the signal flows, the sampler's pads chosen like the modulators; Effects: the output filter, chorus, delay, reverb, master), Modulation
  (Modulators; Assignments, of the part or of each note), Info (Description). **Code**: the instrument (the patch file) or the sequencer (the clip), in an editor as tall as
  the page. Each view starts with its title and its icon. On a phone the page is for playing only: the top bar (menu, sound, volume, tempo, play), the live display,
  the macros and the keyboard, with the library in its drawer; no Synth or Code view, no piano roll (the clip plays
  from the top bar), no macro CC, no shortcuts sheet. Along the bottom, the 8 macros to play in a Macro control card (2 rows of 4) above the
  keyboard, which lights the notes the clip plays.
- **Address**: `?sound=NAME` opens a preset, `?q=TEXT` fills the library search, `?view=sequence|synth|code` opens a
  view (e.g. `/?sound=drone_glacial&view=synth`); the address bar follows the sound playing, and Share's QR code
  opens on it (`lib/urlparams.js`).
- **Interface rules** (every part of the page follows them):
  - one component per kind of control, used everywhere: `Pills` (the views, size lg; the Synth view's pills, md,
    with an icon; drawn choices of a waveform or a shape, icon), `Tabs` (an editor's own views, small and
    underlined), `Seg` (a mode or a value among a few words: one shot / loop held / loop free, Hz / beats, Part /
    Each note, the sampler's pads), `Switch` (every on / off), `Icon` (every icon, named in `lib/icons.js`), `Param`
    (a parameter row, reading and writing the session unless told otherwise);
  - three levels of navigation, each its own look: the views as pills in the top bar (Sequence, Synth, Code); then
    the Synth view's pills (Macros, Engine, Modulation, Info); then each pill's tabs, even a single one (Names /
    Assignments; the instrument / Effects; Modulators / Assignments; Description); no folds;
  - one toolbar (`.toolbar`): settings on the left as a label then its control, actions on the right as plain
    text buttons, one height; a help line, when there is one, under the content in small muted text;
  - a choice of waveforms or shapes is a row of drawn buttons (`lib/waves.js`: oscillators, LFOs, envelope starting
    shapes); the envelope editor shows a playhead while the envelope runs (`synth_probe.h`, `w_mseg`);
  - the cards fill a grid of equal columns that wraps (`.groups`); what is not in use (an effect off, an empty route or macro row) waits behind an Add button (`.addrow`);
  - Effects is a rack: the five effects of the bus show their place and two arrows that move them along the signal (`session.rack`, `setRack`, the parameters `fx_slot1..5`); the signal path of Summary follows that order;
  - parameters are cards of rows (label without the card's own words, control, value in its unit), one card per
    stage of the engine (every engine with stages gives each parameter to one, tested); a dot after a label: moved
    since the sound was loaded, double-click puts it back; on / off is always a switch; a stage switched off folds;
  - text on the accent color is dark or white by the accent's brightness; everything is in English.
- **Top bar**: a click on the sound's name opens its description; **?** (or the ? key) the sheet of shortcuts.
- **Settings** (the gear at the bottom of the sidebar): the part (MIDI channel, keys, transposition, MPE), the
  tempo and the tuning, apart from the sound's design.
- **Theme**: the sun / moon icon at the bottom of the sidebar cycles auto (as the system), light and dark; kept
  in the browser.
- **Transport** (on top, always in view): play / stop the clip (Space), tempo, loop on / off (off: it plays once),
  position (bar . beat . sixteenth), and the listening volume (after the synth and its meter, kept in the browser,
  untouched by sounds and patches; double-click: 0 dB).
- **Sequence section**: a MIDI clip played with the current sound (each preset brings its own: its `clip`, else its
  audition phrase as notes; the audition bar is gone, the transport plays the clip), on a piano roll with a velocity lane below. Draw
  tool: click to draw a note and drag to stretch it; Select tool (or Cmd-drag): a rectangle selects. A click
  selects a note (Shift adds), dragging moves the selected notes (time and pitch), their right edge stretches
  them, Alt-drag or the velocity lane sets their velocity, double-click deletes. Once the roll has the focus:
  Delete, Cmd+A, Cmd+C / X / V, Cmd+D (duplicate after the selection), arrows (a grid step, a semitone, Shift an
  octave), Cmd+U (quantize); the computer keyboard keeps its letters for playing. Everything snaps to the grid
  (Shift: finer). The clip's length
  is its loop (the brace on the ruler, drag its end, or the Length menu); the key colors the rows. It plays through
  the worklet's sample-accurate player (`w_notes`), edits take over in place, and it keeps playing while the sound
  is edited and through patch loads. Clips are kept in this browser (New, Save, the saved list; the step grids of
  before are converted) and **Export song** writes a self-contained song (`songs/` format, the sound inline).
- **Code tab**: the documents the page is made of, by file name in an explorer (`NAME.json`, the sound's patch;
  `NAME.clip.json`, its clip), opened
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
  the state, and the components read them directly (props only for what differs between two uses: a modulator's
  prefix, a matrix's kind, an editor's tab). The session decodes the macros' 32 target slots once (`macroMaps`).
  Styles: `src/app.css`, by part (tokens, shared controls, layout, editors); a component keeps only what is its own.
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
| `ui/src/App.svelte` | the page: `Sidebar` + `Library`, `TopBar`, the views `ClipTab` (Scope, SequenceTab + PianoRoll), `SynthTab` (ChainView, MacroMatrix, ParamGroups / KitPanel, ModPanel: MsegEditor, LfoView, ModMatrix; InfoPanel), `CodeView`; `MacroStrip` and `Keyboard` at the bottom; the dialogs |
| `ui/src/lib/*.svelte.js`, `*.js` | state (`session`, `sequence`, `history`, `theme`), data (`engines`, `icons`, `waves`, `mseg`, `kit`, `naming`, `seqExport`), browser storage (`prefs`) |

- The main thread compiles the module and hands it to the worklet. Every API call goes through the worklet's
  port, so the API stays single-threaded as `synth.h` requires.
- A patch loads exactly as in `synth_render` (fresh `synth_init`, then engine and parameters), so it sounds
  the same as the WAV renders.
- The wasm output is **bit-identical** to the host (checked on `acid_bass`, `fm6_bass`, `wt_digital_pad`,
  `kit_analog`, `modal_alien_bell`), thanks to `-ffp-contract=off`. WebAssembly has no flush-to-zero, so only
  denormals (below −300 dB) could ever differ.
- Measured costs: first init (wavetables) 136 ms in the worklet constructor; loading a patch 0.6 ms (less
  than one 2.7 ms quantum); rendering about 200× faster than real time under Node.
