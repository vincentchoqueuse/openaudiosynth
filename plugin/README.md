# plugin — CLAP plugin of the engine

A thin host layer, like `host/synth_play.c` (CoreAudio) and the web worklet: the same `dsp/synth.h` API, the same
patch files. Built with the host build (`cmake --build build-host`), it gives `build-host/plugin/OASynth.clap`.

```sh
cp -R build-host/plugin/OASynth.clap ~/Library/Audio/Plug-Ins/CLAP/     # install (macOS)
ctest --test-dir build-host -R clap                                   # test host: loads the .clap and checks it
```

- **One file, one plugin for everything and one per engine**: `OASynth` plays any engine (it has an `engine` parameter), and there is one plugin per
  engine (`OASynth va`, `OASynth fm6`, `OASynth drums`...) that plays that engine only and lists only its parameters. Same code, same parameter ids, same state format; a single-engine plugin
  refuses the state of another engine. Ids: `synth.engine` (all engines), `synth.<engine>`.
- **Notes and MIDI** go to `synth_midi` with their sample offset; a **parameter change** splits the block where it
  falls. The plugin renders bit for bit like the engine called directly, and like `synth_render` for a patch
  (`test_clap_host.c` checks it).
- **Parameters** (stable ids, so automation survives an engine change): `engine`; every parameter of every engine
  (`engine * 1000 + index`, module = the engine's name), each engine keeping its own values; the global ones
  (master, effects, tempo) and the 8 macros (`100000 + synth id`). Modulation shapes, routes and parts are not
  exposed but are kept in the state.
- **State** = a patch file (`docs/patch_format.md`): what the DAW saves in its project is readable, and a patch
  from `patches/` loads as a state. A patch holds one engine: the values of the other engines are not saved (they
  come back at their defaults). A state loaded while playing re-initializes the engine at the start of the next
  block (a one-off load spike, like any preset change).
- **Sample kits** (`OASynth sampler`, or `OASynth` on the sampler engine): the `kit` parameter chooses one: *none*,
  then the kits found when the plugin is loaded. The three demo kits (recorded from the drums engine) are in the
  bundle (`Contents/Resources/kits/*.oidk`); put your own in `~/Library/Application Support/synth/kits/`, one
  directory of `NN_name.wav` files per kit (NN: the MIDI note) or one `.oidk` file, named with `a-z`, `0-9`
  and `_`, then restart the host (the list is read once). A kit is read from the disk on the main thread, never
  on the audio thread; the state names it (`"kit": "synth_analog"`), and a project opened where that kit is
  missing plays the sampler silent.
- **Several instances**: each plugin instance owns its engine instance (`synth_create`), so several run side by
  side (the test host checks two at once).
- **Hosts**: CLAP is loaded natively by Bitwig, Reaper, FL Studio, Studio One...; for VST3 / AU hosts (Ableton
  Live, Logic) a wrapper such as `clap-wrapper` can build them from this plugin (to do; check the VST3 SDK license).
- **Interface**: none yet (the DAW shows its generic parameter list); the plan is to show the web UI
  (`web/ui`) in a web view, the worklet's messages going to the plugin instead.

SDK: `third_party/clap` (CLAP 1.2.10 headers, MIT).
