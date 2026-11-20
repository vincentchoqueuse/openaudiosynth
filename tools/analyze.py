#!/usr/bin/env python3
# analyze.py — perceptually oriented measurements of a rendered sound, so a patch can be judged and
# corrected without listening: level, pitch, envelope (attack/decay/sustain/release), brightness
# (spectral centroid over time), noisiness, band balance, movement (LFO/detune beating), stereo width.
# With --ref, also compares against a reference WAV (log-mel spectral distance + metric deltas).
#
#   python3 tools/analyze.py out.wav                 human-readable summary
#   python3 tools/analyze.py out.wav --json          machine-readable
#   python3 tools/analyze.py out.wav --png           + spectrogram PNG in build-host/plots/
#   python3 tools/analyze.py out.wav --ref target.wav
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
BANDS = [(20, 120, "sub"), (120, 500, "low"), (500, 2000, "mid"), (2000, 6000, "presence"), (6000, 20000, "air")]


def load(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    return sr, x[:, 0], x[:, 1]


def db(v, floor=1e-12):
    return 20.0 * math.log10(max(float(v), floor))


def rms_envelope(m, sr, win_s=0.005):
    hop = max(1, int(sr * win_s))
    n = len(m) // hop
    frames = m[: n * hop].reshape(n, hop)
    return np.sqrt((frames ** 2).mean(axis=1) + 1e-20), hop / sr


def envelope_metrics(env, dt):
    """Measured on the FIRST note: render a single held note (see the skill) for exact ADSR figures."""
    gpeak = env.max()
    if gpeak < 1e-6:
        return {}
    on = np.nonzero(env > 0.05 * gpeak)[0]
    start = int(on[0])
    # first note: local peak within 1.5 s after the onset
    win = env[start: start + int(1.5 / dt)]
    p_rel = int(np.argmax(win))
    peak = win[p_rel]
    peak_i = start + p_rel
    # attack: from 10 % to 90 % of that peak
    a10 = start + int(np.argmax(env[start:] >= 0.1 * peak))
    a90 = start + int(np.argmax(env[start:] >= 0.9 * peak))
    attack = max(0, a90 - a10) * dt
    # decay: from the peak until the level first drops 20 dB below it (None if it never does)
    below = np.nonzero(env[peak_i:] < peak * 0.1)[0]
    decay20 = float(below[0] * dt) if len(below) else None
    # sustain: median level 0.3..0.6 s after the peak, relative to the peak
    sus = env[peak_i + int(0.3 / dt): peak_i + int(0.6 / dt)]
    sustain = float(np.median(sus) / peak) if len(sus) else 0.0
    # release tail of the whole render: last frame above -20 dB to last above -60 dB (of the global peak)
    last20 = np.nonzero(env > 0.1 * gpeak)[0][-1]
    last60 = np.nonzero(env > 0.001 * gpeak)[0][-1]
    return {
        "onset_s": round(start * dt, 4),
        "attack_ms": round(attack * 1000, 1),
        "decay_to_-20dB_s": None if decay20 is None else round(decay20, 3),
        "sustain_ratio": round(sustain, 3),
        "tail_-20_to_-60dB_s": round((last60 - last20) * dt, 3),
        "sounding_s": round((on[-1] - start) * dt, 3),
    }


def pitch_metrics(m, sr, env, dt):
    """f0 by normalized autocorrelation on 200 ms around the first note's peak; harmonicity 0 (noise)..1."""
    n = int(0.2 * sr)
    if len(m) < n:
        return {}
    on = np.nonzero(env > 0.05 * env.max())[0]
    first = int(on[0]) if len(on) else 0
    centre = int((first + int(np.argmax(env[first: first + int(1.0 / dt)]))) * dt * sr)
    a = max(0, min(len(m) - n, centre))
    seg = m[a: a + n] * np.hanning(n)
    seg = seg - seg.mean()
    ac = signal.fftconvolve(seg, seg[::-1], mode="full")[n - 1:]
    if ac[0] <= 0:
        return {}
    ac = ac / ac[0]
    lo, hi = int(sr / 4000), int(sr / 25)
    neg = np.nonzero(ac[:hi] < 0)[0]  # skip the main lobe around lag 0
    if len(neg):
        lo = max(lo, int(neg[0]))
    k = lo + int(np.argmax(ac[lo:hi]))
    harmonicity = float(max(0.0, ac[k]))
    if 1 <= k < len(ac) - 1:  # parabolic refinement
        y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
        den = y0 - 2 * y1 + y2
        d = 0.5 * (y0 - y2) / den if den < 0 else 0.0  # only refine a true maximum
        lag = k + max(-0.5, min(0.5, d))
    else:
        lag = k
    f0 = sr / lag
    midi = 69 + 12 * math.log2(f0 / 440.0)
    near = int(round(midi))
    return {
        "f0_hz": round(f0, 2),
        "note": f"{NOTE_NAMES[near % 12]}{near // 12 - 1}",
        "cents_off": round(100 * (midi - near), 1),
        "harmonicity": round(harmonicity, 3),
    }


def spectral_metrics(m, sr):
    f, t, S = signal.stft(m, sr, window="hann", nperseg=2048, noverlap=1536)
    P = np.abs(S) ** 2
    energy = P.sum(axis=0)
    loud = energy > energy.max() * 1e-4
    if not loud.any():
        return {}, None
    cen = (P * f[:, None]).sum(axis=0) / (energy + 1e-30)
    flat = np.exp(np.mean(np.log(P + 1e-20), axis=0)) / (P.mean(axis=0) + 1e-20)
    cs = cen[loud]
    third = max(1, len(cs) // 3)
    cum = np.cumsum(P[:, loud].sum(axis=1))
    rolloff = float(f[np.searchsorted(cum, 0.85 * cum[-1])])
    tot = P[:, loud].sum()
    bands = {}
    for lo, hi, name in BANDS:
        sel = (f >= lo) & (f < hi)
        bands[name] = round(100.0 * P[sel][:, loud].sum() / tot, 1)
    return {
        "centroid_hz_mean": round(float(np.average(cs, weights=energy[loud])), 0),
        "centroid_hz_start_mid_end": [round(float(np.median(cs[:third])), 0),
                                      round(float(np.median(cs[third:2 * third])), 0),
                                      round(float(np.median(cs[2 * third:])), 0)],
        "rolloff85_hz": round(rolloff, 0),
        "noisiness": round(float(np.average(flat[loud], weights=energy[loud])), 3),
        "band_energy_pct": bands,
    }, (f, P, loud)


def movement_metrics(env, dt):
    """Dominant amplitude-modulation rate (tremolo, detune beating, LFO) in the sustained part."""
    on = np.nonzero(env > 0.1 * env.max())[0]
    if len(on) < int(1.0 / dt):
        return {"am_rate_hz": None, "am_depth_db": 0.0}
    seg = np.log(env[on[0]: on[-1]] + 1e-9)
    seg = signal.detrend(seg)
    F = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    fr = np.fft.rfftfreq(len(seg), dt)
    sel = (fr >= 0.3) & (fr <= 20.0)
    if not sel.any():
        return {"am_rate_hz": None, "am_depth_db": 0.0}
    k = np.argmax(F * sel)
    depth = 20 / math.log(10) * (seg.max() - seg.min()) / 2
    return {"am_rate_hz": round(float(fr[k]), 2), "am_depth_db": round(float(depth), 1)}


def stereo_metrics(L, R):
    if np.allclose(L, R):
        return {"lr_correlation": 1.0, "width": 0.0}
    c = float(np.corrcoef(L, R)[0, 1])
    s = (L - R) / 2
    mm = (L + R) / 2
    return {"lr_correlation": round(c, 3), "width": round(float(np.sqrt((s ** 2).mean() / ((mm ** 2).mean() + 1e-20))), 3)}


def logmel(m, sr, n_mels=48):
    f, t, S = signal.stft(m, sr, nperseg=2048, noverlap=1536)
    P = np.abs(S) ** 2
    mel = lambda hz: 2595 * np.log10(1 + hz / 700)
    edges = 700 * (10 ** (np.linspace(mel(30), mel(min(16000, sr / 2)), n_mels + 2) / 2595) - 1)
    fb = np.zeros((n_mels, len(f)))
    for i in range(n_mels):
        l, c, r = edges[i], edges[i + 1], edges[i + 2]
        fb[i] = np.clip(np.minimum((f - l) / (c - l), (r - f) / (r - c)), 0, None)
    return 10 * np.log10(fb @ P + 1e-10)


def analyze(path):
    sr, L, R = load(path)
    m = 0.5 * (L + R)
    peak = float(np.max(np.abs(np.concatenate([L, R]))))
    env, dt = rms_envelope(m, sr)
    out = {
        "file": path,
        "duration_s": round(len(m) / sr, 3),
        "level": {
            "peak_dbfs": round(db(peak), 1),
            "rms_dbfs": round(db(np.sqrt((m ** 2).mean())), 1),
            "crest_db": round(db(peak) - db(np.sqrt((m ** 2).mean())), 1),
            "clipped_samples": int(np.sum(np.abs(np.concatenate([L, R])) >= 0.999)),
            "dc_offset": round(float(m.mean()), 5),
        },
        "envelope": envelope_metrics(env, dt),
        "pitch": pitch_metrics(m, sr, env, dt),
    }
    spec, _ = spectral_metrics(m, sr)
    out["spectrum"] = spec
    out["movement"] = movement_metrics(env, dt)
    out["stereo"] = stereo_metrics(L, R)
    return out, (sr, m)


def compare(a, b, sig_a, sig_b):
    sr_a, ma = sig_a
    sr_b, mb = sig_b
    A, B = logmel(ma, sr_a), logmel(mb, sr_b)
    n = min(A.shape[1], B.shape[1])
    A, B = A[:, :n], B[:, :n]
    # level-independent: remove each one's mean
    dist = float(np.mean(np.abs((A - A.mean()) - (B - B.mean()))))
    spec_a = A.mean(axis=1) - A.mean()
    spec_b = B.mean(axis=1) - B.mean()

    def g(d, *keys):
        for k in keys:
            d = d.get(k) if isinstance(d, dict) else None
        return d

    deltas = {}
    for keys in [("spectrum", "centroid_hz_mean"), ("spectrum", "noisiness"), ("envelope", "attack_ms"),
                 ("envelope", "decay_to_-20dB_s"), ("envelope", "sustain_ratio"), ("level", "rms_dbfs"),
                 ("pitch", "f0_hz"), ("movement", "am_rate_hz")]:
        va, vb = g(a, *keys), g(b, *keys)
        if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
            deltas[".".join(keys)] = {"this": va, "ref": vb, "diff": round(va - vb, 3)}
    return {
        "logmel_distance_db": round(dist, 2),
        "average_spectrum_diff_db_low_to_high": [round(float(np.mean(c)), 1)
                                                  for c in np.array_split(spec_a - spec_b, 6)],
        "deltas": deltas,
    }


def print_human(r):
    lv, en, pi, sp, mv, st = (r.get(k, {}) for k in ("level", "envelope", "pitch", "spectrum", "movement", "stereo"))
    print(f"{r['file']}  ({r['duration_s']} s)")
    print(f"  level    peak {lv['peak_dbfs']} dBFS, rms {lv['rms_dbfs']} dBFS, crest {lv['crest_db']} dB,"
          f" clipped {lv['clipped_samples']}, dc {lv['dc_offset']}")
    if en:
        print(f"  envelope attack {en['attack_ms']} ms, -20 dB after {en['decay_to_-20dB_s']} s,"
              f" sustain {en['sustain_ratio']} x peak, tail {en['tail_-20_to_-60dB_s']} s, sounding {en['sounding_s']} s")
    if pi:
        print(f"  pitch    {pi['f0_hz']} Hz = {pi['note']} {pi['cents_off']:+} cents, harmonicity {pi['harmonicity']}")
    if sp:
        print(f"  spectrum centroid {sp['centroid_hz_mean']} Hz (start/mid/end {sp['centroid_hz_start_mid_end']}),"
              f" rolloff85 {sp['rolloff85_hz']} Hz, noisiness {sp['noisiness']}")
        print("  bands %   " + ", ".join(f"{k} {v}" for k, v in sp["band_energy_pct"].items()))
    print(f"  movement AM {mv['am_rate_hz']} Hz, depth {mv['am_depth_db']} dB;"
          f"  stereo corr {st['lr_correlation']}, width {st['width']}")
    if "compare" in r:
        c = r["compare"]
        print(f"  vs ref   log-mel distance {c['logmel_distance_db']} dB (0 = identical timbre;"
              f" < 3 close, > 8 different)")
        print(f"           spectrum diff low->high (dB): {c['average_spectrum_diff_db_low_to_high']}")
        for k, d in c["deltas"].items():
            print(f"           {k}: {d['this']} vs {d['ref']} ({d['diff']:+})")


def main():
    ap = argparse.ArgumentParser(description="Analyze a rendered sound.")
    ap.add_argument("wav")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--png", action="store_true", help="also write a spectrogram PNG to build-host/plots/")
    ap.add_argument("--ref", help="reference WAV to compare against")
    args = ap.parse_args()
    r, sig = analyze(args.wav)
    if args.ref:
        rr, sig_r = analyze(args.ref)
        r["compare"] = compare(r, rr, sig, sig_r)
    if args.png:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from plot_spectrum import plot
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            plot(args.wav, "build-host/plots")
        r["png"] = os.path.join("build-host/plots", os.path.splitext(os.path.basename(args.wav))[0] + ".png")
    if args.json:
        print(json.dumps(r, indent=2))
    else:
        print_human(r)
        if "png" in r:
            print(f"  png      {r['png']}")


if __name__ == "__main__":
    main()
