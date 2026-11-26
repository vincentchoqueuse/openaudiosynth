# loudness.py — the level of every patch, to keep the library even: renders each (synth_render) and measures its
# K-weighted loudness (ITU-R BS.1770 weighting, LUFS-like, gated to the 50 ms windows within 20 dB of its loudest) and
# its peak; with --fix, moves the volume of each patch more than 3 dB from the library's median toward it, its peak
# kept under -1 dBFS. Usage: python tools/loudness.py <repo> <tmpdir> [--fix]
import json, os, subprocess, sys
import numpy as np, scipy.io.wavfile as wavfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patchtree  # noqa: E402
from scipy.signal import lfilter
# K-weighting (ITU-R BS.1770) at 48 kHz: a high shelf, then a high-pass
KB1, KA1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
KB2, KA2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
R, T = sys.argv[1], sys.argv[2]
os.makedirs(T, exist_ok=True)

def measure(path):
    wav = f'{T}/x.wav'
    subprocess.run([f'{R}/build-host/host/synth_render', '--patch', path, '--out', wav, '--tail', '1.5'], capture_output=True, cwd=R)
    sr, x = wavfile.read(wav); x = x.astype(np.float64)
    if np.abs(x).max() > 2: x /= 32768.0
    k = lfilter(KB2, KA2, lfilter(KB1, KA1, x, axis=0), axis=0)  # K-weighted, per channel
    n = int(0.05 * sr)
    e = (k[: len(k) // n * n] ** 2).sum(axis=1)                 # channel powers summed, as BS.1770
    p = e.reshape(-1, n).mean(axis=1)
    g = p[p > p.max() * 0.01]
    return -0.691 + 10 * np.log10(g.mean()), 20 * np.log10(np.abs(x).max())

names = sorted(f for f in os.listdir(f'{R}/patches') if f.endswith('.json'))
lv = {n: measure(f'{R}/patches/{n}') for n in names}
med = float(np.median([v[0] for v in lv.values()]))
print(f'median {med:.1f} dB')
for n in names:
    L, pk = lv[n]
    if abs(L - med) <= 3.0:
        continue
    gain = min(med - L, -1.0 - pk)  # toward the median, never a peak above -1 dBFS
    if '--fix' in sys.argv and abs(gain) > 0.5:
        path = f'{R}/patches/{n}'; p = json.load(open(path))
        v0 = float(patchtree.get(p, 'volume', 0.0))
        patchtree.set(p, 'volume', round(max(-60.0, min(24.0, v0 + gain)), 1))
        open(path, 'w').write(patchtree.dumps(p))
        L2, pk2 = measure(path)
        print(f'{n[:-5]:30s} {L:6.1f} -> {L2:6.1f} dB (volume {v0:+.1f} -> {patchtree.get(p, "volume"):+.1f}, peak {pk2:5.1f})')
    else:
        print(f'{n[:-5]:30s} {L:6.1f} dB, peak {pk:5.1f}: would move {gain:+.1f}')
