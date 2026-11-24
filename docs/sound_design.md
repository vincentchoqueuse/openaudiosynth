# Sound design guide

How the parameters translate into sound, and starting recipes. Ranges and units: `docs/catalog.md`.
Measurements quoted here come from `tools/analyze.py` on a single held C3 (the "probe").

## Signal flow

```
osc1 (sine/saw/pulse/tri) ─┐
osc2 ─────────────────────┼─> SVF (lp/bp/hp/notch) ─> amp ADSR ─> pan ─> mix ─> [song reverb]
noise ─────────────────────┘     ^ cutoff = base + fenv + keytrack + velocity + LFO 1 + drift
per voice: LFO 2 -> osc 2 detune, LFO 3 -> osc 1 detune, LFO 4 -> whole-voice detune (desynchronized)
LFO 1 (global, slow) ──> pitch, cutoff, pulse width, and rate/depth of LFOs 2-4 ("LFO of LFO")
```

## What each control does to the ear

| Want | Turn |
|---|---|
| brighter / darker | `cutoff` (main control), `keytrack` (keeps high notes bright), `fenv_amount` |
| "pew", pluck, acid squelch | `fenv_amount` 3–6 oct, `fenv_decay` 0.1–0.3 s, `fenv_sustain` 0, `resonance` 5–12 |
| softer onset | `amp_attack` 20–80 ms; swell > 0.5 s |
| percussive | `amp_sustain` 0, `amp_decay` 0.2–1 s |
| fat / wide | two saws, `osc2_detune` 5–12 cents, `pan_spread` 0.5–1, `drift` 4–8 |
| **moving, breathing detune (lush pads)** | `lfo2_detune` 8–20 cents, `lfo2_rate` 0.1–0.4 Hz, `lfo2_spread` 1 |
| **supersaw swarm** | `osc1_wave` `supersaw` (and `osc2_wave` too, maybe `osc2_oct` -1): `supersaw_detune` 15–40, `supersaw_mix` 0.6–0.9, `supersaw_motion` 4–10 cents at 0.1–0.3 Hz |
| denser, never-repeating detune | add `lfo3_detune` 5–12 (osc 1) and `lfo4_detune` 3–6 (whole voice); keep the default irrational rate ratios |
| movement that itself evolves | LFO 1 slow (`lfo_rate` 0.02–0.08 Hz) + `lfo2_rate_mod` 1–2 oct + `lfo2_depth_mod` 0.5–0.8 (acts on LFOs 2–4, in opposite directions) |
| slow filter breathing | `lfo_cutoff` 0.3–1 oct with slow `lfo_rate` |
| vibrato | `lfo_pitch` 0.1–0.3 st at 5–6 Hz, or `wheel_vibrato` + mod wheel |
| tape-like instability | `drift` 6–10 cents |
| hollow / reedy | `pulse` waves; `osc*_pw` away from 0.5; `lfo_pwm` 0.2–0.4 for ensemble motion |
| sub weight | `osc2_oct` -1 with `sine`/`pulse`, keep `cutoff` low |
| living sub | two sines, `osc2_detune` 3–5 cents, `osc2_level` ~0.5 (not 1: equal levels cancel at each beat), small `lfo2_detune` |
| breath, air, hiss | `noise_level` with `filter_mode` `bp` |

Pitfalls: high `resonance` with high `fenv_amount` gets loud — check `peak_dbfs`. Very short
`amp_release` (< 5 ms) on low notes can click. Voices sit at -18 dBFS each; chords of 8 notes still
leave headroom.

## Recipes (starting points, see `patches/`)

- `acid_bass` — saw + sub pulse, `cutoff` ~200, `resonance` 8, `fenv_amount` 4.5, `fenv_decay` 0.18, accents.
- `sub_bass` — tri + sine -1 oct, closed filter, no resonance.
- `pluck` — saw + tri +1 oct + a bit of noise, `fenv_decay` 0.2, `amp_sustain` 0.
- `brass` — two saws, `fenv_attack` 40–80 ms gives the "blat".
- `pwm_strings` — two pulses + `lfo_pwm`, soft attack.
- `warm_pad` — two detuned saws, slow attack, slowly opening filter.
- `ambient_saw_pad` — two saws, **three desynchronized per-voice detune LFOs + LFO-of-LFO**, slow swell, big reverb
  in a song. Few chords: the movement carries the music.
