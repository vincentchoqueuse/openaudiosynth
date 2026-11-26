#!/usr/bin/env python3
"""scala_to_patch.py — converts a 12-note octave-repeating Scala tuning (.scl) into the synth's custom tuning
parameters (tuning = custom, tune_0..tune_11 in cents away from equal temperament), printed as a JSON
fragment to paste into a patch (its "tuning" module), or merged into a patch with --patch.

  python3 tools/scala_to_patch.py my_scale.scl [--root D] [--patch patches/x.json]

Scala format: comment lines start with "!", then a description, the number of notes, and one pitch per line
(cents if it contains a ".", otherwise a ratio such as 5/4 or 2). The last pitch is the octave.
"""
import argparse
import json
import math
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patchtree  # noqa: E402

ROOTS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def parse_scl(path):
    lines = [l.strip() for l in open(path, encoding="latin-1") if not l.strip().startswith("!")]
    count = int(lines[1].split()[0])
    pitches = []
    for l in lines[2:2 + count]:
        tok = l.split()[0]
        if "." in tok:
            pitches.append(float(tok))
        else:
            num, _, den = tok.partition("/")
            pitches.append(1200.0 * math.log2(float(num) / float(den or 1)))
    return lines[0], pitches


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scl")
    ap.add_argument("--root", default="C", choices=ROOTS)
    ap.add_argument("--patch", help="merge into this patch file")
    a = ap.parse_args()
    desc, pitches = parse_scl(a.scl)
    if len(pitches) != 12 or abs(pitches[-1] - 1200.0) > 0.01:
        sys.exit(f"{a.scl}: {len(pitches)} notes, period {pitches[-1]:.2f} cents: only 12-note octave scales are supported")
    degrees = [0.0] + pitches[:11]
    params = {"tuning": "custom", "tuning_root": a.root}
    for k, c in enumerate(degrees):
        off = round(c - 100.0 * k, 3)
        if abs(off) > 100.0:
            sys.exit(f"degree {k} is {off:+.1f} cents from equal temperament (limit +/-100)")
        params[f"tune_{k}"] = off
    if a.patch:
        doc = json.load(open(a.patch))
        for k, v in params.items():
            patchtree.set(doc, k, v)
        with open(a.patch, "w") as f:
            f.write(patchtree.dumps(doc))
        print(f"{a.patch}: tuning '{desc}' from {a.root}")
    else:
        print(patchtree.dumps(patchtree.tree("va", params)), end="")


if __name__ == "__main__":
    main()
