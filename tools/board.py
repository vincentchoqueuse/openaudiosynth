#!/usr/bin/env python3
"""board.py — a small Python client of the board link (docs/link.md): find the boards on the Ethernet cable,
pick a preset, play notes, phrases and knob moves, and run a quick test of every preset. The host is the
sequencer: notes are sent as they are due (with the heartbeat the boards expect while notes are held), so a
pattern can change while it plays and several boards share one clock.

    tools/board.py --iface en0 discover
    tools/board.py --iface en0 select modal_bowl_bowed
    tools/board.py --iface en0 note B5 --dur 3
    tools/board.py --iface en0 play "C4 E4 G4 C5 _ _ - -" --bpm 120 --loop 4
    tools/board.py --iface en0 play "A2 _ A2 C3" --sweep 13:0:127        # knob 1 (CC 13) up over the phrase
    tools/board.py --iface en0 audition va_acid_bass                        # a sound's demo clip, on its preset
    tools/board.py --iface en0 cc 14 100
    tools/board.py --iface en0 test --seconds 2                          # every preset: plays, then the load
    tools/board.py --iface en0 panic

Phrases use the notation of the patches' auditions: notes (C4, Eb2, F#4), chords (C3+E3+G3), accent (C4!) or
ghost (C4?), '_' holds the previous step, '-' is a rest. --iface sends the broadcasts out of that interface (on a
Mac whose Wi-Fi is the default route) to its subnet's broadcast address, which the boards accept whatever
their own link-local address is.

As a module:
    from board import Link
    link = Link(iface="en0")
    print(link.discover())
    link.select("mono_d_lead")
    link.play("C3 E3 G3 C4", bpm=120)
"""
import argparse
import json
import random
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = 47100
HEARTBEAT_S = 0.1  # LINK_HEARTBEAT_MS: a host that plays sends something at least this often
DISCOVER, HELLO, ACK, HEARTBEAT, MIDI, SELECT = 0x01, 0x02, 0x03, 0x04, 0x10, 0x12
STATUS = {0: "ok", 1: "busy", 2: "malformed", 3: "catalog", 4: "unknown preset", 5: "too big", 6: "corrupted",
          7: "pending"}
IP_BOUND_IF = 25  # macOS: send out of this interface


def note_number(token):
    """'C4' -> 60, 'Eb2', 'F#4'; a number is taken as is."""
    if token.isdigit():
        return int(token)
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d+)", token)
    if not m:
        raise ValueError(f"bad note {token!r} (expected e.g. C4, Eb2, F#4)")
    n = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1).upper()]
    n += {"#": 1, "b": -1, "": 0}[m.group(2)]
    n += 12 * (int(m.group(3)) + 1)
    if not 0 <= n <= 127:
        raise ValueError(f"note {token!r} out of range")
    return n


def phrase_events(text, bpm=120.0, step=0.25, velocity=100, gate=0.9, offset=0.0):
    """A phrase -> (events, length): events are (time s, MIDI bytes), note-ons and note-offs, sorted."""
    step_s = 60.0 / bpm * step
    events, t, held = [], 0.0, []  # held: [note, velocity, start, length] of the current step
    def flush():
        for n, v, start, length in held:
            events.append((offset + start, bytes([0x90, n, v])))
            events.append((offset + start + length - step_s * (1.0 - gate), bytes([0x80, n, 0])))
        held.clear()
    for tok in text.replace("|", " ").split():
        if tok == "_":
            for h in held:
                h[3] += step_s
            t += step_s
            continue
        flush()
        if tok != "-":
            for member in tok.split("+"):
                v = velocity
                while member and member[-1] in "!?~":
                    v = 127 if member[-1] == "!" else 50 if member[-1] == "?" else v
                    member = member[:-1]
                held.append([note_number(member), v, t, step_s])
        t += step_s
    flush()
    events.sort(key=lambda e: (e[0], e[1][0] == 0x90))  # at the same time, note-offs first
    return events, t


