#!/usr/bin/env python3
"""modphase.py — experiment: a bank of partials whose modulus (amplitude) modulates their phase (frequency) and
whose phase relations modulate their modulus, i.e. modulation passing back and forth between |z| and arg z.

Partial k (harmonic number h_k, slightly stretched and detuned so neighbours drift against each other):
    dphi_k/dt = 2 pi f_k (1 + alpha (r_k - 1))                 modulus -> phase (louder = sharper)
    dr_k/dt   = (1 - r_k) / tau + beta r_k sum_j cos(theta_j - theta_k)   phase -> modulus
with theta_k = phi_k / h_k the partial's phase brought back to the fundamental: neighbours in step with the
harmonic series feed each other, out of step they starve each other. With both couplings on, the loop is an
Adler / Kuramoto-like system: partials pull each other into lock, then slip, in clusters.

Writes, in build-host/renders and build-host/plots: exp_modphase_<case>.wav (case = off, mod2phase, phase2mod,
loop) and a figure with the spectrogram, the instantaneous frequency of every partial (deviation in cents
from its rest frequency) and its amplitude, plus a single-partial Adler demo (locking threshold, asymmetric
sidebands). Usage: python3 tools/experiments/modphase.py [--seconds 14]
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
SR = 48000
CTRL = 2000  # the couplings are integrated at 2 kHz, then the phase increments are interpolated to audio


def bank(seconds, alpha, beta, seed=3, f0=55.0, n=16, tau=1.5, stretch=0.0008, detune_cents=6.0):
    rng = np.random.default_rng(seed)
    h = np.arange(1, n + 1, dtype=float)
    f = f0 * h * (1 + stretch * h ** 2) ** 0.5 * 2 ** (rng.normal(0, detune_cents, n) / 1200)
    steps = int(seconds * CTRL)
    dt = 1.0 / CTRL
    phi = rng.uniform(0, 2 * np.pi, n)
    r = np.ones(n) * 0.5 + 0.5 * rng.uniform(0, 1, n)
    fr = np.zeros((steps, n))  # instantaneous frequency
    rr = np.zeros((steps, n))
    nb = np.zeros((n, n))      # neighbour weights: +/- 2 partials
    for k in range(n):
        for j in (k - 2, k - 1, k + 1, k + 2):
            if 0 <= j < n:
                nb[k, j] = 1.0
    for s in range(steps):
        theta = phi / h
        coupling = (nb * np.cos(theta[None, :] - theta[:, None])).sum(1)
        r += dt * ((1.0 - r) / tau + beta * r * coupling)
        r = np.clip(r, 0.0, 3.0)
        inst = f * (1.0 + alpha * (r - 1.0))
        phi += 2 * np.pi * inst * dt
        fr[s], rr[s] = inst, r
    # audio: interpolate frequency and amplitude, integrate the phase at the audio rate
    t_ctrl = np.arange(steps) / CTRL
    t = np.arange(int(seconds * SR)) / SR
    out = np.zeros_like(t)
    for k in range(n):
        fi = np.interp(t, t_ctrl, fr[:, k])
        ai = np.interp(t, t_ctrl, rr[:, k]) / h[k] ** 0.8  # gentle spectral tilt
        out += ai * np.sin(2 * np.pi * np.cumsum(fi) / SR + k)
    fade = np.minimum(1, np.minimum(t / 1.5, (seconds - t) / 2.0))
    out *= fade
    return out / np.max(np.abs(out)) * 0.5, f, fr, rr


def adler_demo(ax):
    """One partial z = r e^{i phi} coupled to a reference at fixed frequency: spectra below / above lock."""
    for label, dw, col in (("detune 0.6 x lock range (locks)", 0.6, "#2f6f9f"),
                           ("1.2 x (slips, asymmetric sidebands)", 1.2, "#b4531f"),
                           ("3 x (plain beating)", 3.0, "#6e6e73")):
        sr, T = 8000, 8.0
        n = int(sr * T)
        f_ref, lock = 400.0, 6.0  # Hz of pull at full coupling
        fosc = f_ref + dw * lock
        psi = 2 * np.pi * f_ref * np.arange(n) / sr
        phi, x = 0.0, np.zeros(n)
        for i in range(n):  # module from the phase relation, phase speed from the module
            r = 1.0 + 0.5 * np.cos(phi - psi[i])
            phi += 2 * np.pi * (fosc - lock * (r - 1.0) / 0.5) / sr
            x[i] = r * np.cos(phi)
        f, p = signal.welch(x[n // 4:], sr, nperseg=1 << 15)
        sel = (f > f_ref - 40) & (f < f_ref + 60)
        ax.plot(f[sel], 10 * np.log10(p[sel] + 1e-12), color=col, label=label, lw=1.2)
    ax.axvline(f_ref, color="0.6", lw=0.8, ls=":")
    ax.set_xlabel("Hz (reference at 400 Hz)")
    ax.set_ylabel("dB")
    ax.set_title("single partial, Adler loop: lock, slip, beat")
    ax.legend(fontsize=7)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=14.0)
    a = ap.parse_args()
    cases = {"off": (0.0, 0.0), "mod2phase": (0.004, 0.0), "phase2mod": (0.0, 0.35), "loop": (0.004, 0.35),
             "loop_strong": (0.03, 0.8)}
    os.makedirs(os.path.join(ROOT, "build-host", "renders"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "build-host", "plots"), exist_ok=True)
    fig, axes = plt.subplots(len(cases) + 1, 3, figsize=(15, 3.1 * (len(cases) + 1)))
    for row, (name, (alpha, beta)) in enumerate(cases.items()):
        x, f, fr, rr = bank(a.seconds, alpha, beta)
        wavfile.write(os.path.join(ROOT, "build-host", "renders", f"exp_modphase_{name}.wav"), SR,
                      (np.stack([x, np.roll(x, 240)], 1) * 32767).astype(np.int16))
        ff, tt, S = signal.spectrogram(x, SR, nperseg=8192, noverlap=6144)
        sel = ff < 1200
        axes[row, 0].pcolormesh(tt, ff[sel], 10 * np.log10(S[sel] + 1e-12), shading="auto", vmin=-110, vmax=-30)
        axes[row, 0].set_title(f"{name}: alpha={alpha}, beta={beta}")
        axes[row, 0].set_ylabel("Hz")
        tc = np.arange(len(fr)) / CTRL
        cents = 1200 * np.log2(fr / f[None, :])
        axes[row, 1].plot(tc, cents, lw=0.7)
        axes[row, 1].set_title("instantaneous frequency of each partial (cents)")
        axes[row, 2].plot(tc, rr, lw=0.7)
        axes[row, 2].set_title("modulus of each partial")
        spread = np.std(cents[len(cents) // 3:], axis=0).mean()
        flux = np.mean(np.abs(np.diff(10 * np.log10(S[sel] + 1e-12), axis=1)))
        print(f"{name:10s} pitch movement {spread:6.2f} cents rms, amplitude movement {np.std(rr[len(rr)//3:], axis=0).mean():.3f}, "
              f"spectral flux {flux:.2f} dB/frame")
    adler_demo(axes[-1, 0])
    axes[-1, 1].axis("off")
    axes[-1, 2].axis("off")
    for ax in axes[-2]:
        ax.set_xlabel("s")
    fig.tight_layout()
    png = os.path.join(ROOT, "build-host", "plots", "exp_modphase.png")
    fig.savefig(png, dpi=80)
    print("plot:", os.path.relpath(png, ROOT))


if __name__ == "__main__":
    main()
