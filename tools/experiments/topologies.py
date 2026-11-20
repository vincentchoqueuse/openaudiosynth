#!/usr/bin/env python3
"""topologies.py — experiment: how the structure of a network of coupled Stuart-Landau partials shapes the
spectrum. Same integrator for every structure (control rate 4 kHz, audio by phase integration):

    r_k'   = r_k (mu - r_k^2) + Ka * sum_j W_kj r_j cos(d_kj + lag)  [- Ka deg_k r_k if diffusive]
    phi_k' = w_k(t) (1 - beta (r_k^2 - mu)) - (B r^2)_k + Kp * nu_k sum_j W_kj r_j / (r_k + 0.4) sin(d_kj + lag)
    d_kj   = phi_j / nu_j - phi_k / nu_k      (phases brought to a common reference by the ratios nu)

Structures (W, frequencies, ratios, signs): chain, twisted ring, global sync, inhibition, two carriers, tree
cascade, cross-FM, small world, diffusive freeze. Writes build-host/renders/exp_net_<name>.wav, a gallery of
spectrograms (build-host/plots/exp_net.png), long-term spectra, and a table of spectral descriptors.
Usage: python3 tools/experiments/topologies.py [--seconds 20] [--only name1,name2]
"""
import argparse
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SR, CTRL = 48000, 4000


def neighbours(n, reach=1, ring=False):
    W = np.zeros((n, n))
    for k in range(n):
        for d in range(1, reach + 1):
            for j in (k - d, k + d):
                if ring:
                    W[k, j % n] = 1.0
                elif 0 <= j < n:
                    W[k, j] = 1.0
    return W