def iface_broadcast(iface):
    """The broadcast address of an interface (ifconfig), or None."""
    try:
        out = subprocess.run(["ifconfig", iface], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    m = re.search(r"broadcast (\d+\.\d+\.\d+\.\d+)", out)
    return m.group(1) if m else None


class Link:
    def __init__(self, iface=None, to=None, board=0, timeout=0.6):
        self.board, self.timeout, self.seq = board, timeout, random.randrange(1 << 30)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if iface:
            if sys.platform == "darwin":
                self.sock.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF, socket.if_nametoindex(iface))
            elif hasattr(socket, "SO_BINDTODEVICE"):
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, iface.encode())
        self.sock.bind(("0.0.0.0", 0))
        self.to = to or (iface_broadcast(iface) if iface else None) or "255.255.255.255"
        self.last_send = 0.0

    # ---- datagrams ----
    def _send(self, kind, payload=b""):
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        self.sock.sendto(b"OL" + bytes([1, kind]) + struct.pack("<II", self.board, self.seq) + payload,
                         (self.to, PORT))
        self.last_send = time.perf_counter()
        return self.seq

    def _replies(self, kind, wait):
        """(board id, payload) of the datagrams of this type received within wait seconds."""
        end, out = time.perf_counter() + wait, []
        while (left := end - time.perf_counter()) > 0:
            self.sock.settimeout(left)
            try:
                d, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                break
            if len(d) >= 12 and d[:3] == b"OL\x01" and d[3] == kind:
                out.append((struct.unpack_from("<I", d, 4)[0], d[12:]))
        return out

    # ---- commands ----
    def discover(self, wait=None):
        """Every board that answers: a list of dicts (id, ip, preset playing, load...)."""
        self._send(DISCOVER)
        boards = []
        for bid, p in self._replies(HELLO, wait or self.timeout):
            if len(p) < 86:
                continue
            _id, ip, catalog, preset, count, load, peak, overruns, kit = struct.unpack_from("<I12xIIhHBBII", p)
            boards.append({"id": f"{bid:08x}", "ip": socket.inet_ntoa(struct.pack(">I", ip)),
                           "catalog": f"{catalog:08x}", "preset": preset, "presets": count, "load": load,
                           "peak": peak, "overruns": overruns, "kit": kit,
                           "board": p[38:54].split(b"\0")[0].decode(errors="replace"),
                           "playing": p[54:86].split(b"\0")[0].decode(errors="replace")})
        return boards

    def select(self, name, tries=20):
        """Plays the built-in preset of this name; returns the status ('ok', 'unknown preset'...)."""
        payload = name.encode()[:31].ljust(32, b"\0")
        for _ in range(tries):
            seq = self._send(SELECT, payload)
            acks = [p for _, p in self._replies(ACK, self.timeout) if struct.unpack_from("<I", p)[0] == seq]
            if not acks:
                continue
            status = STATUS.get(acks[0][4], "?")
            if status != "busy":
                return status
            time.sleep(0.05)
        return "no answer"

    def midi(self, *messages):
        """MIDI messages (bytes or lists), back to back in one datagram."""
        self._send(MIDI, b"".join(bytes(m) for m in messages))

    def heartbeat(self):
        self._send(HEARTBEAT)

    def note_on(self, note, velocity=100, channel=0):
        self.midi([0x90 | channel, note_number(str(note)), velocity])

    def note_off(self, note, channel=0):
        self.midi([0x80 | channel, note_number(str(note)), 0])

    def cc(self, number, value, channel=0):
        self.midi([0xB0 | channel, number & 127, max(0, min(127, int(value)))])

    def panic(self):
        """All notes off and sustain off on the 16 channels."""
        self.midi(*([0xB0 | c, 123, 0] for c in range(16)))
        self.midi(*([0xB0 | c, 64, 0] for c in range(16)))

    def hold(self, seconds):
        """Waits, sending the heartbeat that keeps held notes sounding."""
        end = time.perf_counter() + seconds
        while (now := time.perf_counter()) < end:
            if now - self.last_send >= HEARTBEAT_S:
                self.heartbeat()
            time.sleep(min(0.02, max(0.0, end - now)))

    def run(self, events, length, sweep=None):
        """Sends timed events (time s, MIDI bytes), events at the same time in one datagram, with the heartbeat
        in between; sweep = (cc, from, to) moves a knob over the length."""
        t0, i, last_cc = time.perf_counter(), 0, None
        while True:
            now = time.perf_counter() - t0
            batch = []
            while i < len(events) and events[i][0] <= now:
                batch.append(events[i][1])
                i += 1
            if sweep:
                c, a, b = sweep
                v = int(round(a + (b - a) * min(now / length, 1.0))) if length > 0 else b
                if v != last_cc:
                    batch.append(bytes([0xB0, c & 127, max(0, min(127, v))]))
                    last_cc = v
            if batch:
                self.midi(*batch)
            elif time.perf_counter() - self.last_send >= HEARTBEAT_S and i < len(events):
                self.heartbeat()
            if i >= len(events) and now >= length:
                return
            nxt = events[i][0] if i < len(events) else length
            time.sleep(max(0.0, min(nxt - now, 0.01 if sweep else HEARTBEAT_S / 2)))

    def play(self, text, bpm=120.0, step=0.25, loop=1, velocity=100, gate=0.9, sweep=None):
        self.play_events(*phrase_events(text, bpm, step, velocity, gate), loop=loop, sweep=sweep)

    def play_events(self, events, length, loop=1, sweep=None):
        all_events = [(t + k * length, m) for k in range(loop) for t, m in events]
        self.run(all_events, length * loop, sweep)


def board_presets():
    """(name, engine) of the board's built-in presets, from the generated table."""
    src = (ROOT / "target/h745/app/presets_gen.c").read_text()
    return re.findall(r'^\s*\{"([a-z0-9_]+)", "([a-z0-9]+)", "', src, re.M)


