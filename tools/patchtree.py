"""patchtree.py — patches (version 2) group their settings by module: {"filter": {"cutoff": 800}, "fx": {"delay":
{"mix": 0.2}}}; a leaf's path is a parameter's role in docs/catalog.json. Helpers for the tools: the roles of an
engine, a setting read or written by its parameter name or its role, and a flat {name: value} set turned into the
tree (in the catalog's order: the engine's modules first, then the effects, the part and the modulation)."""
import json
import os

RESERVED = ("version", "engine", "name", "description", "kit", "macros")
_CAT = {}


def catalog(path=None):
    path = path or os.path.join(os.path.dirname(__file__), "..", "docs", "catalog.json")
    if path not in _CAT:
        _CAT[path] = json.load(open(path))
    return _CAT[path]


def tables(engine, cat=None):
    """the parameter tables an engine's patch can set, in order: the engine's, then global, part, modulation"""
    cat = cat or catalog()
    return [cat["engines"][engine]["params"]] + [cat[k]["params"] for k in ("global", "part", "modulation") if k in cat]


def role_of(engine, name, cat=None):
    """a parameter's role from its name (a role is returned as it is)"""
    if "." in name:
        return name
    for t in tables(engine, cat):
        if name in t:
            return t[name]["role"]
    raise KeyError(f"{engine}: no parameter {name}")


def name_of(engine, role, cat=None):
    for t in tables(engine, cat):
        for k, v in t.items():
            if v.get("role") == role:
                return k
    raise KeyError(f"{engine}: no parameter with role {role}")


def get(patch, key, default=None, cat=None):
    """a setting of a patch, by name or role"""
    node = patch
    for part in role_of(patch["engine"], key, cat).split("."):
        if not isinstance(node, dict) or part not in node:
            return default
        node = node[part]
    return node


def set(patch, key, value, cat=None):
    """sets a setting of a patch, by name or role (the modules on the way are made)"""
    parts = role_of(patch["engine"], key, cat).split(".")
    node = patch
    for part in parts[:-1]:
        node = node.setdefault(part, {})
    node[parts[-1]] = value


def flatten(node, path=""):
    """a module tree, flat: {role: value}"""
    out = {}
    for k, v in node.items():
        p = f"{path}.{k}" if path else k
        if isinstance(v, dict):
            out.update(flatten(v, p))
        else:
            out[p] = v
    return out


def settings(patch):
    """the settings of a patch, flat: {role: value} (its own keys left out)"""
    return flatten({k: v for k, v in patch.items() if k not in RESERVED})


def tree(engine, flat, cat=None):
    """{name or role: value} -> the module tree, in the catalog's order"""
    order = {}
    for t in tables(engine, cat):
        for k, v in t.items():
            order.setdefault(v["role"], len(order))
    roles = {role_of(engine, k, cat): v for k, v in flat.items()}
    out = {}
    for r in sorted(roles, key=lambda r: order.get(r, 1 << 30)):
        node, parts = out, r.split(".")
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = roles[r]
    return out


def dumps(obj, width=118):
    """JSON as the library writes it: 2-space indent, an object or array on one line when it fits in `width`"""
    def one(v):
        return json.dumps(v, ensure_ascii=False, separators=(", ", ": "))

    def fmt(v, ind, lead):  # lead: what comes before v on its line
        flat = one(v)
        if not isinstance(v, (dict, list)) or not v or len(flat) + lead <= width:
            return flat
        pad = " " * (ind + 2)
        if isinstance(v, dict):
            items = []
            for k, x in v.items():
                key = json.dumps(k, ensure_ascii=False) + ": "
                items.append(pad + key + fmt(x, ind + 2, ind + 2 + len(key)))
            return "{\n" + ",\n".join(items) + "\n" + " " * ind + "}"
        return "[\n" + ",\n".join(pad + fmt(x, ind + 2, ind + 2) for x in v) + "\n" + " " * ind + "]"
    return fmt(obj, 0, 0) + "\n"


def song_v2(song):
    """a song written with flat track settings ({"cutoff": 700}) -> version 2: each track's settings as its patch's
    module tree (the patch's engine, from its file or its inline object)"""
    song["version"] = 2
    root = os.path.join(os.path.dirname(__file__), "..")
    for t in song.get("tracks", []):
        if t.get("params"):
            pp = t["patch"]
            engine = pp["engine"] if isinstance(pp, dict) else json.load(open(os.path.join(root, pp)))["engine"]
            t["params"] = tree(engine, t["params"])
    return song
