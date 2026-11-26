# Benchmarks

`./build-host/tests/synth_tests --bench` (RelWithDebInfo, -O2), 48 kHz, block 32, all voices held,
20 s rendered. Host numbers are for comparison between versions only; the STM32 budget is measured
with `DWT->CYCCNT` on the board (M7).

| Date | Machine | Engine | Voices | ns / voice / sample | Load (real time) |
|---|---|---|---|---|---|
| 2026-11-17 | Apple Silicon (host) | va (2 osc + noise, SVF, 2 ADSR, LFO 1 + per-voice LFO 2) | 8 | 11.2 | 0.43 % |
| 2026-11-17 | Apple Silicon (host) | va, LFOs 2-4 (detune 0, skipped) | 8 | 11.5 | 0.44 % |
| 2026-11-17 | Apple Silicon (host) | va, 2 x supersaw (14 saws/voice, motion on) | 8 | 36.2 | 1.39 % |
| 2026-11-18 | Apple Silicon (host) | modal, 16 modes, strike + bow | 8 | 21.9 | 0.84 % |
| 2026-11-18 | Apple Silicon (host) | modal, 32 modes (piano worst case), strike + bow | 12 | 42.1 | 2.43 % |
| 2026-11-18 | Apple Silicon (host) | drums (12 instruments ringing) | 12 | 2.4 | 0.14 % |
| 2026-11-18 | Apple Silicon (host) | acid (ladder 2x oversampled) | 1 | 47.6 | 0.23 % |
| 2026-11-18 | Apple Silicon (host) | mono (3 osc, ladder 2x oversampled) | 1 | 53.1 | 0.25 % |
| 2026-11-18 | Apple Silicon (host) | poly (sync/poly-mod osc, ladder 1x) | 8 | 30.5 | 1.17 % |
| 2026-11-18 | Apple Silicon (host) | va + full effect bus (chorus, delay, reverb) | 8 | 16.7 | 0.64 % |
| 2026-11-18 | Apple Silicon (host) | fm6 (6 operators, all levels on) | 16 | 21.7 | 1.66 % |
| 2026-11-18 | Apple Silicon (host) | wt (1 osc, unison 1, lp12) | 8 | 15.1 | 0.58 % |
| 2026-11-18 | Apple Silicon (host) | wt worst case (2 osc x unison 8, sub, ladder 2x) | 8 | 90.6 | 3.48 % |
| 2026-11-18 | Apple Silicon (host) | coupled (16 coupled partials per voice) | 8 | 53.3 | 2.05 % |
| 2026-11-19 | Apple Silicon (host) | mono (Model D panel), ladder 1x | 1 | 27.8 | 0.13 % |
| 2026-11-19 | Apple Silicon (host) | mono, ladder 2x (default) | 1 | 42.9 | 0.21 % |
| 2026-11-19 | Apple Silicon (host) | mono, ladder 4x | 1 | 71.2 | 0.34 % |
| 2026-11-19 | Apple Silicon (host) | poly, ladder 1x (default) / 2x / 4x | 8 | 24.9 / 39.7 / 68.1 | 0.95 / 1.52 / 2.62 % |
| 2026-11-19 | Apple Silicon (host) | wt worst case, ladder 2x (default) / 4x | 8 | 72.2 / 92.5 | 2.77 / 3.55 % |
| 2026-11-21 | Apple Silicon (host) | drone (12 partials + 2 beating twins, saw body, air, sparkles) | 6 | 38.4 | 1.10 % |
| 2026-11-21 | Apple Silicon (host) | drone, 8 voices | 8 | 39.6 | 1.52 % |
| 2026-11-22 | Apple Silicon (host) | micro worst case (held, two sines, sub, drive, free gate, pitch envelope, noise click) | 8 | 15.0 | 0.58 % |
| 2026-11-23 | Apple Silicon (host) | va + whole chain: phaser (4 stages), flanger, chorus, delay, reverb, master saturation (soft) and limiter (va+fx: 13.1) | 8 | 28.4 | 1.09 % |
| 2026-11-24 | Apple Silicon (host) | havoc default (one generator sounding; up to four, plus two processors) | 1 | 21 | 0.10 % |
| 2026-11-26 | Apple Silicon (host) | modal, 12 voices: modes two at a time (was 28.9) | 12 | 18.0 | 1.03 % |
| 2026-11-26 | Apple Silicon (host) | drone, 8 voices: sounding sines in pairs (was 39.2) | 8 | 32.8 | 1.26 % |
| 2026-11-26 | Apple Silicon (host) | va + whole chain: phaser channels in one loop (was 27.9) | 8 | 20.8 | 0.80 % |
| 2026-11-26 | Apple Silicon (host) | poly, ladder 1x: block state in locals (was 24.5) | 8 | 22.2 | 0.85 % |
| 2026-11-26 | Apple Silicon (host) | mono, ladder 1x: block state in locals (was 29.1) | 1 | 26.5 | 0.13 % |
| 2026-11-26 | Apple Silicon (host) | va, unison 7 per oscillator, width 0.5 (stereo: filters doubled), motion on (the mono supersaw of 7 was 27.0) | 8 | 38.2 | 1.47 % |
| 2026-11-26 | Apple Silicon (host) | va, unison 16 per oscillator, stereo, motion on | 8 | 70.4 | 2.70 % |