def clip_events(name, bpm=None):
    """A sound's demo clip (clips/NAME.json, the web sequencer's clip) -> (events, length s); None if it has none."""
    path = ROOT / "clips" / f"{name}.json"
    if not path.exists():
        return None
    c = json.loads(path.read_text())
    spb = 60.0 / (bpm or float(c.get("bpm", 120)))
    events = []
    for n in c["notes"]:
        if n["start"] >= c["length"]:
            continue
        events.append((n["start"] * spb, bytes([0x90, n["pitch"], n.get("velocity", 100)])))
        events.append((n["start"] * spb + max(n["length"] * spb - 0.002, 0.001), bytes([0x80, n["pitch"], 0])))
    events.sort(key=lambda e: (e[0], e[1][0] == 0x90))  # at the same time, note-offs first
    return events, c["length"] * spb


def demo_events(name):
    """its clip, or a C major arpeggio"""
    return clip_events(name) or phrase_events("C3 E3 G3 C4", 120.0, 0.25)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--iface", help="network interface the board is cabled to (ifconfig), e.g. en0")
    ap.add_argument("--to", help="destination address (default: the interface's broadcast)")
    ap.add_argument("--board", default="0", help="board id (hex, from discover); 0 = every board")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("discover")
    s = sub.add_parser("select"); s.add_argument("name")
    s = sub.add_parser("note"); s.add_argument("note"); s.add_argument("--vel", type=int, default=100)
    s.add_argument("--dur", type=float, default=1.0)
    for name in ("play", "audition"):
        s = sub.add_parser(name)
        s.add_argument("phrase" if name == "play" else "patch")
        s.add_argument("--bpm", type=float); s.add_argument("--step", type=float)
        s.add_argument("--loop", type=int, default=1); s.add_argument("--vel", type=int, default=100)
        s.add_argument("--sweep", help="CC:FROM:TO, a knob moved over the whole phrase (e.g. 13:0:127)")
    s = sub.add_parser("cc"); s.add_argument("number", type=int); s.add_argument("value", type=int)
    s = sub.add_parser("test"); s.add_argument("--seconds", type=float, default=2.0)
    s.add_argument("--only", help="presets whose name contains this")
    sub.add_parser("panic")
    a = ap.parse_args()

    link = Link(a.iface, a.to, int(a.board, 16))
    if a.cmd == "discover":
        boards = link.discover()
        for b in boards:
            print(f"{b['id']}  {b['board']}  {b['ip']:15s}  {b['playing'] or '-':22s}  load {b['load']}% "
                  f"(peak {b['peak']}%)  overruns {b['overruns']}  {b['presets']} presets  catalog {b['catalog']}")
        if not boards:
            sys.exit("no board answered (cable? --iface?)")
    elif a.cmd == "select":
        status = link.select(a.name)
        print(status)
        sys.exit(status != "ok")
    elif a.cmd == "note":
        link.note_on(a.note, a.vel)
        link.hold(a.dur)
        link.note_off(a.note)
    elif a.cmd in ("play", "audition"):
        sweep = tuple(int(x) for x in a.sweep.split(":")) if a.sweep else None
        if a.cmd == "audition":
            events, length = clip_events(a.patch, a.bpm) or phrase_events("C3 E3 G3 C4", a.bpm or 120.0, 0.25)
            status = link.select(a.patch)
            if status != "ok":
                sys.exit(f"{a.patch}: {status}")
            time.sleep(0.1)
        else:
            events, length = phrase_events(a.phrase, a.bpm or 120.0, a.step or 0.25, a.vel)
        try:
            link.play_events(events, length, a.loop, sweep=sweep)
        except KeyboardInterrupt:
            link.panic()
    elif a.cmd == "cc":
        link.cc(a.number, a.value)
    elif a.cmd == "panic":
        link.panic()
    elif a.cmd == "test":
        rows, bad = [], 0
        for name, engine in board_presets():
            if a.only and a.only not in name:
                continue
            status = link.select(name)
            if status != "ok":
                print(f"{name:24s} {engine:8s} {status}")
                bad += 1
                continue
            time.sleep(0.1)
            events, length = demo_events(name)
            events = [e for e in events if e[0] < a.seconds]
            link.run(events + [(a.seconds, bytes([0xB0, 123, 0]))], a.seconds)
            b = link.discover()
            info = next((x for x in b if link.board in (0, int(x["id"], 16))), None)
            if not info or info["playing"] != name:
                print(f"{name:24s} {engine:8s} not playing ({info['playing'] if info else 'no answer'})")
                bad += 1
                continue
            rows.append((name, engine, info["load"], info["peak"], info["overruns"]))
            print(f"{name:24s} {engine:8s} load {info['load']:3d}%  (worst since start {info['peak']:3d}%)  "
                  f"overruns {info['overruns']}")
        link.panic()
        print(f"{len(rows)} presets played, {bad} failed")
        sys.exit(bad != 0)


if __name__ == "__main__":
    main()
