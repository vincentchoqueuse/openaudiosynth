#!/usr/bin/env python3
# compose_duel_froid.py — generates songs/duel_froid.json: an original, sparse piece for loosely forced sines in close
# counterpoint in E (phrygian colour), each through its own resonant comb (sine_duel_a / _b / _c), with rare glass
# points (glass_points). Four sections: I duel of two voices; II a low third voice drones and rubs underneath;
# III crisis, the seconds tighten and overlap while the combs sweep (MSEG on their tuning); IV the three voices
# converge on E (unison and octave) and beat slowly until they fade. Cold and narrow-band.
# Usage: python3 tools/compose_duel_froid.py && ./build-host/host/synth_render --song songs/duel_froid.json --out x.wav
import json

BPM = 50  # one step = one beat = 1.2 s
LENGTH = 178

# (start beat, length in beats, note)
# I — the duel (0-48): minor seconds that grind, a unison that beats
A1 = [(0, 8, "E4"), (8, 6, "D4"), (16, 8, "E4"), (24, 10, "B3"), (36, 6, "C4"), (42, 6, "B3")]
B1 = [(4, 8, "F4"), (12, 6, "E4"), (18, 6, "E4"), (26, 8, "C4"), (34, 6, "B3"), (44, 4, "F4")]
# II — the third voice (48-100): low drone underneath, the two above swap B/C
A2 = [(52, 10, "B3"), (64, 8, "C4"), (74, 10, "B3"), (86, 8, "E4")]
B2 = [(56, 8, "C4"), (66, 8, "B3"), (78, 8, "C4"), (90, 8, "F4")]
C2 = [(48, 12, "E3"), (62, 10, "F3"), (74, 12, "B2"), (88, 12, "C3")]
# III — crisis (100-140): shorter notes, overlapping seconds, combs sweeping
A3 = [(100, 6, "E4"), (106, 6, "F4"), (112, 8, "E4"), (122, 10, "D4"), (133, 6, "C4")]
B3 = [(102, 6, "F4"), (108, 6, "E4"), (116, 8, "F4"), (126, 10, "E4"), (134, 6, "B3")]
C3 = [(100, 20, "E3"), (122, 16, "F3")]
# IV — dissolution (140-178): convergence on E, unison and octave, slow beating, fade
A4 = [(142, 26, "E4")]
B4 = [(148, 22, "E4")]
C4 = [(140, 30, "E3")]
GLASS = [(10, 4, "B5"), (30, 4, "E6"), (47, 4, "F5"), (59, 4, "B5"), (70, 4, "F5"), (95, 4, "B5"),
         (118, 3, "C6"), (131, 3, "F5"), (152, 4, "E6"), (166, 6, "B5")]

# section III: the combs sweep (looping MSEG 2 on the comb tuning, about +/- half an octave over 13 s)
SWEEP = {"mseg2_mode": "loop_free", "mseg2_points": 4, "mseg2_sustain": "none", "mseg2_loop_start": 1,
         "mseg2_loop_end": 4, "mseg2_t1": 0, "mseg2_l1": 0, "mseg2_t2": 5, "mseg2_l2": 1, "mseg2_c2": -0.5,
         "mseg2_t3": 3, "mseg2_l3": 0.35, "mseg2_c3": 0.3, "mseg2_t4": 5, "mseg2_l4": 0,
         "route6_source": "mseg2", "route6_destination": "cutoff", "route6_amount": 0.05}


def tokens(events, length):
    """Event list -> one token per beat: the note on its first beat, '_' while held, '-' when silent."""
    out = ["-"] * length
    for start, dur, note in events:
        out[start] = note
        for k in range(start + 1, min(start + dur, length)):
            out[k] = "_"
    return " ".join(out)


def track(patch, events, gain, pan, send=0.6, params=None):
    t = {"patch": f"patches/{patch}.json", "step": 1, "notes": tokens(events, LENGTH), "gain_db": gain, "pan": pan,
         "reverb_send": send, "velocity": 80, "gate": 1}
    if params:
        t["params"] = params
    return t


song = {
    "version": 1,
    "name": "duel_froid",
    "description": "Original sparse piece for loosely forced sines in close counterpoint in E (phrygian colour), each through its own slowly wandering resonant comb: I a duel of two voices (minor seconds that grind, unisons that beat, notes sliding into place); II a low third voice drones and rubs underneath; III crisis, overlapping seconds while the combs sweep; IV the three converge on E and beat slowly until they fade. Rare glass points, a long large plate. Cold and narrow-band.",
    "bpm": BPM,
    "length_beats": LENGTH,
    "tail_s": 18,
    "tracks": [
        track("sine_duel_a", A1 + A2 + A4, 0, -0.35),
        track("sine_duel_b", B1 + B2 + B4, 0, 0.35),
        track("sine_duel_a", A3, 3, -0.45, params=SWEEP),
        track("sine_duel_b", B3, 3, 0.45, params=SWEEP),
        track("sine_duel_c", C2 + C4, -2, 0.0, send=0.5),
        track("sine_duel_c", C3, -2, 0.0, send=0.5, params={**SWEEP, "route6_amount": 0.03}),
        track("glass_points", GLASS, 4, 0.0, send=0.7),
    ],
    "reverb": {"decay": 0.95, "damping_hz": 7000, "predelay_s": 0.04, "size": 2.0, "return_db": 0},
    "master": {"fade_in_s": 1, "fade_out_s": 14},
}
json.dump(song, open("songs/duel_froid.json", "w"), indent=2)
open("songs/duel_froid.json", "a").write("\n")
print(f"songs/duel_froid.json: {LENGTH * 60 / BPM:.0f} s + tail")