Cross build (arm-none-eabi-gcc, cortex-m7, -O2, `libsynth_dsp.a`): text ~77 KB (help strings
included; `-DDSP_PARAM_TEXT=0` removes them), state ~340 KB with 8 parts (~40 KB per part: lower `SYNTH_PARTS` on a board), plus the effect arena (default 1.3 MB,
host-sized: a board sets `SYNTH_FX_MEM_FLOATS` and puts it in SDRAM with `DSP_LARGE_DATA`). Plate reverb memory at 48 kHz, size 1: ~37 k floats (~150 KB) provided by
the caller: AXI SRAM on the H745, not DTCM.

Wavetable memory: 9 factory tables x 32 frames x 10250 floats = 11.8 MB (`SYNTH_ENGINE_MEM_FLOATS`), built
once at the first `synth_init` (~0.1 s on the host). A board defines `WT_FRAMES 16` (5.9 MB) and/or a smaller
`SYNTH_ENGINE_MEM_FLOATS` (the tables that do not fit fall back to `basic`) and places the arena in SDRAM
with `DSP_LARGE_DATA`.

2026-11-18, after `-ffp-contract=off` (no implicit FMA, bit-identical results on every build): va 15.6, modal (12 v)
48.2, poly 38.9, fm6 26.2, wt 20.8, coupled 64.3 ns/voice/sample (+10..40 %).
selfosc (4x oversampled Van der Pol per voice): 124 ns/voice/sample (8 voices, 4.8 %).


Oversampling of the nonlinear filters (`oversample` = 1x / 2x / 4x on mono, acid, poly, wt, va): aliasing of a
driven sine through the ladder at 5 kHz is -32 / -50 / -48 dB below the note (what remains at 2x and 4x is the
5th harmonic folding through the half-band transition band around 24 kHz), at 8 kHz -75 / -84 / -103 dB; the
scream filter -20 / -30 / -30 dB at 5 kHz, -46 / -48 / -56 dB at 8 kHz (`tests/test_filter.c`). On a full patch (one
sawtooth at E6 into the driven resonant ladder) the energy between the harmonics is -27.9 / -33.5 / -34.3 dB:
above 2x the sawtooth's own residual aliasing dominates. 4x costs one more 576-byte half-band state per
filter: an engine state is now up to 44 KB (`wt`), an instance ~400 KB with 8 parts.

## STM32H745I-DISCO (M7 at 400 MHz, first measurement, 2026-11-19)

Measured on the board with `DWT->CYCCNT` around the render of each 128-frame interrupt (`app_health` in
`target/h745/app/app.c`), Debug firmware (the DSP and the interrupt code are -O2 in every build type).

| Case | Average load | Peak load | Overruns |
|---|---|---|---|
| `mono` defaults (2x ladder), one note sounding, plate reverb at 0.25 | 21.5 % | 24.6 % | 0 |

21.5 % of 8 333 cycles per sample is about 1 800 cycles per sample for one voice and the reverb: the
profiling (voice against effects, ITCM placement) is the next step.

### On-board benchmark of every preset (`tools/h745_bench.py`)

