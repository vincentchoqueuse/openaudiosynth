#!/usr/bin/env python3
# plot_spectrum.py — spectrogram + long-term average spectrum of a WAV render (PNG).
# Usage: python3 tools/plot_spectrum.py build-host/renders/va_sweep.wav [more.wav ...]
# Output: build-host/plots/<name>.png
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal
from scipy.io import wavfile


def plot(path, out_dir):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    name = os.path.splitext(os.path.basename(path))[0]

    fig, (a1, a2) = plt.subplots(2, 1, figsize=(10, 8), height_ratios=[2, 1])
    f, t, s = signal.spectrogram(x, sr, window="blackmanharris", nperseg=4096, noverlap=3072)
    s_db = 10 * np.log10(s + 1e-20)
    vmax = s_db.max()
    a1.pcolormesh(t, f, s_db, vmin=vmax - 100, vmax=vmax, shading="auto", cmap="magma")
    a1.set_yscale("symlog", linthresh=200)
    a1.set_ylim(20, sr / 2)
    a1.set_ylabel("Hz")
    a1.set_title(f"{name} — spectrogram (100 dB range)")

    f2, p = signal.welch(x, sr, window="blackmanharris", nperseg=8192)
    a2.semilogx(f2[1:], 10 * np.log10(p[1:] + 1e-20))
    a2.set_xlim(20, sr / 2)
    a2.set_xlabel("Hz")
    a2.set_ylabel("dB")
    a2.grid(True, which="both", alpha=0.3)
    a2.set_title("average spectrum (Welch)")
    fig.tight_layout()
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, name + ".png")
    fig.savefig(out, dpi=110)
    plt.close(fig)
    print(out)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__ or "usage: plot_spectrum.py file.wav ...")
        sys.exit(2)
    for p in sys.argv[1:]:
        plot(p, "build-host/plots")
