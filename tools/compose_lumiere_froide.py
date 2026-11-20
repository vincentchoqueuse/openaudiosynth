#!/usr/bin/env python3
# compose_lumiere_froide.py — generates songs/lumiere_froide.json: an original slow 3/4 piece in E minor in the
# spirit of Satie's bass-then-chord sway, dressed like cold 90s ambient: the `piano_cold` patch far back in a
# long plate, a worn-tape saw pad (`pad_cold_tape`) and tape air. Example of composing for docs/patch_format.md.
# Usage: python3 tools/compose_lumiere_froide.py && ./build-host/host/synth_render --song songs/lumiere_froide.json --out x.wav
import json

BPM = 66  # quarter notes; 3 beats per bar, one step = one beat

# chord name -> (bass, voicing on beat 2)
CH = {
    "Em9": ("E2", "G3+B3+D4+F#4"), "Cmaj7": ("C2", "E3+G3+B3"), "Am9": ("A1", "G3+B3+C4+E4"),
    "G6": ("G2", "E3+B3+D4"), "Em7": ("E2", "G3+B3+D4"), "Fmaj7#11": ("F2", "A3+B3+C4+E4"),
    "B7sus": ("B1", "A3+E4+F#4"), "B7": ("B1", "A3+D#4+F#4"),
}
INTRO = ["Em9", "Cmaj7", "Em9", "Cmaj7"]
A1 = ["Em9", "Cmaj7", "Em9", "Cmaj7", "Am9", "G6", "Cmaj7", "B7sus"]
A2 = ["Em9", "Cmaj7", "Em9", "Cmaj7", "Am9", "Fmaj7#11", "B7sus", "Em9"]
B = ["Am9", "Em7", "Fmaj7#11", "Em9", "Am9", "G6", "Fmaj7#11", "B7"]
A3 = A1[:7] + ["Em9"]
# melody, 3 tokens (beats) per bar: sparse, falling, mostly long notes ('-' rest, '_' tie)
M_A1 = ["- B4 D5", "E5 _ _", "- F#5 G5", "B4 _ _", "- C5 B4", "A4 _ _", "- G4 F#4", "E4 _ D#4"]
M_A2 = ["- B4 D5", "G5 _ _", "- F#5 E5", "B4 _ _", "- E5 D5", "C5 _ _", "- B4 A4", "B4 _ _"]
M_B = ["- - E5", "D5 _ _", "- C5 E5", "B4 _ _", "- A4 C5", "B4 _ D5", "E5 _ _", "_ _ D#5"]
M_A3 = M_A1[:7] + ["E4 _ _"]
FORM = [(INTRO, None), (A1, M_A1), (A2, M_A2), (B, M_B), (A3, M_A3)]
CODA_BARS = 4


def piano_bar(name, k):
    bass, chord = CH[name]
    soft = "?" if k % 2 else ""  # every other bass note softer: the sway
    return [bass + soft, chord, "_"]


piano, melody, pad, air = [], [], [], []
for chords, mel in FORM:
    for k, name in enumerate(chords):
        piano += piano_bar(name, k)
        melody += mel[k].split() if mel else ["-"] * 3
        pad += [CH[name][1], "_", "_"]
coda = "E2+B2+G3+B3+F#4+B4"
piano += [coda] + ["_"] * (3 * CODA_BARS - 1)
melody += ["-"] * (3 * CODA_BARS)
pad += ["G3+B3+F#4"] + ["_"] * (3 * CODA_BARS - 1)
beats = len(piano)
air = ["E4"] + ["_"] * (beats - 1)

song = {
    "version": 1,
    "name": "lumiere_froide",
    "description": "Original slow 3/4 piece in E minor: Satie-like bass-then-chord sway (Em9 / Cmaj7, a darker middle with Fmaj7#11 and B7), a sparse falling melody, a cold piano far back in a long plate with tape-warbled pitch and echoes, a worn-tape saw pad and tape air: cold 90s ambient melancholy.",
    "bpm": BPM,
    "length_beats": beats,
    "tail_s": 14,
    "tracks": [
        {"patch": "patches/piano_cold.json", "step": 1, "notes": " ".join(piano), "gain_db": -9, "pan": -0.05,
         "reverb_send": 0.7, "velocity": 58, "gate": 1, "humanize_velocity": 10, "humanize_ms": 18},
        {"patch": "patches/piano_cold.json", "step": 1, "notes": " ".join(melody), "gain_db": 9, "pan": 0.08,
         "reverb_send": 0.8, "velocity": 76, "gate": 1, "humanize_velocity": 8, "humanize_ms": 22,
         "params": {"brightness": 0.5, "hardness": 0.35}},
        {"patch": "patches/pad_cold_tape.json", "step": 1, "notes": " ".join(pad), "gain_db": -12,
         "reverb_send": 0.6, "velocity": 70, "gate": 1},
        {"patch": "patches/ambient_air.json", "step": 1, "notes": " ".join(air), "gain_db": -6,
         "reverb_send": 0.2, "velocity": 80, "gate": 1},
    ],
    "reverb": {"decay": 0.94, "damping_hz": 5000, "predelay_s": 0.06, "size": 2.0, "return_db": 0},
    "master": {"fade_in_s": 3, "fade_out_s": 12},
}
json.dump(song, open("songs/lumiere_froide.json", "w"), indent=2)
open("songs/lumiere_froide.json", "a").write("\n")
print(f"songs/lumiere_froide.json: {beats // 3} bars, {beats * 60 / BPM:.0f} s + tail")
