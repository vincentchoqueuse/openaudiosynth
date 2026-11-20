#!/usr/bin/env python3
# compose_warp_piano.py — generates songs/warp_piano.json: an original, early-90s-IDM-flavoured piece
# built around the `piano_ambient` patch: arpeggiated pedal piano, detuned pad, sub, broken-beat drums
# with ghost notes, 32nd-note hat ratchets and drill snare fills, glitch zaps in a ping-pong echo.
# Example of composing programmatically for the song format (docs/patch_format.md).
# Usage: python3 tools/compose_warp_piano.py && ./build-host/host/synth_render --song songs/warp_piano.json --out x.wav
import json

BPM = 92

# 8-bar progression, 2 bars per chord: (low root, low fifth, 4-note voicing, melody per bar [(m1, m2), (m1, m2)])
A = [
    ("F#2", "C#3", ["A3", "C#4", "E4", "G#4"], [("C#5", "E5"), ("G#5", "F#5")]),   # F#m9
    ("D2", "A2", ["F#3", "A3", "C#4", "G#4"], [("E5", "C#5"), ("G#5", "A5")]),     # Dmaj7#11
    ("B1", "F#2", ["D3", "F#3", "A3", "C#4"], [("F#5", "D5"), ("C#5", "B4")]),     # Bm9
    ("C#2", "G#2", ["F#3", "G#3", "B3", "E#4"], [("G#4", "B4"), ("E#5", "C#5")]),  # C#7sus4 -> C#7
]
B_MEL = [[("A5", "G#5"), ("E5", "C#5")], [("F#5", "G#5"), ("A5", "C#6")],
         [("B5", "A5"), ("F#5", "D5")], [("E#5", "G#5"), ("B5", "G#5")]]
CYCLES = ["A", "B", "A", "B"]  # 4 x 8 bars, then a 4-bar final chord


def piano_bar(root, fifth, v, m1, m2):
    """8 eighths: low root + chord, arpeggio, melody on the 4th and 8th eighth, low fifth on beat 3."""
    return [f"{root}+{v[0]}", v[1], v[2], m1, f"{fifth}+{v[1]}", v[2], v[3], m2]


piano, pad, sub = [], [], []
for c, kind in enumerate(CYCLES):
    for k, (root, fifth, v, mel) in enumerate(A):
        if kind == "B":
            mel = B_MEL[k]
        for bar in range(2):
            piano += piano_bar(root, fifth, v, *mel[bar])
        pad += ["+".join(v)] + ["_"] * 7          # 1 step = 1 bar (pad track step = 4 beats)... see below
        sub += ([root.replace("2", "1") if "2" in root else root] + ["_"] * 15) if c >= 1 else ["-"] * 16
# final: F#m(add9) held 4 bars
piano += ["F#2+C#3+A3+C#4+G#4+E5"] + ["_"] * 31
pad_steps = []
for c in range(len(CYCLES)):
    for root, fifth, v, mel in A:
        pad_steps += ["+".join(v)] + ["_"]  # 2 bars per chord, step = 1 bar
pad_steps += ["A3+C#4+E4+G#4", "_", "_", "_"]
sub += ["F#1"] + ["_"] * 63
BARS = 32 + 4


def bars32(fn, nbars=32):
    """Concatenates per-bar 32nd-note patterns from fn(bar) -> list of 32 tokens."""
    out = []
    for b in range(nbars):
        out += fn(b)
    return out + ["-"] * 32 * (BARS - nbars)