- `ambient_supersaw_pad` — 2 x 7-saw supersaw (one octave apart), per-saw motion + LFOs 2–4.
- `ambient_saw_low`, `ambient_bell`, `ambient_air` — drone, glass bells, breathing hiss.

## Reading a patch (tools/patch_view.py)

`python3 tools/patch_view.py patches/<name>.json --html [--render]`: every setting in its unit with its help,
changed values highlighted, envelopes drawn, modulation matrix as arrows; `--render` adds the measured sound.

## Judging a render (tools/analyze.py)

| Metric | Reads as |
|---|---|
| `attack_ms`, `decay_to_-20dB_s`, `sustain_ratio` | envelope as heard (probe only) |
| `centroid_hz` (start/mid/end) | brightness and how it evolves; < 500 dark, 1–3 k bright, > 4 k harsh |
| `noisiness` | 0 tonal … 0.3+ noisy |
| `band_energy_pct` | balance; `sub`+`low` > 80 % = bass-heavy |
| `harmonicity`, `cents_off` | pitched? in tune? (drift adds a few cents on purpose) |
| `am_rate_hz`, `am_depth_db` | movement: beating / LFO; 0.1–1 Hz slow, 4–7 Hz vibrato-like |
| `lr_correlation`, `width` | 1/0 = mono; < 0.5 / > 0.5 = wide |
| `peak_dbfs`, `clipped_samples` | headroom; must stay 0 clipped |
| `--ref`: `logmel_distance_db` | timbre distance to a reference: < 3 close, > 8 different |

## The `modal` engine (bells, bars, glass, membranes)

Each note is a bank of resonators. Choose the **object** with `model`, then shape it:

| Want | Turn |
|---|---|
| church bell | `model` bell, `decay` 8–12 s, `damping` 0.5, `hardness` 0.8+ |
| marimba / wood | `model` marimba, `decay` 0.8–1.5 s, `damping` 2+, `hardness` 0.4 |
| kalimba / music box | `model` bar, `position` 0.1–0.2, `decay` 2–3 s, `hardness` 0.7 |
| singing bowl, glass | `model` bowl, `decay` 10+ s, `damping` 0.3–0.5, soft `hardness` |
| bowed glass / endless drone | `bow` 0.6–1, `bow_attack` 1–3 s (+ chorus ensemble) |
| gong / tam-tam | `model` membrane, `stretch` 0.03–0.08, `decay` 10+, `noise` 0.3, soft `hardness` |
| invented materials | `stretch` ±0.1–0.3 bends all partial ratios |
| dull ↔ glassy | `brightness`, `hardness`, `vel_brightness` |
| alive, beating partials | `variation` 3–8 cents |
| key-up damper | `release` shorter than `decay` |

Struck notes decay by themselves even when the key is held; bowed notes sustain while held.

## Effect bus (any engine, any patch)

`phaser_*`, `flanger_*`, `chorus_*`, `delay_*`, `reverb_*`, `saturation*` and `limiter*` can be set in any patch (and per song track via `params`).

The five effects of the bus form a rack whose order is free: `fx_slot1` .. `fx_slot5` name the effect at each place (`phaser`, `flanger`, `chorus`, `delay`, `reverb`; that order by default). Each effect takes what comes out of the one before it, so the order is heard as soon as two are on: `"fx_slot1": "reverb", "fx_slot2": "delay"` puts the echoes on the reverb's tail instead of the reverb on the echoes; a phaser placed after the reverb sweeps the whole tail. Naming only the first places is enough: the effects not named follow in the usual order. There is one of each effect.

