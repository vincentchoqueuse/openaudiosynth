#!/usr/bin/env python3
"""check_rules.py — mechanical check of the code rules of CLAUDE.md, so a human or an agent can verify a
change without rereading the whole code base (run by ctest as `rules`):
  dsp/ and midi/: only <stdint.h> and <stddef.h> (math.h only in table generators), no `double`, no libm,
  allocation, I/O or locking calls, no mutable file-scope variable outside the allowed list;
  every source file starts with a header comment; source is ASCII (English); every engine source file is
  registered, built, tested and has at least one patch; the web page's stylesheets have balanced braces (an
  unclosed rule silently swallows every rule after it).
"""
import os
import re
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTABLE = ("dsp", "midi")
ALLOWED_SYS = {"stdint.h", "stddef.h"}
MATH_OK = set()  # files allowed to include <math.h> (init-time table generators), relative paths
NO_PATCH_OK = {"sine"}  # reference engine of the test suite
GLOBALS_OK = {("dsp/synth.c", "g_synth"), ("dsp/synth.c", "g_fx_mem"), ("dsp/synth.c", "g_engine_mem"),
              ("dsp/synth.c", "g_engine_mem_built")}
FORBIDDEN = r"\b(malloc|calloc|realloc|free|printf|fprintf|sprintf|snprintf|puts|fopen|fread|fwrite|pthread_\w+|" \
            r"sinf?|cosf?|tanf?|expf?|exp2f?|logf?|log2f?|log10f?|powf?|sqrtf?|tanhf?|floorf?|ceilf?|fmodf?)\s*\("

errors = []


def err(path, line, msg):
    errors.append(f"{path}:{line}: {msg}")


def strip(code):
    """Removes comments and string/char literals, keeping line breaks (line numbers stay valid)."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        if code.startswith("/*", i):
            j = code.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * code.count("\n", i, j))
            i = j
        elif code.startswith("//", i):
            j = code.find("\n", i)
            i = n if j < 0 else j
        elif c in "\"'":
            j = i + 1
            while j < n and code[j] != c:
                j += 2 if code[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def sources(*dirs, ext=(".c", ".h")):
    for d in dirs:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for f in sorted(files):
                if f.endswith(ext):
                    p = os.path.join(base, f)
                    yield os.path.relpath(p, ROOT), open(p, encoding="utf-8", errors="replace").read()


for rel, code in sources("dsp", "midi", "host", "tests"):
    if not code.lstrip().startswith("/*"):
        err(rel, 1, "missing header comment (what the file does, and its reference)")
    for k, line in enumerate(code.splitlines(), 1):
        if any(ord(ch) > 127 and unicodedata.category(ch).startswith("L") for ch in line):
            err(rel, k, "non-ASCII letter (code and comments are in English)")
    if not rel.startswith(PORTABLE):
        continue
    bare = strip(code)
    lines = bare.splitlines()
    for k, line in enumerate(lines, 1):
        m = re.match(r"\s*#\s*include\s*<([^>]+)>", line)
        if m and m.group(1) not in ALLOWED_SYS and not (m.group(1) == "math.h" and rel in MATH_OK):
            err(rel, k, f"<{m.group(1)}> not allowed in {rel.split('/')[0]}/ (portable code: stdint.h, stddef.h)")
        if re.search(r"\bdouble\b", line):
            err(rel, k, "`double`: float only")
        m = re.search(FORBIDDEN, line)
        if m and not re.search(r"\b(fm|dsp|svf|osc|adsr|lfo)_\w*" + re.escape(m.group(1)) + r"\s*\(", line):
            err(rel, k, f"`{m.group(1)}()` not allowed in the audio code (libm per sample, allocation, I/O or lock)")
    if rel.endswith(".c"):
        depth = 0
        for k, line in enumerate(lines, 1):
            if depth == 0:
                m = re.match(r"\s*static\s+(?!const\b)(?!inline\b)[\w\s\*]*?\b(\w+)\s*(\[[^\]]*\])*\s*(=[^;]*)?;", line)
                if m and "(" not in line and (rel, m.group(1)) not in GLOBALS_OK:
                    err(rel, k, f"mutable file-scope variable `{m.group(1)}` (state belongs in caller-provided structs)")
            depth += line.count("{") - line.count("}")

for rel in sorted(os.listdir(os.path.join(ROOT, "tools"))):
    if rel.endswith(".py"):
        head = open(os.path.join(ROOT, "tools", rel)).read(400)
        if not re.match(r'(#!.*\n)?\s*("""|#)', head):
            err("tools/" + rel, 1, "missing header (docstring or comment)")

# engines: each source is registered, built, tested and has a patch
cmake = open(os.path.join(ROOT, "cmake/synth_sources.cmake")).read()  # the source list of every build
registry = open(os.path.join(ROOT, "dsp/engines/registry.c")).read()
tests = "".join(c for f, c in sources("tests") if os.path.basename(f).startswith("test_"))
patches = "".join(open(os.path.join(ROOT, "patches", f)).read() for f in os.listdir(os.path.join(ROOT, "patches")))
for f in sorted(os.listdir(os.path.join(ROOT, "dsp/engines"))):
    if not f.endswith(".c") or f == "registry.c":
        continue
    name = f[:-2]
    src = "dsp/engines/" + f
    if src not in cmake:
        err(src, 1, "not in cmake/synth_sources.cmake")
    if f"&{name}_engine" not in registry:
        err(src, 1, "not registered in dsp/engines/registry.c")
    if f"SYNTH_ENGINE_{name.upper()}" not in tests:
        err(src, 1, "no unit test uses this engine (tests/)")
    if f'"engine": "{name}"' not in patches and name not in NO_PATCH_OK:
        err(src, 1, "no patch uses this engine (patches/)")

# web stylesheets: braces balance outside comments and strings
web_src = os.path.join(ROOT, "web/ui/src")
for dirpath, _, files in os.walk(web_src):
    for f in files:
        if not f.endswith(".css"):
            continue
        path = os.path.join(dirpath, f)
        text = re.sub(r"/\*.*?\*/", "", open(path).read(), flags=re.S)
        text = re.sub(r"'[^'\n]*'|\"[^\"\n]*\"", "", text)
        depth, line = 0, 1
        for c in text:
            line += c == "\n"
            depth += (c == "{") - (c == "}")
            if depth < 0:
                break
        if depth:
            err(os.path.relpath(path, ROOT), line, f"unbalanced braces ({depth:+d})")

for e in errors:
    print(e)
print(f"rules: {len(errors)} problem(s)")
sys.exit(1 if errors else 0)