def structure(name, n, rng, seconds):
    """Returns a dict: f (Hz), nu (phase ratios), W, Ka, Kp, beta, lag, diffusive, B (cross-FM), K_ramp, drivers."""
    s = dict(Ka=0.0, Kp=0.0, beta=0.0, lag=0.0, diffusive=False, B=None, ramp=None, fixed=None, wander=0.0)
    if name == "chain":
        h = np.arange(1, n + 1.0)
        s.update(f=110 * h * 2 ** (rng.normal(0, 15, n) / 1200), nu=h, W=neighbours(n), Ka=0.8, Kp=0.35, beta=0.05)
    elif name == "twisted_ring":  # a dense band, ring coupling with a phase lag: travelling waves
        f = np.sort(rng.uniform(440, 560, n))
        s.update(f=f, nu=np.ones(n), W=neighbours(n, 2, ring=True), Kp=90.0, Ka=0.6, lag=1.3, beta=0.01)
    elif name == "global_sync":  # a choir of random pitches condensing into unison as the coupling grows
        f = 220 * 2 ** (rng.uniform(-0.25, 0.25, n))
        s.update(f=f, nu=np.ones(n), W=np.ones((n, n)) - np.eye(n), Kp=24.0, Ka=0.05, ramp=(0.0, 1.0))
    elif name == "inhibition":  # all-to-all negative modulus coupling: competition, few winners at a time
        f = 110 * np.arange(1, n + 1.0) * 2 ** (rng.normal(0, 30, n) / 1200)
        s.update(f=f, nu=np.arange(1, n + 1.0), W=np.ones((n, n)) - np.eye(n), Ka=-1.6, Kp=0.2, beta=0.03,
                 wander=25.0)
    elif name == "two_carriers":  # partials pulled by A2 and by E-flat 3 (a tritone): they switch sides
        f = np.exp(rng.uniform(np.log(150), np.log(2000), n))
        f[0], f[1] = 110.0, 155.56
        s.update(f=f, nu=None, W=None, Ka=0.8, Kp=10.0, beta=0.02, fixed=[0, 1], wander=35.0)
    elif name == "tree_cascade":  # 55 Hz -> 4 children at harmonics -> 4 grandchildren each at their harmonics
        f, parent, ratio = [55.0], [-1], [1.0]
        for c in range(4):
            m = rng.integers(2, 8)
            f.append(55.0 * m * 2 ** (rng.normal(0, 20) / 1200)), parent.append(0), ratio.append(float(m))
            ci = len(f) - 1
            for g in range(4):
                m2 = rng.integers(2, 6)
                f.append(f[ci] * m2 * 2 ** (rng.normal(0, 25) / 1200)), parent.append(ci), ratio.append(ratio[ci] * m2)
        f = np.array(f)
        W = np.zeros((len(f), len(f)))
        for k, p in enumerate(parent):
            if p >= 0:
                W[k, p] = 1.0  # children listen to their parent only
        s.update(f=f, nu=np.array(ratio), W=W, Ka=0.6, Kp=3.0, beta=0.06, fixed=[0])
    elif name == "cross_fm":  # each partial's pitch is bent by the modulus of two others: chaotic glissandi
        f = np.exp(rng.uniform(np.log(120), np.log(1800), n))
        B = np.zeros((n, n))
        for k in range(n):
            for j in rng.choice(n, 2, replace=False):
                B[k, j] = 2 * np.pi * f[k] * rng.choice([-1, 1]) * 0.08
        W = (rng.uniform(0, 1, (n, n)) < 0.15) * rng.choice([-1.0, 1.0], (n, n))
        np.fill_diagonal(W, 0.0)
        s.update(f=f, nu=f / 110.0, W=W, Ka=0.9, Kp=0.5, B=B)
    elif name == "small_world":  # ring + random shortcuts: chimera (part locked, part free)
        h = np.arange(1, n + 1.0)
        W = neighbours(n, 1, ring=True)
        for _ in range(n // 3):
            a, b = rng.choice(n, 2, replace=False)
            W[a, b] = W[b, a] = 1.0
        s.update(f=82.4 * h * 2 ** (rng.normal(0, 25, n) / 1200), nu=h, W=W, Ka=0.5, Kp=0.9, beta=0.04)
    elif name == "freeze":  # diffusive coupling of detuned partials: amplitude death, a frozen sparse spectrum
        h = np.arange(1, n + 1.0)
        s.update(f=110 * h * 2 ** (rng.normal(0, 15, n) / 1200), nu=h, W=neighbours(n), Ka=2.0, Kp=2.0,
                 diffusive=True)
    else:
        raise ValueError(name)
    return s


def simulate(name, seconds, seed=5, n=24, mu=1.0):
    rng = np.random.default_rng(seed)
    s = structure(name, n, rng, seconds)
    f0 = np.array(s["f"], float)
    n = len(f0)
    steps, dt = int(seconds * CTRL), 1.0 / CTRL
    phi = rng.uniform(0, 2 * np.pi, n)
    r = 0.3 + 0.7 * rng.uniform(0, 1, n)
    fixed = s["fixed"] or []
    r[fixed] = 1.0
    dev = np.zeros(n)
    a_ou = np.exp(-dt / 4.0)
    fr, rr = np.zeros((steps, n)), np.zeros((steps, n))
    for st in range(steps):
        if s["wander"]:
            dev = a_ou * dev + np.sqrt(1 - a_ou ** 2) * rng.normal(0, s["wander"], n)
            dev[fixed] = 0.0
        f = f0 * 2 ** (dev / 1200)
        g = 1.0 if s["ramp"] is None else s["ramp"][0] + (s["ramp"][1] - s["ramp"][0]) * min(1.0, st / (0.7 * steps))
        if s["W"] is None:  # carriers: each free partial listens to every carrier at its nearest harmonic
            cos_t, sin_t, nu = np.zeros(n), np.zeros(n), np.ones(n)
            for c in s["fixed"]:
                hh = np.maximum(1, np.round(f / f[c]))
                d = hh * phi[c] - phi
                cos_t += r[c] * np.cos(d)
                sin_t += hh * r[c] * np.sin(d)
            cos_t[fixed] = sin_t[fixed] = 0.0
        else:
            nu = s["nu"]
            th = phi / nu
            d = th[None, :] - th[:, None] + s["lag"]
            cos_t = (s["W"] * r[None, :] * np.cos(d)).sum(1)
            sin_t = nu * (s["W"] * r[None, :] * np.sin(d)).sum(1)
            if s["diffusive"]:
                cos_t = cos_t - s["W"].sum(1) * r
        dr = r * (mu - r * r) + g * s["Ka"] * cos_t
        dr[fixed] = 0.0
        r = np.maximum(r + dt * dr, 1e-3)
        w = 2 * np.pi * f * (1 - s["beta"] * (r * r - mu)) + g * s["Kp"] * sin_t / (r + 0.4)
        if s["B"] is not None:
            w = w + s["B"] @ (r * r - mu)
        w[fixed] = 2 * np.pi * f[fixed]
        phi = phi + w * dt
        fr[st], rr[st] = w / (2 * np.pi), r
    t_ctrl = np.arange(steps) / CTRL
    t = np.arange(int(seconds * SR)) / SR
    out = np.zeros_like(t)
    for k in range(n):
        fi = np.clip(np.interp(t, t_ctrl, fr[:, k]), 20, 20000)
        ai = np.interp(t, t_ctrl, rr[:, k]) * (110.0 / max(f0[k], 55.0)) ** 0.5
        out += ai * np.sin(2 * np.pi * np.cumsum(fi) / SR + k)
    out *= np.minimum(1, np.minimum(t / 1.5, (seconds - t) / 2.5))
    return out / np.max(np.abs(out)) * 0.5, fr, rr


def descriptors(x):
    f, p = signal.welch(x, SR, nperseg=16384)
    sel = (f > 30) & (f < 8000)
    f, p = f[sel], p[sel]
    centroid = (f * p).sum() / p.sum()
    flatness = np.exp(np.mean(np.log(p + 1e-20))) / np.mean(p)
    peaks, _ = signal.find_peaks(10 * np.log10(p + 1e-20), prominence=10)
    strong = peaks[p[peaks] > p.max() * 10 ** (-40 / 10)]
    # harmonicity: share of peak energy within 1.5 % of a multiple of the strongest low peak
    f1 = f[strong[np.argmax(p[strong])]] if len(strong) else 1.0
    ratio = f[strong] / f1
    harm = p[strong][np.abs(ratio - np.round(ratio)) < 0.015 * np.maximum(1, ratio)].sum() / max(p[strong].sum(), 1e-20)
    ff, tt, S = signal.spectrogram(x, SR, nperseg=8192, noverlap=6144)
    E = 10 * np.log10(S[(ff > 30) & (ff < 8000)] + 1e-12)
    flux = np.mean(np.std(E[:, len(tt) // 4:], axis=1))
    return centroid, flatness, len(strong), harm, flux, (f, p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    names = ["chain", "twisted_ring", "global_sync", "inhibition", "two_carriers", "tree_cascade", "cross_fm",
             "small_world", "freeze"]
    if a.only:
        names = a.only.split(",")
    os.makedirs(os.path.join(ROOT, "build-host", "renders"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "build-host", "plots"), exist_ok=True)
    cols = 3
    rows = (len(names) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(16, 3.4 * rows), squeeze=False)
    fig2, ax2 = plt.subplots(1, 1, figsize=(12, 4.5))
    print(f"{'structure':14s} {'centroid':>9s} {'flatness':>9s} {'peaks':>6s} {'harmonic':>9s} {'movement':>9s}")
    for i, name in enumerate(names):
        x, fr, rr = simulate(name, a.seconds)
        wavfile.write(os.path.join(ROOT, "build-host", "renders", f"exp_net_{name}.wav"), SR,
                      (np.stack([x, np.roll(x, 400)], 1) * 32767).astype(np.int16))
        c, fl, npk, harm, flux, (f, p) = descriptors(x)
        print(f"{name:14s} {c:8.0f}Hz {fl:9.4f} {npk:6d} {harm * 100:8.0f}% {flux:7.2f}dB")
        ax = axes[i // cols, i % cols]
        ff, tt, S = signal.spectrogram(x, SR, nperseg=8192, noverlap=6144)
        sel = ff < 3000
        ax.pcolormesh(tt, ff[sel], 10 * np.log10(S[sel] + 1e-12), shading="auto", vmin=-110, vmax=-30)
        ax.set_title(f"{name}  (peaks {npk}, harmonic {harm * 100:.0f}%, movement {flux:.1f} dB)", fontsize=9)
        ax2.semilogx(f, 10 * np.log10(p + 1e-20), lw=0.8, label=name)
    for j in range(len(names), rows * cols):
        axes[j // cols, j % cols].axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "build-host", "plots", "exp_net.png"), dpi=75)
    ax2.set_xlim(30, 8000)
    ax2.set_ylim(-140, -40)
    ax2.legend(fontsize=7, ncol=3)
    ax2.set_title("long-term spectra")
    fig2.tight_layout()
    fig2.savefig(os.path.join(ROOT, "build-host", "plots", "exp_net_spectra.png"), dpi=75)
    print("plots: build-host/plots/exp_net.png, exp_net_spectra.png")


if __name__ == "__main__":
    main()
