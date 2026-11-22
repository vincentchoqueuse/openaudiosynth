# tempo.py — the tempo of a clip from the autocorrelation of its spectral flux, 80..170 BPM (no beat tracker)
import sys, numpy as np, soundfile as sf
from scipy.signal import stft
for f in sys.argv[1:]:
    y, sr = sf.read(f); y = y.mean(axis=1) if y.ndim > 1 else y
    hop = 512
    _, _, Z = stft(y, sr, nperseg=2048, noverlap=2048 - hop)
    S = np.log1p(np.abs(Z) * 100)
    flux = np.maximum(0, np.diff(S, axis=1)).sum(axis=0); flux -= flux.mean()
    ac = np.correlate(flux, flux, 'full')[len(flux) - 1:]
    fps = sr / hop
    bpms = np.arange(80, 170.01, 0.05)
    score = [ac[int(round(60 / b * fps))] + 0.5 * ac[int(round(120 / b * fps))] for b in bpms]
    print(f, 'tempo %.2f BPM' % bpms[int(np.argmax(score))])
