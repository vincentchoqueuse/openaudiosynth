# target/h745 — STM32H745I-DISCO firmware

CubeMX project (`h745.ioc`, CMake, GCC), dual core: the M7 runs the audio (SAI2 + DMA to the WM8994 codec),
the M4 the touch screen (preset selector), the network and USB MIDI. The two cores talk through a mailbox in SRAM4
(`app/shared.h`).

```sh
just h745      # copies the ST drivers if needed (tools/stm32_drivers.sh), builds both cores
just flash     # flashes both cores through the ST-LINK (STM32CubeCLT)
```

- `Drivers/` is not versioned: `tools/stm32_drivers.sh` copies it from the STM32Cube repository installed by
  CubeMX (firmware version read from the `.ioc`).
- Generated code is only edited inside `USER CODE` blocks (`main.c` calls `app_init` and `app_loop`); our code
  goes to `app/`: `audio.c` (codec, SAI2, DMA, load measurement), `app.c` (synth, demo arpeggio, health
  counters), `app_cm7.cmake` (added to `CM7/CMakeLists.txt`), and the linker script `stm32h745_cm7_synth.ld`.
- Memory: the synth state (one part, ~74 KB) and the 16 KB stack in the DTCM, the effect memory (400 KB) and
  the DMA buffer in the AXI SRAM. The DSP and the audio interrupt are compiled at -O2 in every build type.
- Health, in the debugger (Live Expressions): `app_health` gives the DSP load (average and peak, in % of the
  time between two interrupts), the overruns (must stay 0), the DMA errors and the free stack.
- Presets: `app/presets_gen.c`, generated from `patches/*.json` by `host/board_presets` at every host build
  (`just build`); patches of `wt` are left out until the wavetables have the SDRAM. A preset is applied as the
  host does (synth_init, engine, values) in the M7's main loop while the audio interrupt fades out and plays
  silence, then fades back in.
- Overload guard: after 4 consecutive overruns (~10 ms) the M7 mutes the preset and reports it (`overload`
  in the mailbox, shown "too heavy" on the screen), so a preset too heavy never locks the board.
- The M4 (`app/m4/app_m4.c`) shows a grid of instruments (one tile per engine, in its color), then the presets
  of the chosen one with their descriptions (drag to scroll, "<" to go back). Its renderer (`app/m4/gfx.c`)
  draws into two RGB565 frames in the SDRAM swapped at the vertical blanking, fills with the DMA2D and blends
  anti-aliased text (DejaVu Sans rasterized by `tools/gen_font.py` into `fonts_gen.c`). A tap on the DSP meter
  opens the audio view (`app/m4/scope_view.c`): oscilloscope and spectrum of the output, which the M7 copies
  into a ring in SRAM4 (`SCOPE` in `shared.h`). It drives the display, the SDRAM and the touch panel through ST's BSP; the touch
  panel shares I2C4 with the codec, so it starts only once the M7 has released the bus (`i2c4_free`).
  LTDC, FMC and the other board peripherals are disabled in CubeMX: the BSP sets them up itself (CubeMX's
  generated LTDC init has the same name as the BSP's and would replace it).
- Touch panel (GT911 on this board): it occasionally stops answering on I2C (seen once in ~4 000 reads);
  the M4 then starts it again (`ts_resets` in `ui`), at the cost of the BSP's 1 s read timeout.
- Known workaround: CubeMX also initializes SAI2 (`MX_SAI2_Init`) and its `HAL_SAI_MspInit` would move the
  SAI clock off the audio PLL; `audio.c` marks the BSP's handle initialized and checks the clock source.
- Output level: `BOARD_VOLUME` in `app/board_config.h` (0..100; 90, about 0 dB at the codec, by default; 70 was
  -13 dB and sounded faint).
- MIDI in (`app/board_config.h`: `BOARD_MIDI_USB`, `BOARD_MIDI_NET`, both on by default; edit, then
  `just flash`): over Ethernet (docs/link.md), and/or from a class-compliant USB MIDI controller plugged into
  the **USB OTG FS** micro-USB connector (not the ST-LINK one) through an OTG adapter. The M4 is the USB host
  (`app/m4/usbh_midi.c`, on the HAL's HCD only; descriptors read by `midi/usbdesc.c`): it powers the
  connector, sets the device up, then polls its MIDI IN endpoints every frame in the USB interrupt and pushes
  each message to the M7's event queue, like the network. The screen shows the device's name; unplugging it
  stops the notes it held. No hub. PLL3 now serves both the display (9.6 MHz) and USB (48 MHz). Tested with a
  Novation Launch Control. The screen's second line shows the last CC or note received. A debugger reads
  `usbh_midi_cc_seen` on the M4 (each CC as `(channel << 8) | cc`, in the order the knobs first moved) to
  find what a controller sends. `BOARD_MACRO_CC` (CCs 13..20, the Launch Control's top knobs) sets the CCs
  that turn macros 1..8 on every preset, in place of the presets' own CCs (102..109). A preset whose macros do
  nothing gets default targets (`app/knobs.c`, `BOARD_KNOB_TARGETS`): six main parameters of its engine
  (cutoff, resonance, envelope, wavetable position, FM modulator levels...) on macros 1..6, the delay and
  reverb sends on 7 and 8, starting at the preset's value. The sends cover their whole range; the engine
  parameters move around the preset's value (0.35 either way, normalized), and only 0.12 towards a closed
  filter or a long attack, so that no knob can silence the sound.
  The board reads the knob CCs itself (the synth's macro CCs are switched off) with **soft takeover**: after a
  preset change, a knob drives its macro only once it reaches or crosses the macro's value, so turning a knob never
  makes the sound jump (an FM preset's operator levels dropping to 0, a cutoff closing); until then the controls
  view shows where the knob is and "turn up" or "turn down".
- Screen views of the preset playing, as tabs in the top bar: a tap on its name (or on the preset playing in
  the list) opens its **controls**, a tap on the DSP meter its **signal** (oscilloscope and spectrum); "<"
  goes back. The controls are the eight knobs: the parameter each one moves, its value in its unit, a bar,
  lit while it turns. The M7 writes them in the mailbox (`knob_*` in `app/shared.h`, filled by `app/knobs.c`).
- The MCU's unique id (MAC address, board id) is read by the M7 and handed to the M4 in the mailbox: the M4
  takes a bus fault reading `UID_BASE`.
- Keep the board's supply configuration (direct SMPS): a wrong setting makes the board unreachable by the
  debugger. With the SMPS, the M7 runs at 400 MHz at most.

## Stage safety

- **Silent at power-up**: the demo arpeggio is off; a tap on the title "synth" of the instrument screen turns it
  on or off ("demo on" / "demo off" under the title). A note from the network also stops it.
- **Watchdog** (IWDG1, 250 ms): refreshed by the main loop only while the audio interrupt keeps rendering (or when
  the audio never started). A hung core resets the board, which comes back silent. The watchdog is frozen while
  the debugger halts the core.
- **Faults**: the HardFault handler zeros the whole DMA buffer, so the circular DMA plays silence instead of
  repeating the last 2.7 ms, then waits for the watchdog.
- **Network**: notes received over Ethernet stop when the host goes quiet for 500 ms, the cable is pulled or
  events were lost (`docs/link.md`, fail-safe).
- **NaN**: a part whose output is not finite is reset with its current values, in every build (`dsp/synth.c`).
- **Preset changes**: the new engine and its parameters are applied in the main loop (one silent block rendered
  there) before the audio interrupt takes the synth back.