| Want | Set |
|---|---|
| slow swirl (pads, keys) | `phaser_mix` 0.6–1, `phaser_rate` 0.1–0.3 Hz, `phaser_feedback` 0.3–0.6, 4 stages |
| singing sweep | `phaser_feedback` 0.8, `phaser_stages` 8–12, `phaser_center` 1–2 kHz |
| jet (pads, drums, guitars-like) | `flanger_mix` 0.8–1, `flanger_rate` 0.1–0.2 Hz, `flanger_delay` 3–6 ms, `flanger_feedback` 0.6–0.8 |
| metallic, ringing | `flanger_delay` 1–2 ms, `flanger_feedback` 0.85 |
| hollow | `flanger_feedback` -0.6 to -0.85 |
| analog width on anything | `chorus_mode` I or I+II, `chorus_mix` 0.4–0.6 |
| string-machine shimmer | `chorus_mode` ensemble |
| ambient echoes | `delay_mix` 0.25–0.35, `delay_feedback` 0.4–0.6, `delay_tone` 2–4 kHz, `delay_wow` 0.2–0.4 |
| dub | `delay_pingpong` on, `delay_feedback` 0.6–0.75, `delay_drive` 0.5, dark `delay_tone` |
| tempo sync | `delay_time` = 60 / bpm × beats (0.75 = dotted eighth) |
| space | `reverb_mix` 0.3–0.6, `reverb_decay` 0.85–0.95, `reverb_damping` 3–5 kHz |
| infinite pad | play, then `reverb_freeze` on (CC 69): the tail holds, new notes play dry on top |
| glue, warmth | `saturation` soft or tube, `saturation_drive` 3–6 dB; tape: `saturation_tone` 8–12 kHz |
| driven mix | `saturation` tube or hard, `saturation_drive` 12–18 dB |
| louder, no clipping | `limiter` on, `limiter_gain` 3–6 dB, `limiter_ceiling` -1 dB, `limiter_release` 0.1–0.2 s |

Songs also have their own shared reverb (`"reverb"` block); the per-patch bus reverb is for the
sound itself (and for live play).

## Piano (`modal`, model `string`)

