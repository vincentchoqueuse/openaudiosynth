#!/usr/bin/env python3
"""golden.py — sound regression guard: renders every patch and song and compares a SHA-256 of the
audio with tests/golden.json. A refactoring must keep every hash; an intended sound change is
recorded explicitly with --update (and shows up in the diff of golden.json).

  python3 tools/golden.py --render build-host/host/synth_render           # check
  python3 tools/golden.py --render build-host/host/synth_render --update  # record
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "tests", "golden.json")


def audio_hash(path):
    with open(path, "rb") as f:
        data = f.read()
    return hashlib.sha256(data[44:]).hexdigest()[:16]  # PCM only, after the 44-byte header


def render_all(render):
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        for kind, folder in (("patch", "patches"), ("song", "songs")):
            for name in sorted(os.listdir(os.path.join(ROOT, folder))):
                if not name.endswith(".json"):
                    continue
                wav = os.path.join(tmp, "x.wav")
                src = os.path.join(ROOT, folder, name)
                r = subprocess.run([render, "--" + kind, src, "--out", wav], capture_output=True, text=True, cwd=ROOT)
                log = r.stdout + r.stderr
                if r.returncode or any(w in log for w in ("warning", "error", "unknown", "clamped")):
                    sys.exit(f"{os.path.relpath(src, ROOT)}: render failed or warned\n{log}")
                out[f"{kind}/{name[:-5]}"] = audio_hash(wav)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, help="path to synth_render")
    ap.add_argument("--update", action="store_true", help="record the current sound as the reference")
    args = ap.parse_args()
    now = render_all(os.path.abspath(args.render))
    if args.update:
        try:
            with open(GOLDEN) as f:
                old = json.load(f)
        except FileNotFoundError:
            old = {}
        for k in sorted(set(now) | set(old)):  # say what this update records: review it before committing
            if k not in old:
                print(f"new      {k}")
            elif k not in now:
                print(f"removed  {k}")
            elif now[k] != old[k]:
                print(f"CHANGED  {k}")
        with open(GOLDEN, "w") as f:
            json.dump(now, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"recorded {len(now)} renders in {os.path.relpath(GOLDEN, ROOT)}")
        return 0
    try:
        with open(GOLDEN) as f:
            ref = json.load(f)
    except FileNotFoundError:
        sys.exit("no tests/golden.json: run with --update first")
    changed = sorted(k for k in now if k in ref and now[k] != ref[k])
    new = sorted(k for k in now if k not in ref)
    gone = sorted(k for k in ref if k not in now)
    for k in changed:
        print(f"CHANGED  {k}")
    for k in new:
        print(f"new      {k}  (not in golden.json yet)")
    for k in gone:
        print(f"missing  {k}")
    print(f"{len(now)} renders: {len(changed)} changed, {len(new)} new, {len(gone)} missing")
    return 1 if changed or new or gone else 0  # new: record it with --update (just golden)


if __name__ == "__main__":
    sys.exit(main())
