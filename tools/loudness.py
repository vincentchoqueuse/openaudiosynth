# loudness.py — the level of every patch, to keep them even: renders each (its clip, or its audition) with
# synth_render and prints its gated RMS (the louder half of 400 ms windows, so a sparse pluck and a held drone compare)
# and its peak. Patches aim at -18 dBFS gated, held back where their peak would pass -3 dBFS.
# Usage: python tools/loudness.py OUTDIR [patch names...]
import json, subprocess, sys, glob, os, numpy as np, scipy.io.wavfile as w
OUT = sys.argv[1]
names = sys.argv[2:] or sorted(os.path.basename(p)[:-5] for p in glob.glob('patches/*.json'))
res = {}
for n in names:
    wav = f'{OUT}/{n}.wav'
    subprocess.run(['./build-host/host/synth_render', '--patch', f'patches/{n}.json', '--out', wav, '--tail', '2'], capture_output=True)
    sr, x = w.read(wav)
    x = x.astype(np.float64)
    if np.abs(x).max() > 2: x /= 32768
    m = np.sqrt((x ** 2).mean(axis=1)) if x.ndim > 1 else np.abs(x)
    win = int(0.4 * sr); k = len(m) // win
    r = np.sqrt((m[:k * win].reshape(k, win) ** 2).mean(axis=1))
    r = np.sort(r)[k // 2:]
    g = 20 * np.log10(np.sqrt((r ** 2).mean()) + 1e-12)
    pk = 20 * np.log10(np.abs(x).max() + 1e-12)
    vol = json.load(open(f'patches/{n}.json'))['params'].get('volume', 0)
    res[n] = (round(g, 1), round(pk, 1), vol)
    print(f'{n:28s} gated {g:6.1f}  peak {pk:5.1f}  volume {vol}', flush=True)
json.dump(res, open(f'{OUT}/loud.json', 'w'))
