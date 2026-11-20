#!/usr/bin/env python3
"""stuart_landau.py — experiment: a bank of partials, each a Stuart-Landau oscillator (normal form of the Hopf
bifurcation), diffusively coupled to its neighbours. In polar form, for partial k (harmonic number h_k, phase
theta_k = phi_k / h_k brought back to the fundamental):

    r_k'   = r_k (mu - r_k^2) + K sum_j (r_j cos(theta_j - theta_k) - r_k)      phase -> modulus (and saturation)
    phi_k' = 2 pi f_k (1 - beta (r_k^2 - mu)) + K h_k sum_j (r_j / r_k) sin(theta_j - theta_k)   modulus -> phase

mu sets the level the modulus settles at (sqrt(mu)), beta how much the pitch depends on the modulus
(non-isochronicity), K the coupling. Unlike the first prototype (modphase.py) nothing is clipped: the cubic
term bounds the modulus. Writes build-host/renders/exp_sl_<case>.wav and build-host/plots/exp_sl.png
(spectrogram, frequency / harmonic number of every partial: merging lines = partials locked into the harmonic
series, modulus of every partial). Usage: python3 tools/experiments/stuart_landau.py [--seconds 16]
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


def bank(seconds, Ka, Kp, beta, diffusive=False, mu=1.0, seed=7, f0=110.0, n=16, detune_cents=15.0, stretch=0.0006):
    """Ka: phase -> modulus coupling, Kp: phase coupling (pull), beta: modulus -> pitch. diffusive=True adds the
    -K r_k self term of z_j - z_k coupling, which with detuned partials leads to amplitude death (freezing)."""
    rng = np.random.default_rng(seed)
    h = np.arange(1, n + 1, dtype=float)
    f = f0 * h * np.sqrt(1 + stretch * h ** 2) * 2 ** (rng.normal(0, detune_cents, n) / 1200)
    nb = np.zeros((n, n))
    for k in range(n):
        for j in (k - 1, k + 1):
            if 0 <= j < n:
                nb[k, j] = 1.0
    deg = nb.sum(1)
    steps, dt = int(seconds * CTRL), 1.0 / CTRL
    phi = rng.uniform(0, 2 * np.pi, n)
    r = 0.2 + 0.8 * rng.uniform(0, 1, n)
    fr, rr = np.zeros((steps, n)), np.zeros((steps, n))
    for s in range(steps):
        th = phi / h
        d = th[None, :] - th[:, None]
        cos_t = (nb * r[None, :] * np.cos(d)).sum(1)
        sin_t = (nb * r[None, :] * np.sin(d)).sum(1)
        r = r + dt * (r * (mu - r * r) + Ka * (cos_t - (deg * r if diffusive else 0.0)))
        r = np.maximum(r, 1e-4)
        w = 2 * np.pi * f * (1 - beta * (r * r - mu)) + Kp * h * sin_t / np.maximum(r, 0.05)
        phi = phi + w * dt
        fr[s], rr[s] = w / (2 * np.pi), r
    t_ctrl = np.arange(steps) / CTRL
    t = np.arange(int(seconds * SR)) / SR
    out = np.zeros_like(t)
    for k in range(n):
        fi = np.interp(t, t_ctrl, fr[:, k])
        ai = np.interp(t, t_ctrl, rr[:, k]) / h[k] ** 0.9
        out += ai * np.sin(2 * np.pi * np.cumsum(fi) / SR + k)
    out *= np.minimum(1, np.minimum(t / 2.0, (seconds - t) / 2.5))
    return out / np.max(np.abs(out)) * 0.5, f, h, fr, rr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=16.0)
    a = ap.parse_args()
    # name: (Ka, Kp, beta, diffusive)
    cases = {"free": (0.0, 0.0, 0.0, False), "freeze": (2.0, 2.0, 0.0, True), "lock": (0.0, 0.8, 0.0, False),
             "exchange": (0.6, 0.6, 0.03, False), "critical": (0.8, 0.35, 0.05, False), "storm": (1.5, 1.0, 0.08, False)}
    os.makedirs(os.path.join(ROOT, "build-host", "renders"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "build-host", "plots"), exist_ok=True)
    fig, axes = plt.subplots(len(cases), 3, figsize=(15, 3.2 * len(cases)))
    for row, (name, (Ka, Kp, beta, diffusive)) in enumerate(cases.items()):
        x, f, h, fr, rr = bank(a.seconds, Ka, Kp, beta, diffusive)
        wavfile.write(os.path.join(ROOT, "build-host", "renders", f"exp_sl_{name}.wav"), SR,
                      (np.stack([x, np.roll(x, 300)], 1) * 32767).astype(np.int16))
        ff, tt, S = signal.spectrogram(x, SR, nperseg=8192, noverlap=6144)
        sel = ff < 2000
        axes[row, 0].pcolormesh(tt, ff[sel], 10 * np.log10(S[sel] + 1e-12), shading="auto", vmin=-110, vmax=-30)
        axes[row, 0].set_title(f"{name}: Ka={Ka}, Kp={Kp}, beta={beta}{', diffusive' if diffusive else ''}")
        tc = np.arange(len(fr)) / CTRL
        norm = fr / h[None, :]
        axes[row, 1].plot(tc, norm, lw=0.7)
        axes[row, 1].set_ylim(norm[len(norm) // 5:].min() - 0.5, norm[len(norm) // 5:].max() + 0.5)
        axes[row, 1].set_title("frequency / harmonic number (Hz): merged = locked")
        axes[row, 2].plot(tc, rr, lw=0.7)
        axes[row, 2].set_title("modulus of each partial")
        late = slice(len(fr) // 3, None)
        spread = np.std(1200 * np.log2(norm[late] / np.median(norm[late], axis=1, keepdims=True)), axis=1).mean()
        move = np.std(1200 * np.log2(fr[late] / fr[late].mean(0)), axis=0).mean()
        E = 10 * np.log10(S[sel] + 1e-12)
        print(f"{name:8s} inharmonic spread {spread:6.2f} cents (0 = all locked on the series), pitch movement "
              f"{move:6.2f} cents, modulus movement {np.std(rr[late], axis=0).mean():.3f}, "
              f"band-energy movement {np.mean(np.std(E[:, len(tt) // 4:], axis=1)):.2f} dB")
    for ax in axes[-1]:
        ax.set_xlabel("s")
    fig.tight_layout()
    png = os.path.join(ROOT, "build-host", "plots", "exp_sl.png")
    fig.savefig(png, dpi=80)
    print("plot:", os.path.relpath(png, ROOT))


if __name__ == "__main__":
    main()
