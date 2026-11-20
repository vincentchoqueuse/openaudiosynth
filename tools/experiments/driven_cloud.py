#!/usr/bin/env python3
"""driven_cloud.py — experiment: a cloud of partials with random frequencies, each a Stuart-Landau oscillator,
pulled by a carrier. Each partial looks at the carrier harmonic nearest to it (n = round(f_k / f_c)) and is
coupled to it n:1: close enough, it locks onto that harmonic and the carrier feeds its modulus; too far, it
drifts and starves. The natural frequencies wander slowly at random (Ornstein-Uhlenbeck in cents) and the
carrier may change note, so the cloud keeps reorganizing around the carrier's harmonics.

    r_k'   = r_k (mu - r_k^2) + Ka A cos(n_k psi - phi_k)                        carrier phase -> modulus
    phi_k' = 2 pi f_k(t) (1 - beta (r_k^2 - mu)) + Kp (A / (r_k + 0.4)) sin(n_k psi - phi_k)   modulus -> phase, pull

Writes build-host/renders/exp_cloud_<case>.wav and build-host/plots/exp_cloud.png (spectrogram; every partial's
frequency divided by the carrier's: horizontal lines at integers = partials locked onto a carrier harmonic;
share of locked partials over time). Usage: python3 tools/experiments/driven_cloud.py [--seconds 24]
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


def carrier_track(seconds, melody):
    """Carrier frequency over time: a note, or a slow melody with glides between notes."""
    t = np.arange(int(seconds * CTRL)) / CTRL
    if not melody:
        return np.full_like(t, 110.0)
    notes = [110.0, 87.31, 130.81, 98.0]  # A2, F2, C3, G2
    idx = np.minimum((t // 6).astype(int), len(notes) - 1)
    target = np.array(notes)[idx]
    out = np.empty_like(target)
    f = target[0]
    for i, tg in enumerate(target):  # 0.6 s glide (one pole in log frequency)
        f *= (tg / f) ** (1.0 / (0.6 * CTRL))
        out[i] = f
    return out


def cloud(seconds, Ka, Kp, beta, melody=False, n=28, mu=1.0, seed=11, wander_cents=40.0, wander_s=4.0):
    rng = np.random.default_rng(seed)
    base = np.exp(rng.uniform(np.log(70.0), np.log(2200.0), n))   # random partial frequencies
    steps, dt = int(seconds * CTRL), 1.0 / CTRL
    fc = carrier_track(seconds, melody)
    phi = rng.uniform(0, 2 * np.pi, n)
    psi = 0.0
    r = 0.3 + 0.7 * rng.uniform(0, 1, n)
    dev = rng.normal(0, wander_cents, n)                            # wandering offset (cents)
    a_ou = np.exp(-dt / wander_s)
    fr, rr, ratio = np.zeros((steps, n)), np.zeros((steps, n)), np.zeros((steps, n))
    for s in range(steps):
        dev = a_ou * dev + np.sqrt(1 - a_ou ** 2) * rng.normal(0, wander_cents, n)
        f_nat = base * 2 ** (dev / 1200)
        h = np.maximum(1, np.round(f_nat / fc[s]))                   # carrier harmonic each partial listens to
        d = h * psi - phi
        A = 1.0
        r = r + dt * (r * (mu - r * r) + Ka * A * np.cos(d))
        r = np.maximum(r, 1e-3)
        w = 2 * np.pi * f_nat * (1 - beta * (r * r - mu)) + Kp * h * (A / (r + 0.4)) * np.sin(d)
        phi = phi + w * dt
        psi = psi + 2 * np.pi * fc[s] * dt
        fr[s], rr[s], ratio[s] = w / (2 * np.pi), r, w / (2 * np.pi) / fc[s]
    t_ctrl = np.arange(steps) / CTRL
    t = np.arange(int(seconds * SR)) / SR
    out = np.zeros_like(t)
    for k in range(n):
        fi = np.interp(t, t_ctrl, fr[:, k])
        ai = np.interp(t, t_ctrl, rr[:, k]) * (100.0 / base[k]) ** 0.35  # gentle tilt: high partials softer
        out += ai * np.sin(2 * np.pi * np.cumsum(fi) / SR + k)
    fct = np.interp(t, t_ctrl, fc)
    out += 0.6 * np.sin(2 * np.pi * np.cumsum(fct) / SR)  # the carrier itself, low in the mix
    out *= np.minimum(1, np.minimum(t / 2.0, (seconds - t) / 3.0))
    return out / np.max(np.abs(out)) * 0.5, fr, rr, ratio


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=24.0)
    a = ap.parse_args()
    # name: (Ka, Kp, beta, melody)
    cases = {"cloud": (0.0, 0.0, 0.0, False), "gravity": (0.6, 6.0, 0.02, False), "strong": (1.2, 18.0, 0.03, False),
             "melody": (0.8, 10.0, 0.03, True)}
    os.makedirs(os.path.join(ROOT, "build-host", "renders"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "build-host", "plots"), exist_ok=True)
    fig, axes = plt.subplots(len(cases), 3, figsize=(15, 3.3 * len(cases)))
    for row, (name, (Ka, Kp, beta, melody)) in enumerate(cases.items()):
        x, fr, rr, ratio = cloud(a.seconds, Ka, Kp, beta, melody)
        wavfile.write(os.path.join(ROOT, "build-host", "renders", f"exp_cloud_{name}.wav"), SR,
                      (np.stack([x, np.roll(x, 400)], 1) * 32767).astype(np.int16))
        ff, tt, S = signal.spectrogram(x, SR, nperseg=8192, noverlap=6144)
        sel = ff < 2500
        axes[row, 0].pcolormesh(tt, ff[sel], 10 * np.log10(S[sel] + 1e-12), shading="auto", vmin=-110, vmax=-30)
        axes[row, 0].set_title(f"{name}: Ka={Ka}, Kp={Kp}, beta={beta}{', carrier melody' if melody else ''}")
        tc = np.arange(len(fr)) / CTRL
        axes[row, 1].plot(tc, ratio, lw=0.6)
        axes[row, 1].set_ylim(0.5, 14.5)
        axes[row, 1].set_title("partial frequency / carrier (integers = locked)")
        locked = (np.abs(ratio - np.round(ratio)) < 0.01).mean(1)
        axes[row, 2].plot(tc, locked, color="#2f6f9f")
        axes[row, 2].set_ylim(0, 1)
        axes[row, 2].set_title("share of partials locked on a carrier harmonic")
        E = 10 * np.log10(S[sel] + 1e-12)
        late = slice(len(fr) // 4, None)
        print(f"{name:8s} locked {locked[late].mean() * 100:5.1f} % of the time, pitch movement "
              f"{np.std(1200 * np.log2(fr[late] / fr[late].mean(0)), axis=0).mean():6.1f} cents, modulus movement "
              f"{np.std(rr[late], axis=0).mean():.3f}, band-energy movement {np.mean(np.std(E[:, len(tt) // 4:], axis=1)):.2f} dB")
    for ax in axes[-1]:
        ax.set_xlabel("s")
    fig.tight_layout()
    png = os.path.join(ROOT, "build-host", "plots", "exp_cloud.png")
    fig.savefig(png, dpi=80)
    print("plot:", os.path.relpath(png, ROOT))


if __name__ == "__main__":
    main()