def section(b):
    return CYCLES[b // 8], b % 8


def kick(b):
    kind, i = section(b)
    p = ["-"] * 16
    if b < 4 or (kind == "A" and b >= 16 and b < 24):
        return ["-"] * 16
    hits = [0, 10] if i % 2 == 0 else [0, 7, 10]
    if i == 7:
        hits = [0, 7]  # leave room for the fill
    for h in hits:
        p[h] = "G1!" if h == 0 else "G1"
    return p


def snare32(b):
    kind, i = section(b)
    p = ["-"] * 32
    if b < 8 or (16 <= b < 24):
        return p
    p[8], p[24] = "D3", "D3!"
    p[14], p[30] = "D3?", "D3?"
    if b % 2 == 1:
        p[19] = "D3?"
    if i == 7:  # drill roll on the last beat
        p[24:32] = ["D3?", "D3?", "D3?", "D3", "D3", "D3", "D3!", "D3!"]
    return p


def hat32(b):
    kind, i = section(b)
    p = ["-"] * 32
    if b < 4:
        return p
    for s in range(0, 32, 2):  # sixteenths
        p[s] = "F#5" if s % 4 == 0 else "F#5?"
    if b >= 24:  # last cycle: 32nd ratchets, a different place in every bar
        for s in [(5 + 6 * b) % 32, (13 + 10 * b) % 32, (27 + 4 * b) % 32]:
            if s % 2 == 1:
                p[s] = "F#5?"
                p[s - 1] = "F#5"
    if 16 <= b < 24:  # breakdown: only a few sparse ticks
        p = ["-"] * 32
        for s in [(3 + 7 * b) % 32, (17 + 5 * b) % 32]:
            p[s] = "F#5?"
    return p


def ohat(b):
    kind, i = section(b)
    p = ["-"] * 16
    if 8 <= b < 16 or b >= 24:
        if i % 2 == 1:
            p[14] = "A#5"
    return p


ZAP_NOTES = ["F#6", "A6", "C#6", "E6", "B5", "G#6"]


def zap(b):
    kind, i = section(b)
    p = ["-"] * 16
    if b >= 16:
        for j, s in enumerate([(3 + 5 * b) % 16, (11 + 3 * b) % 16]):
            if (b + j) % 3 != 0:
                p[s] = ZAP_NOTES[(b * 2 + j) % len(ZAP_NOTES)] + ("?" if j else "")
    return p


def per16(fn, nbars=32):
    out = []
    for b in range(nbars):
        out += fn(b)
    return out + ["-"] * 16 * (BARS - nbars)


song = {
    "version": 1,
    "name": "warp_piano",
    "description": "Original piece in an early-90s IDM / Warp spirit: the hazy pedal piano (piano_ambient) "
                   "arpeggiates a F#m9 - Dmaj7#11 - Bm9 - C#7sus progression; a detuned saw pad, sub swells; "
                   "broken-beat drums with ghost notes, 32nd-note hat ratchets and drill snare fills; glitch "
                   "zaps in a ping-pong echo; breakdown in bars 17-24.",
    "bpm": BPM,
    "length_beats": BARS * 4,
    "tail_s": 8,
    "tracks": [
        {"patch": "patches/piano_ambient.json", "step": 0.5, "notes": " ".join(piano), "gate": 0.95,
         "velocity": 86, "humanize_velocity": 12, "humanize_ms": 14, "gain_db": 0, "reverb_send": 0.25},
        {"patch": "patches/ambient_saw_pad.json", "step": 4, "notes": " ".join(pad_steps),
         "gain_db": -15, "reverb_send": 0.5,
         "params": {"amp_attack": 3.0, "amp_release": 3.0, "cutoff": 700, "chorus_mode": "I+II", "chorus_mix": 0.5}},
        {"patch": "patches/ambient_sub.json", "step": 0.25, "notes": " ".join(sub), "gain_db": -10,
         "reverb_send": 0.0, "params": {"amp_attack": 0.8, "amp_release": 1.0}},
        {"patch": "patches/drum_kick.json", "step": 0.25, "notes": " ".join(per16(kick)), "gain_db": 3,
         "velocity": 105, "humanize_velocity": 6, "reverb_send": 0.03},
        {"patch": "patches/drum_snare.json", "step": 0.125, "notes": " ".join(bars32(snare32)), "gain_db": 13,
         "velocity": 100, "humanize_velocity": 8, "humanize_ms": 3, "reverb_send": 0.25},
        {"patch": "patches/drum_hat.json", "step": 0.125, "notes": " ".join(bars32(hat32)), "gain_db": 24,
         "velocity": 90, "humanize_velocity": 14, "humanize_ms": 4, "reverb_send": 0.08, "pan": 0.25},
        {"patch": "patches/drum_ohat.json", "step": 0.25, "notes": " ".join(per16(ohat)), "gain_db": 20,
         "velocity": 85, "reverb_send": 0.15, "pan": -0.2},
        {"patch": "patches/glitch_zap.json", "step": 0.25, "notes": " ".join(per16(zap)), "gain_db": -3,
         "velocity": 90, "reverb_send": 0.35},
    ],
    "reverb": {"decay": 0.86, "damping_hz": 5000, "predelay_s": 0.03, "size": 1.6, "return_db": -2},
    "master": {"fade_in_s": 0.5, "fade_out_s": 10},
}
for t in song["tracks"]:  # sanity: every phrase fits the song length
    steps = len(t["notes"].split())
    assert steps * t["step"] <= song["length_beats"] + 1e-9, (t["patch"], steps * t["step"], song["length_beats"])
json.dump(song, open("songs/warp_piano.json", "w"), indent=2)
open("songs/warp_piano.json", "a").write("\n")
print("songs/warp_piano.json: %d bars at %d bpm = %.0f s" % (BARS, BPM, BARS * 4 * 60 / BPM))