The firmware renders the demo arpeggio for 1.5 s on every preset with the audio interrupt stopped and records
the load (average and worst 128-frame buffer, % of the M7's real-time budget). Short windows: the numbers
compare firmwares, live play with long tails loads more. Mean over the 75 presets:

| Firmware | Mean | Presets above 100 % at their worst buffer |
|---|---|---|
| DSP at -O2 (first version) | 25.7 % | 4 |
| hot DSP code in the ITCM (`DSP_FAST_CODE`) | 26.0 % | 7 (the I-cache already holds it; calls back to the flash cost veneers): not kept |
| DSP at -O3 | **24.2 %** | 3 |

Per engine at -O3, and what each part costs (mean of the engine's presets):

| Engine | Load | Effects off | Filter 1x |
|---|---|---|---|
| coupled | 59.7 % | 52.6 % | = |
| selfosc | 51.8 % | 52.1 % (-O2) | = |
| poly | 30.8 % | 26.8 % (-O2) | = |
| modal | 26.0 % | 21.9 % (-O2) | = |
| fm6 | 25.9 % | 19.5 % (-O2) | = |
| mono | 16.3 % | 12.9 % (-O2) | -5 points (-O2) |
| va | 12.8 % | 11.4 % (-O2) | = (its default filter is the SVF) |

The plate reverb costs about 7 points (~575 cycles per sample) and the tape delay about 5 (`mono_d_lead`:
23.6 % with both, 16.7 % without the reverb, 18.8 % without the delay, 11.4 % without any effect). -O3 does not
change the sound (no FP contraction, no fast-math); hoisting the reverb's output taps out of the loop gave nothing
measurable (the compiler already did it) but stays for clarity.

### Profiled optimizations (same sound: every golden hash unchanged)

`tools/h745_bench.py --preset NAME --profile` samples the PC with TIM7 during the benchmark and prints the time
per function and per source line. What it found, and what changed:

| Hot spot | Fix | Effect |
|---|---|---|
| newlib-nano's byte-wise `memset`/`memcpy`, called by GCC for the 32-sample zeroing loops (4.5 % of `mono_d_lead`) | `-fno-tree-loop-distribute-patterns` for the DSP on the board | `mono_d_lead` 23.9 -> 22.8 % |
| `modal`: a per-note pitch LFO rebuilt every coefficient (gains, decays, frequencies) of 32 modes at each block | `retune_coeffs`: a pitch change recomputes only the frequencies, same expressions | `piano_cold` 72.2 -> 35.5 % |
| `modal` tension: two passes over the modes per sample | the next sample's tension summed during the rotation pass | `piano_tension` 69 -> 67 % |

Mean of the 75 presets: 25.7 % (first firmware) -> 24.2 % (-O3) -> **23.0 %**. Per engine (first -> now): modal
-18 %, acid -23 %, mono -17 %, selfosc -12 %, poly -10 %, drums -10 %, va -8 %, fm6 -6 %, coupled -3 %. Two presets
still exceed the budget at their worst buffer (`coupled_mosaic`, `piano_tension`): their cost is the algorithm
itself (one sine per partial per sample; 32 coupled modes per sample), not the code around it.

Then (2026-11-20), the default knob targets (`target/h745/app/knobs.c`) cost about half a point (mean 23.0 ->
23.5 %), and two loops were rewritten to work on **two partials or modes per pass**: the Cortex-M7 waits on each
floating-point result, and a single partial's (or mode's) chain of dependent operations left the FPU idle; two
independent chains interleave. Same operations, sums in the same order: every golden hash unchanged.

| Hot spot | Fix | Effect (average / worst buffer) |
|---|---|---|
| `coupled`: one sine per partial per sample (80 % of the preset) | two partials per pass, `fm_sin_pos` (no negative branch: the phase is never negative) | `coupled_mosaic` 72.3 / 103.8 -> 55.2 / 78.1 %, every `coupled` preset -17 points |
| `modal` tension: the rotation of each mode per sample | two modes per pass | `piano_tension` 68.2 / 106.0 -> 53.1 / 83.1 %, `gong_tension` -7 points |
| (tried) four partials per pass | 55.3 -> 54.0 %: not worth the code | not kept |

Mean of the 78 presets: **22.3 %**; none above 90 % at its worst buffer. Above the 60 % target on average remain
the three `sine_duel` presets (`selfosc`, 61 %, 87 % at worst): a 4x oversampled differential equation, one step
after the other, and half-band filters that sum their taps in a chain; going faster there means reordering
sums, so a (tiny) change of sound.