Start from `piano_grand` (concert), `piano_felt` (soft felt, intimate) or `piano_ambient` (pedal,
haze: the owner's favourite). Key controls: `inharmonicity` 3e-4..1e-3, `unison` 0.8-2 cents,
`aftersound` (attack/aftersound contrast), `decay` + `decay_keytrack` ~0.85, `position` 0.12
(hammer at 1/8), `hardness` + `vel_brightness`, `release` = damper (0.15-0.4 s) or pedal (2-3 s).
Use `modes` 32 and `humanize_velocity` / `humanize_ms` in phrases.

## String nonlinearity (`modal` `tension`)

A loud note stretches the string: the tension rises with the energy (mostly the low partials), so every partial
starts sharp and they all glide down together as the note decays. 0.3-0.6 for a piano (a few cents:
`piano_tension` vs `piano_ambient`), 1 for gongs and tam-tams (`gong_tension`). Stable at any setting.

## Drums and glitch (`va` + `pitch_env`)

`drum_kick` (sine, +30 st in 35 ms, play G1), `drum_snare` (tri + noise, D3), `drum_hat` /
`drum_ohat` (high-passed noise), `glitch_zap` (falling saw + ping-pong echo). Program beats at 1/16
or 1/32 steps; ghost notes with `?`, accents with `!`; see `tools/compose_warp_piano.py`.

## Drum machine (`drums`)

`kit` analog = deep long kick (tune it to the key of the song), soft snare, silky hats, cowbell,
toms: electro, hip-hop, ambient. `kit` hybrid = punchy clicky kick, bright snappy snare, gritty
6-bit hats and ride: house, techno. Per instrument: `<name>_level`, `_tune`, `_decay`, `_tone`
(kick: click; snare: snappy; hats/cymbal: brightness). `accent` makes velocity matter; `drive` glues
the kit. Program with step grids (see `docs/patch_format.md`), add `swing` 0.12-0.2 for house,
`pump` on chords and bass for sidechain feel. Examples: `songs/electro_analog.json`,
`techno_hybrid.json`, `house_piano.json`, `prog_pump.json`.

## Monophonic play, glide, slides (`va`)

`voice_mode`: poly, mono (last note wins, retriggers), legato (overlapping notes slide without
retriggering; releasing returns to the previous held key). `glide` 0.03-0.08 s = acid slide,
0.2-0.5 s = lazy lead; `glide_mode` legato = only tied notes glide. In phrases, `~` after a note
slides into the next one: `"C2! C2 C3~ D#2"`. See `acid_slide`.

## Glitch kit (`drums`, `kit` glitch)

Microsound palette on the GM drum notes: kick = gated sub sine pulse, snare = white burst (tone =
sample-rate reduction), clap = single-sample click + 8 kHz blip, chat = micro tick, ohat = 12 kHz
beep, toms = gated data tones 220/440/880 Hz (tone adds harmonics), cymbal = decimated 2-bit data
noise, cowbell = ring-modulated modem chirp, clave = sub pop. Decay = gate length. Use precise
grids, silences and pure sine drones (`sine_drone`); see `songs/microsound.json`.

## Classic instruments

- `acid` (transistor bass line spirit): ride `cutoff`, `resonance` 0.7-0.9, `env_mod` 2-3.5 oct,
  `decay` short; accents (`!`, velocity >= 96) get louder, brighter and the cutoff climbs over
  consecutive accents; `~` slides. Patches `acid_classic`, `acid_square`; song `acid_jam`.
- `mono` (early-70s monosynth spirit): footage `16`/`32` for basses, push `mixer_drive` for the fat
  overdriven ladder, `emphasis` ~0.95 self-oscillates, `release` on = notes ring with the decay time,
  `osc3_keytrack` off + range `32` = osc 3 as LFO for the mod wheel. Patches `mono_bass`, `mono_lead`.
- `poly` (late-70s polysynth spirit): `osca_sync` + `pm_fenv` + `pm_freq_a` = the sync lead;
  `pm_oscb` + `pm_freq_a` = FM bells; pulses + `lfo_pwm` = strings; `unison` = giant mono sounds;
  `drift` = analog slop. Patches `poly_brass`, `poly_strings`, `poly_sync_lead`, `poly_bell`,
  `poly_unison_bass`; song `synthwave_drive`.
- Drum machines: `drums` kits `analog` and `hybrid` (see above).

## Wavetable (`wt`)

- **Position is the main knob**: each table is a morph, so modulating `a_position` / `b_position` with an
  envelope (plucks: `harmonics` falling), an LFO (growls, pads) or velocity is what makes the sound move.
  Tables: `basic` (sine 0 -> triangle 0.33 -> saw 0.67 -> square 1), `pwm` (50 % -> 4 %), `sync` (ratio
  1 -> 8), `harmonics` (1 -> 64 harmonics), `vowel` (a-e-i-o-u), `fold`, `growl` (phase-modulation index
  0 -> 6), `resonant` (window x sine, 1 -> 16), `digital` (random spectra).
- **Warps** reshape the phase: `sync` brightens and screams when swept (env2 or mod wheel -> `a_warp`),
  `mirror` and `bend` add even/odd motion, `fm` lets osc B (or the sub, for osc B) phase-modulate, `ring`
  multiplies by it. Osc B at level 0 still feeds osc A's `fm`/`ring`.
- **Unison** 4-8 with 10-25 cents = width and density (supersaw-like with the saw frame); voices are mono,
  so width comes from the effect bus (chorus `I+II` or `ensemble`), `pan_spread` and the reverb.
- **Matrix**: 6 slots `modN_source -> modN_destination x modN_amount (x modN_via)`. Full scale: pitch
  +/-24 st, cutoff +/-8 oct, detune +/-50 cents, others +/-1. `via` = mod wheel turns a slot into a
  performance control (vibrato depth, warp amount).
- Recipes: `wt_hyper_lead`, `wt_growl_bass`, `wt_reese`, `wt_vowel_pad`, `wt_pluck`, `wt_sync_lead`,
  `wt_digital_pad`, `wt_fold_bass`; demo song `songs/wavetable_drive.json`.

## Per-voice movement, comb filters, tunings, MPE (every engine)

- **Per-voice modulation** (`vmseg_*`, `vlfo_*`, `vroute1..4_*`): every note gets its own MSEG and LFO, so the
  notes of a chord move independently. Destinations: `pitch` (+/-12 st), `level` (+/-24 dB), `pan`,
  `brightness` (+/-4 oct of the engine's main brightness: filter cutoff, FM modulator index, modal brightness,
  the comb's tuning in `wt`) and `timbre` (wavetable position in `wt`, strike/bow position in `modal`).
  Small amounts go far: brightness 0.03-0.06 = a few semitones of wandering.
- **Comb** (`wt` `filter_type` comb / comb_neg): cutoff = the comb's tuning (keytrack 1 = on the note; one or two
  octaves above sounds colder), resonance = feedback (0.8-0.9 = singing), drive = damping (low = glassy). Noise
  or digital tables through a wandering comb = metallic, frozen textures (`deep_comb_*`).
- **Glide**: every parameter moved while notes sound glides in ~5 ms (knobs, CCs, macros, routes), in every engine.
- **Tunings**: `tuning` (equal, just, pythagorean, meantone, werckmeister, custom) from `tuning_root`; pure
  intervals make long ambient chords beat less or differently. Scala files: `tools/scala_to_patch.py`.
- **MPE** (`part_mpe` on): per-note pitch bend, pressure and slide; pressure and slide are per-voice sources.

## More filter characters (`va`, `wt`)

- **formant**: three band-passes at the formants of a vowel; `filter_character` (va) or `drive` (wt) = vowel
  (0 a, 0.25 e, 0.5 i, 0.75 o, 1 u), cutoff = size of the "throat" (1 kHz = adult voice), resonance = sharpness.
  Route an MSEG or an LFO onto the vowel and the sound speaks (`va_talking_pad`).
- **scream**: 2-pole low-pass with a saturating feedback node (2x oversampled): rough, screaming resonance, self-
  oscillates from ~0.9; character/drive = how hard the node is pushed.
- **va second filter** (`filter2_*`, `filter_routing`): series (e.g. hp then lp = a band, lp then formant) or
  parallel (two formants = two voices, lp + hp mixed = a notch-like hole). It follows filter 1's envelope,
  keytracking and modulation.

## Coupled-oscillator networks (`coupled`)

Each note is a network of partials (Stuart-Landau oscillators) that act on each other; the movement comes from
inside, no LFO needed. Prototypes and measurements: `tools/experiments/` (stuart_landau.py, topologies.py).

- `phase_coupling` pulls detuned partials onto the harmonic series (measured: 22 -> 7 cents with 40 cents of
  random detune); `spread` / `drift` add the disorder it fights with.
- `amp_coupling` > 0: neighbours in step feed each other (breathing, `coupled_breathing`); < 0: they compete,
  partials flicker (`coupled_mosaic`).
- `pitch_by_level`: louder partials go flat; with strong couplings partials sweep and catch (`coupled_storm`).
- `freeze`: diffusive coupling; with little phase pull, the partials that cannot lock sink and a few ring on:
  sparse and glassy, different at every note (`coupled_frozen_glass`).

## Self-oscillating oscillators (`selfosc`)

A Van der Pol / Duffing oscillator forced by the note. `drive` morphs a sine (0) into a reedy relaxation wave
(2-3) and a square-ish one (5), in tune at any drive; `velocity_drive` makes hard notes reedier; `stiffness`
sharpens loud notes (Duffing); `entrain` locks the oscillator on the note; low entrain with some `detune` makes
it slip against the note: an irregular, LFO-free shimmer (`selfosc_beating`). Recipes: `selfosc_reed`
(formant filter on "o"), `selfosc_bass` (scream). Free (entrain 0) the pitch is within ~5 cents at full drive.


## Precision sines (`micro`)

Everything that shapes a hit runs per sample: envelope lengths are exact to the sample (a 0.2 s decay ends on
sample 9600), notes start on the sample the sequencer asked for, curves go from linear (0) to analog (> 0: fast
then slow) or gate-like (< 0). For exact rhythms built from pure tones.

- **Kick**: `pitch_mode` fixed (40–55 Hz), `pitch_amount` +30..+40 st, `pitch_time` 30–60 ms, `pitch_curve`
  0.6–0.8, `phase` 90 (the transient), a short `amp_hold`, `amp_decay` 0.3–0.6 s, `env_mode` one_shot, an
  impulse click at 2–4 kHz, a little `drive` (`micro_deep_kick`). Note: the drive's oversampling delays the
  sound by 0.24 ms.
- **Sub**: keyed, legato with a 50–100 ms `glide`, `sub_level` −6 dB, a few dB of drive so small speakers hear
  it (`micro_sub_pressure`).
- **Beating between the ears**: `osc2_mode` hz, `osc2_offset` 0.5–3 Hz, `osc2_split` on (`micro_beating_pair`).
- **Ticks**: `burst` 1–4 cycles of a fixed 2–5 kHz sine plus a noise click of 1–3 ms (`micro_click_grid`);
  `sine_level` at the bottom leaves the click alone.
- **Rolls**: `ratchet` 4–8 on 1/32 with `ratchet_decay` −2 dB (`micro_data_ratchet`).
- **Carved tones**: a held sine cut by `gate_mode` sync, a 16-bit `gate_pattern` (step 1 = lowest bit) and
  sub-millisecond `gate_edge` (`micro_gated_tone`); `gate_mode` free at 20–2000 Hz stutters a note or, fast,
  becomes a rhythmic ring modulation (`micro_high_tone`).
- **Tone against noise**: the noise layer has its own gate on the same steps. `noise_gate` inverse puts it in
  the tone's holes, `noise_env` step gives each opening step its own tick (`noise_decay` 10–80 ms). `stepped`
  noise at 8–16 kHz through a high-pass at 4–6 kHz sounds like digital grain (`micro_noise_interlock`). A
  `periodic` noise (a 7-bit register: a buzz at `noise_rate` / 127) through a band-pass, on its own
  `noise_pattern` and panned away from the tone, sounds like a data stream (`micro_data_burst`). The stepped
  and periodic kinds are not band-limited on purpose; their high-passed steps make sharp peaks, so start low
  with `noise_level`.
- **Low layer** (`low_level` up from its bottom): the weight, a sine between 20 and 160 Hz apart from the rest
  (in the centre, out of the drive). Every hit restarts it at `low_phase` (0 = soft, 90 = its own click) and
  runs its own attack / hold / decay, counted in samples: two hits are the same samples, and the decay ends on
  its last one. `low_drop` and `low_drop_time` are the punch (24 st in 30 ms = a kick, 7 st in 15 ms = a pluck);
  `low_curve` towards 1 leaves a tail, towards -1 holds then cuts; `low_drive` adds the harmonics small speakers
  need. `low_mode` note brings any key into the octave above `low_freq`: a bass line that stays between 41 and
  82 Hz (`micro_low_line`). With the gate on, `low_gate` gives it a hit on every step that opens: the tone's
  steps, its holes, or its own `low_pattern` (`micro_low_pulse`; with the tone and the noise,
  `micro_low_interlock`). Long attack and hold make pressure instead of hits (`micro_low_pressure`). `low_beat`
  adds a second low sine an exact number of hertz away: the two interfere and the level swells `low_beat` times
  a second; both restart together, so the swell falls on the same samples at every hit (`micro_low_beating`:
  3 Hz under a higher pair split left and right at 2 Hz). `low_beat_mix` 0.5 falls to silence at every beat,
  less is shallower.
- **The cold, exact register**: few materials, each one bare: a low sine as the only weight (`micro_low_pole`:
  started from its top, so the hit has its own click), white noise as rectangles cut on the sample
  (`micro_white_cut`: `noise_env` amp under an envelope of one sample of attack, a hold and a half-millisecond
  decay), pure tones of an exact number of cycles that start and end at zero (`micro_test_tone`: `burst` 24),
  ticks at the edge of hearing (`micro_ultra_tick`: 13 kHz, 3 ms, to one side), and silence between them. No
  reverb to speak of, hard pans, every length a number of samples.

See `songs/grille_fine.json` and `songs/trame_blanche.json`.

## Four-operator FM on a matrix (`fm4`)

The matrix says who modulates whom: row = the modulator, column = what it modulates, the diagonal = feedback, the
last column = how loud each operator is heard. A cell is squared (0.3 = a gentle index of about 1.1 radians, 0.5 =
pi, 1 = 4 pi), so the low end is fine-grained. In the web editor the eleven algorithm buttons wire the classic layouts
(links already set keep their amount, new ones start at 0.4) and the diagram shows the routing as it is. Every cell is
a modulation destination: an LFO on one cell moves one sideband family only.

- **Electric piano**: two pairs (algorithm 7): op 2 at unison into op 1 for the body, op 4 at 14 times the note
  with a 0.15 s decay into op 3 for the tine, velocity on both modulators, negative `key_scale` on them so the top
  stays sweet (`fm4_tine_piano`).
- **Bells**: non-integer ratios (`coarse` 3, `fine` 0.167 = 3.5; 7.7), carriers decaying over 2-4 s (`fm4_glass_bell`).
- **Bass**: a unison modulator with its own feedback (0.4-0.5) and a short decay, a sub operator at ratio 0.5, a
  few semitones of `pitch_amount` over 40 ms, a low-pass with its envelope (`fm4_punch_bass`).
- **Brass**: the modulator's attack slower than the carrier's (60 ms against 10 ms) so the tone brightens as it
  swells, some feedback, a second detuned pair (`fm4_brass_stab`).
- **What only a matrix does**: two operators modulating each other (1 -> 2 and 2 -> 1), one a hair off unison, the
  loop churns slowly (`fm4_cross_drone`). A link from a lower to a higher number runs a sample late (drawn dashed).
- **Organ**: all four as carriers at 1, 2, 3, 8, feedback on the top one for a reedy edge (`fm4_reed_organ`).

## Noise machine with a wide palette (`havoc`)

One note runs four generators (`g1_` .. `g4_`). Each picks one of sixteen processes with `g<n>_type`; `g<n>_a` and
`g<n>_b` shape it, and mean something else for each process:

| type | what it is | pitch | a | b |
|---|---|---|---|---|
| `tone` | sine with phase feedback | frequency | feedback: sine, saw (0.33), noise (1) | a second, detuned sine |
| `logistic` | the logistic map, one step per period | clock | 0 = a buzz an octave below the clock, 1 = chaos | stepped .. smooth |
| `byte` | an integer formula on a counter, 8 bits | speed of the counter | which of 8 formulas | its constant (1..15) |
| `dust` | random impulses into a resonator | impulses per second | frequency of the resonator | its ring: a click .. a second |
| `wind` | a band of white noise | centre of the band | wide .. a whistle | gusts |
| `pulsar` | a train of short sine bursts | rate of the train | frequency of the bursts (formant) | their length |
| `cell` | a cellular automaton of 32 cells read as a wave | frequency | the rule, 0..255 (rule / 255) | smoothing |
| `swarm` | eight sines wandering | centre | how far they wander | how fast |
| `computer` | a tone that jumps to random pitches | centre | range of the jumps | jumps per second |
| `rumble` | a leaky random walk | its speed | fold | only the peaks pass (crackle) |
| `bleep` | a chatter of short notes: sines, soft squares, glides, rests | centre | how far the notes go and glide | notes per second (1..32) |
| `glitch` | electronic faults one after the other: a stuck loop of bits, bit noise, a whine, a gap | centre | buzzing loops .. noise | faults per second (2..64) |
| `talk` | a voice speaking no language: vowels, hisses and stops through three formants | pitch of the voice (low = a giant) | how fast it speaks (2..12 phonemes a second) | voiced .. whispered |
| `modem` | a data transmission: carrier, preambles, bursts of random symbols, gaps | lowest tone | symbols per second (20..640) | 2, 4 or 8 tones in an octave |
| `neuron` | a model neuron firing | its speed | regular spikes .. chattering bursts | input current and its noise |
| `alias` | a staircase saw far above its pitch, not band-limited: only its foldings are heard | frequency | how far above (1..64 times) | 256 steps .. 2 |

- **Pitch**: `g<n>_pitch` is in semitones from the note when `g<n>_track` is on, from middle C when it is off;
  it goes down to -96 (1 Hz), where a generator is a rhythm or a slow modulator rather than a sound.
- **Rot**: `g<n>_rot` resamples one generator at a lower rate and on fewer bits, with no filter (0.3 = a grain,
  0.6 = 2 kHz and 4 bits). It sits before the ring of modulation: a rotten generator sends its aliases into the
  frequency or the amplitude of the next.
- **Six macros** carry every sound (and the board's knobs): Rot (the rot of the four generators), Motion (how far
  their own oscillators move their shapes), Chaos (feedback and mutations), Density (the rates of their lives),
  Dark (the low-pass `dark` on the whole machine: 0 = open, 1 = 140 Hz), Space (the reverb). All start at 0, where
  the sound is as it was written. In the browser a generator's card shows its process, pitch, two shapes under
  their real names (Feedback and Detune for `tone`, Speed and Whisper for `talk`...), life and level; the rest is
  under More.
- **Moving timbre**: every generator has its own slow oscillator: `g<n>_lfo_rate`, and how far it moves a
  (`g<n>_lfo_a`) and b (`g<n>_lfo_b`, at 0.62 of the rate, so the pair of shapes never repeats). Give the four
  generators rates with no simple ratio (0.043, 0.071, 0.117, 0.19 Hz in): a held note
  never passes twice through the same sound. On `byte` it changes the formula, on `cell` the rule, on `pulsar`
  the vowel, on `tone` and `logistic` it goes from a note to noise and back.
- **Life**: `g<n>_life` is how the generator exists in time: `steady`, `bursts` (on and off at random,
  `life_rate` times a second, on for `life_shape` of the time), `pulse` (a decaying hit every period: rhythm),
  `swell` (a slow random breath). Three generators with three lives are three layers of one scene.
- **Ring of modulation**: `g<n>_fm` and `g<n>_ring` let the generator before (the fourth for the first)
  modulate this one's frequency or amplitude. A modulator can be silent itself (`level` 0). A slow stepped
  `logistic` into the `fm` of a `tone` is a random melody; a slow saw (`tone`, a 0.33) is a
  repeated dive; a `byte` into the `ring` of anything is digital hash.
- **Processors**: `x1_type` and `x2_type`, in series on the sum: `lp`, `bp`, `hp`, `comb` (a tuned pipe),
  `fold`, `crush`, `ring` (a sine multiplies the sum), `chop` (a periodic gate), `stutter` (now and then the last
  2 to 42 ms repeat as if stuck: p1 = the length, p2 = the share of the time; it makes a fault of any sound),
  `sub` (a square one or two octaves below whatever comes in, at the level of the input: `mix` 0.5 puts a growl
  under the sound), `warp` (frequency modulation of the whole sound by a sine: p1 low = a tape that drags, p1 high
  = sidebands on everything), `mangle` (the bits of every sample: exclusive or with a mask, rotation, bits
  killed, bits reversed; `mix` 0.2 is already a lot).
- **Chaos**: `feedback` sends the machine's output into the frequency of every generator (0.25 roughens, 0.6
  howls, 0.85 collapses). `mutate` makes the machine change itself while it plays: at random, one generator gets
  other shapes and another pitch, and above `mutate_depth` 0.5 sometimes another process (`havoc_meltdown`: 0.8
  and 1). The same note always mutates the same way.
