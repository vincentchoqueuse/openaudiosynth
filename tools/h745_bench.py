#!/usr/bin/env python3
"""h745_bench.py — runs the on-board benchmark of the STM32H745I-DISCO through the ST-LINK and prints the DSP
load of every preset (average and worst buffer, % of the real-time budget of the M7). The firmware must be
running (`just flash`); the benchmark is started through the mailbox (target/h745/app/shared.h: bench_cmd),
plays the demo arpeggio for 1.5 s per preset with the audio interrupt stopped, then the board plays again.

    tools/h745_bench.py                     # presets as they are
    tools/h745_bench.py --fx-off            # chorus, delay and reverb off
    tools/h745_bench.py --os-1x             # nonlinear filters without oversampling
    tools/h745_bench.py --delay-off         # (or --reverb-off) one effect off
    tools/h745_bench.py --save out.json     # keep the results (to compare two firmwares: --compare a.json)
    tools/h745_bench.py --preset mono_d_lead --profile   # one preset, with the functions and lines that cost
"""
import argparse
import glob
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ELF = ROOT / "target/h745/CM7/build/h745_CM7.elf"
PRESETS = ROOT / "target/h745/app/presets_gen.c"
SHARED = 0x38000000
BENCH_CMD = SHARED + 0x0C  # offsets of shared_t (shared.h)
BENCH_PRESET = SHARED + 0x10
BENCH_ACK = SHARED + 0x44
CLI_GLOB = [
    "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI",
    "/opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
]


def cli():
    for pattern in CLI_GLOB:
        found = sorted(glob.glob(pattern))
        if found:
            return found[-1]
    sys.exit("STM32_Programmer_CLI not found: install STM32CubeProgrammer")


def run(args):
    out = subprocess.run([cli(), "-c", "port=SWD", "mode=HOTPLUG"] + args, capture_output=True, text=True).stdout
    return re.sub(r"\x1b\[[0-9;]*m", "", out)


def read_words(addr, count):
    words = []
    for line in run(["-r32", hex(addr), str(4 * count)]).splitlines():
        m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", line)
        if m:
            words += [int(w, 16) for w in m.group(1).split()]
    return words[:count]


def read_block(addr, count):
    """Large reads go through a file (-u): the memory display of the CLI is limited."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "dump.bin"
        run(["-u", hex(addr), str(4 * count), str(path)])
        data = path.read_bytes() if path.exists() else b""
    return list(struct.unpack(f"<{len(data) // 4}I", data[: len(data) // 4 * 4]))


def symbol(name):
    nm = sorted(glob.glob(str(Path.home() / "opt/arm-gnu-toolchain-*/bin/arm-none-eabi-nm")))
    out = subprocess.run([nm[-1] if nm else "arm-none-eabi-nm", str(ELF)], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit(f"{name} not in {ELF}: build the firmware first (just h745)")


def profile(top):
    """Reads the PC samples and prints where the time goes: by function (inlined ones on their own) and by line."""
    count = read_words(symbol("profile_count"), 1)[0]
    pcs = read_block(symbol("profile_pc"), count)
    addr2line = sorted(glob.glob(str(Path.home() / "opt/arm-gnu-toolchain-*/bin/arm-none-eabi-addr2line")))
    unique = sorted(set(pcs))
    out = subprocess.run([addr2line[-1], "-a", "-f", "-i", "-e", str(ELF)] + [hex(p) for p in unique],
                         capture_output=True, text=True).stdout.splitlines()
    where = {}  # pc -> (innermost function, its line)
    i = 0
    while i < len(out):
        pc = int(out[i], 16)
        func, line = out[i + 1], out[i + 2]
        where[pc] = (func, re.sub(r".*/(dsp|target)/", r"\1/", line.split(" ")[0]))
        i += 3
        while i < len(out) and not out[i].startswith("0x"):
            i += 2  # outer inline frames
    by_func, by_line = {}, {}
    for p in pcs:
        func, line = where.get(p, ("?", "?"))
        by_func[func] = by_func.get(func, 0) + 1
        by_line[line] = by_line.get(line, 0) + 1
    print(f"\nprofile: {count} samples")
    for title, table in (("function", by_func), ("line", by_line)):
        print(f"\n{'%':>6s}  {title}")
        for k, v in sorted(table.items(), key=lambda kv: -kv[1])[:top]:
            print(f"{100.0 * v / max(count, 1):6.1f}  {k}")


def as_float(w):
    return struct.unpack("<f", struct.pack("<I", w))[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fx-off", action="store_true")
    ap.add_argument("--os-1x", action="store_true")
    ap.add_argument("--delay-off", action="store_true")
    ap.add_argument("--reverb-off", action="store_true")
    ap.add_argument("--preset", help="measure this preset only")
    ap.add_argument("--profile", action="store_true", help="sample the PC (needs --preset)")
    ap.add_argument("--top", type=int, default=25, help="lines of the profile")
    ap.add_argument("--save")
    ap.add_argument("--compare", help="results of a previous run (--save) to compare with")
    a = ap.parse_args()

    names = re.findall(r'^\s+\{"([^"]+)", "([^"]+)"', PRESETS.read_text(), re.M)
    options = (1 if a.fx_off else 0) | (2 if a.os_1x else 0) | (4 if a.delay_off else 0) | (8 if a.reverb_off else 0)
    only = -1
    if a.preset:
        index = [n for n, _ in names].index(a.preset) if a.preset in [n for n, _ in names] else -1
        if index < 0:
            sys.exit(f"unknown preset {a.preset}")
        only = index
    if a.profile:
        if only < 0:
            sys.exit("--profile needs --preset")
        options |= 16
    run(["-w32", hex(BENCH_PRESET), hex(only & 0xFFFFFFFF)])
    seq = (read_words(BENCH_ACK, 1)[0] + 1) & 0xFFFFFF
    run(["-w32", hex(BENCH_CMD), hex((seq << 8) | options)])
    t0 = time.time()
    while read_words(BENCH_ACK, 1)[0] != seq:
        if time.time() - t0 > 600:
            sys.exit("the benchmark did not finish (is the firmware running?)")
        time.sleep(2)
    n = read_words(symbol("bench_count"), 1)[0]
    avg = [as_float(w) for w in read_words(symbol("bench_avg"), n)]
    peak = [as_float(w) for w in read_words(symbol("bench_peak"), n)]
    res = {f"{names[i][0]}": {"engine": names[i][1], "avg": round(avg[i], 2), "peak": round(peak[i], 2)}
           for i in range(n) if only < 0 or i == only}
    if a.profile:
        profile(a.top)

    ref = json.loads(Path(a.compare).read_text()) if a.compare else {}
    print(f"{'preset':32s} {'engine':8s} {'avg %':>7s} {'peak %':>7s}" + ("   avg before" if ref else ""))
    for name, r in sorted(res.items(), key=lambda kv: -kv[1]["avg"]):
        line = f"{name:32s} {r['engine']:8s} {r['avg']:7.1f} {r['peak']:7.1f}"
        if name in ref:
            line += f"   {ref[name]['avg']:7.1f} ({r['avg'] - ref[name]['avg']:+.1f})"
        print(line)
    over = [k for k, r in res.items() if r["peak"] >= 100.0]
    total = sum(r["avg"] for r in res.values()) / max(len(res), 1)
    print(f"\nmean load {total:.1f} %, {len(over)} presets above 100 % at their worst buffer")
    if a.save:
        Path(a.save).write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
