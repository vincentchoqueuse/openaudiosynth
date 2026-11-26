#!/usr/bin/env python3
# validate.py — checks every patch and song against the open format (spec/schemas, OID 0.3) and against
# the instrument description (catalog): engine and parameter names or roles, choices, ranges, pattern
# rows, referenced patches; and checks the catalog's roles against the vocabulary (spec/roles.json).
#
#   python3 tools/validate.py --catalog-cmd ./build-host/host/synth_catalog
#   python3 tools/validate.py --catalog docs/catalog.json patches/x.json songs/y.json
# Exit status 1 if any error. Warnings (out-of-range values that the loader would clamp) do not fail.
import argparse
import glob
import json
import os
import re
import subprocess
import sys

import jsonschema

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patchtree  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTE_RE = re.compile(r"^[A-Ga-g][#b]?-?\d+$")


def load(path):
    with open(path) as f:
        return json.load(f)


def role_pattern(role):
    """Generic form of a role: indexes and instrument names become <n> / <instrument>."""
    role = re.sub(r"^drum\.[a-z0-9_]+\.", "drum.<instrument>.", role)
    return re.sub(r"([a-z_]+)\d+(?=\.)", r"\1<n>", role)


class Checker:
    def __init__(self, catalog):
        self.cat = catalog
        self.engines = catalog["engines"]
        self.glob = catalog["global"]["params"]
        # parameters every engine has: global, part (mixer strip), modulation (macros, LFOs, MSEGs, routes)
        self.shared = dict(self.glob)
        for section in ("part", "modulation"):
            self.shared.update(catalog.get(section, {}).get("params", {}))
        self.errors, self.warnings = [], []
        sch = os.path.join(ROOT, "spec", "schemas")
        self.patch_schema = load(os.path.join(sch, "patch.schema.json"))
        self.song_schema = load(os.path.join(sch, "song.schema.json"))

    def err(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def warn(self, where, msg):
        self.warnings.append(f"{where}: {msg}")

    def schema(self, where, doc, schema):
        for e in sorted(jsonschema.Draft202012Validator(schema).iter_errors(doc), key=lambda e: list(e.path)):
            self.err(where, f"schema: {'/'.join(map(str, e.path)) or '(root)'}: {e.message}")

    def resolve(self, engine, key):
        """Parameter descriptor for a name or a role (engine first, then the shared ones)."""
        for table in (self.engines[engine]["params"], self.shared):
            if key in table:
                return table[key]
            hits = [d for d in table.values() if d.get("role") == key]
            if hits:
                return hits[0]
        return None

    def roles(self, vocabulary):
        sections = [("global", self.glob)] + [(k, self.cat[k]["params"]) for k in ("part", "modulation") if k in self.cat]
        for scope, params in sections + [(e, v["params"]) for e, v in self.engines.items()]:
            seen = {}
            for name, d in params.items():
                r = d.get("role")
                if not r:
                    self.err("catalog", f"{scope}.{name}: no role")
                    continue
                if r not in vocabulary and role_pattern(r) not in vocabulary:
                    self.err("catalog", f"{scope}.{name}: role '{r}' not in spec/roles.json")
                if r in seen:
                    self.err("catalog", f"{scope}: role '{r}' used by both '{seen[r]}' and '{name}'")
                seen[r] = name

    def params(self, where, engine, params):
        for key, value in params.items():
            d = self.resolve(engine, key)
            if d is None:
                self.err(where, f"unknown {engine} parameter or role '{key}'")
                continue
            if isinstance(value, str) and d.get("role", "").startswith("route") and d["role"].endswith(".destination"):
                if value != "none":  # an engine parameter, or an effect's (master and effect chain), that moves in real time
                    t = self.resolve(engine, value)
                    glob = any(t is g for g in self.glob.values())
                    fx = glob and t.get("role") != "tempo.bpm"  # pm_global_target: not the tempo nor the tuning
                    if t is None or (not fx and any(t is x for x in self.shared.values())):
                        self.err(where, f"'{key}': '{value}' is not a {engine} parameter")
                    elif not t.get("realtime"):
                        self.err(where, f"'{key}': {engine} parameter '{value}' is not realtime (cannot be modulated)")
            elif isinstance(value, str):
                if value not in d.get("choices", []):
                    self.err(where, f"'{key}': unknown choice '{value}' (valid: {d.get('choices', [])})")
            elif "range" in d and not d["range"][0] <= value <= d["range"][1]:
                self.warn(where, f"'{key}' = {value} outside {d['range'][0]}..{d['range'][1]} {d.get('unit', '')} (clamped)")
            elif "choices" in d and not 0 <= value < len(d["choices"]):
                self.err(where, f"'{key}': index {value} outside the {len(d['choices'])} choices")

    def patch(self, path, doc=None):
        """Checks a patch file, or an inline patch object (doc) found at path."""
        doc = load(path) if doc is None else doc
        self.schema(path, doc, self.patch_schema)
        if doc.get("engine") not in self.engines:
            self.err(path, f"unknown engine '{doc.get('engine')}' (valid: {sorted(self.engines)})")
            return None
        self.params(path, doc["engine"], patchtree.settings(doc))
        for k, m in enumerate(doc.get("macros", [])):
            for t in m.get("targets", []):
                if self.resolve(doc["engine"], t.get("param", "")) is None:
                    self.err(path, f"macro {k + 1}: target '{t.get('param')}' is not a {doc['engine']} parameter")
        return doc["engine"]

    def song(self, path):
        doc = load(path)
        self.schema(path, doc, self.song_schema)
        for i, t in enumerate(doc.get("tracks", [])):
            where = f"{path} track {i}"
            if isinstance(t.get("patch"), dict):  # inline patch
                engine = self.patch(f"{where} inline patch", t["patch"])
            else:
                pp = os.path.join(ROOT, t.get("patch", ""))
                if not os.path.isfile(pp):
                    self.err(where, f"patch file not found: {t.get('patch')}")
                    continue
                engine = self.patch(pp)
            if engine is None:
                continue
            self.params(where, engine, patchtree.flatten(t.get("params", {})))
            pat = t.get("pattern")
            if pat is not None:
                names = set(self.engines[engine].get("notes", {}))
                for k, sec in enumerate(pat if isinstance(pat, list) else [pat]):
                    lengths = set()
                    for row, cells in sec.items():
                        if row == "repeat":
                            continue
                        if row not in names and not NOTE_RE.match(row):
                            self.err(where, f"pattern section {k}: row '{row}' is neither a named note "
                                            f"of '{engine}' {sorted(names)} nor a note name")
                        lengths.add(len(cells.replace(" ", "").replace("|", "")))
                    if len(lengths) > 1:
                        self.err(where, f"pattern section {k}: rows have different lengths {sorted(lengths)}")


def main():
    ap = argparse.ArgumentParser(description="Validate patches and songs against the OID format and the catalog.")
    ap.add_argument("files", nargs="*", help="patches/songs (default: all in patches/ and songs/)")
    ap.add_argument("--catalog", default=os.path.join(ROOT, "docs", "catalog.json"))
    ap.add_argument("--catalog-cmd", help="run this synth_catalog binary (with --json) instead of reading --catalog")
    args = ap.parse_args()

    if args.catalog_cmd:
        catalog = json.loads(subprocess.run([args.catalog_cmd, "--json"], capture_output=True, text=True, check=True).stdout)
    else:
        catalog = load(args.catalog)
    c = Checker(catalog)
    c.schema("catalog", catalog, load(os.path.join(ROOT, "spec", "schemas", "catalog.schema.json")))

    files = args.files or sorted(glob.glob(os.path.join(ROOT, "patches", "*.json"))) + \
        sorted(glob.glob(os.path.join(ROOT, "songs", "*.json")))
    n_patch = n_song = 0
    for f in files:
        doc = load(f)
        if "tracks" in doc:
            c.song(f)
            n_song += 1
        else:
            c.patch(f)
            n_patch += 1
    c.roles(set(load(os.path.join(ROOT, "spec", "roles.json"))["roles"]))
    if not args.files:  # controller maps: every target must exist in at least one engine
        for path in sorted(glob.glob(os.path.join(ROOT, "controllers", "*.json"))):
            for cc, keys in load(path).get("map", {}).items():
                if not cc.isdigit() or int(cc) > 127:
                    c.err(path, f"'{cc}' is not a CC number")
                for key in keys if isinstance(keys, list) else [keys]:
                    if not any(c.resolve(e, key) for e in c.engines):
                        c.err(path, f"CC {cc}: '{key}' is not a parameter or role of any engine")

    for w in c.warnings:
        print("warning:", w)
    for e in c.errors:
        print("error:", e)
    print(f"{n_patch} patches, {n_song} songs: {len(c.errors)} error(s), {len(c.warnings)} warning(s)")
    sys.exit(1 if c.errors else 0)


if __name__ == "__main__":
    main()