- **Machines that speak**: `talk` low and slow with a `sub` is an oracle (`havoc_oracle`), on the note it sings, several at once are a crowd; with its `ring` driven by a `tone`
  it is a voice on a radio, with its `fm` driven by a `neuron` it stammers. `modem` against `talk` is a dialogue (`havoc_handshake`). `neuron` at a
  low pitch is single spikes. `alias` with its a moved slowly makes foldings that cross
  (`havoc_nyquist`). Everything at its end:.
- **Dark**: low pitches (-24 and under), `pulsar` with a low formant, `swarm` under the hearing range (beats),
  a `sub` and a low-pass, slow `swell` lives, and the long dark reverb of the bus (`reverb_decay` 0.95 and more,
  `reverb_damping` near 2000 Hz):, `havoc_leviathan`.
- **Where to start**: sparse events = `dust` at a very low pitch (`havoc_wind_chimes` with a long
  ring); weather = `wind` and dense `dust` (`havoc_solar_wind`); fire = `rumble` with b high; machines = `logistic` or `cell` with a `pulse` life into a `comb` (`havoc_automaton`); computers = `computer` and `byte`; creatures =
  `swarm` and `pulsar` (`havoc_insects`); science fiction = `bleep` for anything that talks or reports (`havoc_droid_talk`), `glitch` and `stutter`
  for what breaks, a slow saw into `fm` for sweeps; walls = `tone` with a near 1 and `fm`. `pulsar`, `tone`, `logistic` and `cell` with `track` on play in tune.
- The same note always gives the same sequence of random events. `byte` and `cell` alias on purpose.

