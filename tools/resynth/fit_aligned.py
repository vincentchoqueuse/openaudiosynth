# fit_aligned.py — fit an engine to an isolated synth part whose notes are known (a score, a piano roll): the
# candidate is rendered with the exact clip through synth_render, aligned on the reference by its first note, and
# compared frame by frame: log-mel spectrogram (timbre and its motion within each note), loudness envelope (attack,
# decay, release between notes) and stereo width (side/mid per band: unison, chorus). Search: (1+lambda) evolution
# with restarts around the best. Writes best.json, best.wav and compare.png.
# Usage: python fit_aligned.py SPEC.json  (SPEC: name, outdir, ref, offset (s, the first note in the reference),
#        patch (a patch JSON giving the engine and the clip), base {params}, space {param: [lo, hi, "lin"|"log"|"int"]},
#        iters, seed, optional start (a best.json))
import json, os, subprocess, sys
import numpy as np, librosa, soundfile as sf, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import patchtree  # noqa: E402

spec = json.load(open(sys.argv[1]))
OUT = spec['outdir']; os.makedirs(OUT, exist_ok=True)
RENDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../build-host/host/synth_render')
SR, HOP = 48000, 256
patch = json.load(open(spec['patch']))

def load(path, offset=0.0):
    y, sr = sf.read(path, always_2d=True)
    if sr != SR: y = librosa.resample(y.T, orig_sr=sr, target_sr=SR).T
    return y[int(offset * SR):]

REF = load(spec['ref'], spec.get('offset', 0.0))
N = len(REF)
MEL = dict(sr=SR, n_fft=2048, hop_length=HOP, n_mels=80, fmin=60, fmax=16000)

def features(y):
    y = y[:N] if len(y) >= N else np.pad(y, ((0, N - len(y)), (0, 0)))
    mid, side = y.mean(axis=1), (y[:, 0] - y[:, 1]) / 2
    M = librosa.feature.melspectrogram(y=mid, **MEL); S = librosa.feature.melspectrogram(y=side, **MEL)
    Mdb = 10 * np.log10(M + 1e-10)
    loud = 10 * np.log10(M.sum(axis=0) + 1e-10)
    ref_level = np.percentile(loud, 95)
    width = 10 * np.log10((S.mean(axis=1) + 1e-10) / (M.mean(axis=1) + 1e-10))   # side/mid per band, dB
    return Mdb - ref_level, loud - ref_level, width

RF = features(REF)
active = RF[1] > -45                                     # frames where the reference sounds

def distance(F):
    spec_d = np.mean(np.abs(np.clip(F[0][:, active], -80, 0) - np.clip(RF[0][:, active], -80, 0)))   # dB
    env_d = np.mean(np.abs(np.clip(F[1], -50, 0) - np.clip(RF[1], -50, 0)))                         # dB
    wid_d = np.mean(np.abs(np.clip(F[2], -30, 0) - np.clip(RF[2], -30, 0)))                         # dB
    return float(spec_d + 0.5 * env_d + 0.3 * wid_d), (float(spec_d), float(env_d), float(wid_d))

def render(params, path):
    p = {k: v for k, v in patch.items() if k in patchtree.RESERVED and k != 'macros'}
    p.update(patchtree.tree(patch['engine'], params))
    pj = path + '.json'; json.dump(p, open(pj, 'w'))
    r = subprocess.run([RENDER, '--patch', pj, '--out', path, '--tail', '1'], capture_output=True, text=True)
    return load(path) if r.returncode == 0 and os.path.exists(path) else None

space = spec['space']
def decode(u):
    p = dict(spec['base'])
    for (k, (a, b, sc)), v in zip(space.items(), u):
        p[k] = float(a * (b / a) ** v) if sc == 'log' else float(a + (b - a) * v)
        if sc == 'int': p[k] = int(round(p[k]))
    return p
def encode(p):
    u = []
    for k, (a, b, sc) in space.items():
        v = p.get(k)
        if not isinstance(v, (int, float)): u.append(0.5); continue
        u.append(float(np.log(v / a) / np.log(b / a)) if sc == 'log' else (float(v) - a) / (b - a))
    return np.clip(np.array(u), 0, 1)

rng = np.random.default_rng(spec.get('seed', 1))
n = len(space)
best_u = encode(json.load(open(spec['start']))['params']) if 'start' in spec else np.full(n, 0.5)
tmp = f'{OUT}/cand.wav'
best_d, parts = distance(features(render(decode(best_u), tmp)))
log = [(0, best_d, parts)]
sigma, stall = 0.2, 0
for it in range(1, spec.get('iters', 300) + 1):
    k = rng.random(n) < max(2 / n, 0.3)                   # move a few parameters at a time
    cand = np.clip(best_u + rng.normal(0, sigma, n) * k, 0, 1)
    y = render(decode(cand), tmp)
    if y is None: continue
    d, parts = distance(features(y))
    if d < best_d:
        best_d, best_u, stall = d, cand, 0; log.append((it, d, parts)); sigma = min(0.3, sigma * 1.4)
    else:
        sigma = max(0.02, sigma * 0.97); stall += 1
        if stall > 60: sigma, stall = 0.2, 0              # restart the step around the best
best = decode(best_u)
y = render(best, f'{OUT}/best.wav'); F = features(y); d, parts = distance(F)
json.dump({"params": best, "distance": d, "parts": {"spectrogram_db": parts[0], "envelope_db": parts[1], "width_db": parts[2]},
           "log": log}, open(f'{OUT}/best.json', 'w'), indent=1)
fig, ax = plt.subplots(2, 2, figsize=(15, 8))
ax[0, 0].imshow(np.clip(RF[0], -80, 0), origin='lower', aspect='auto', cmap='magma'); ax[0, 0].set_title('reference (mel dB)')
ax[0, 1].imshow(np.clip(F[0], -80, 0), origin='lower', aspect='auto', cmap='magma'); ax[0, 1].set_title(f'ours — {parts[0]:.1f} dB mean diff')
t = np.arange(len(RF[1])) * HOP / SR
ax[1, 0].plot(t, RF[1], label='reference'); ax[1, 0].plot(t, F[1], label='ours', alpha=.8); ax[1, 0].set_ylim(-50, 3); ax[1, 0].legend(); ax[1, 0].set_title('loudness (dB)')
ax[1, 1].plot(RF[2], label='reference'); ax[1, 1].plot(F[2], label='ours'); ax[1, 1].legend(); ax[1, 1].set_title('stereo width per mel band (side/mid dB)')
fig.suptitle(f"{spec['name']}: distance {d:.2f}"); fig.tight_layout(); fig.savefig(f'{OUT}/compare.png', dpi=90)
print(f"{spec['name']}: {log[0][1]:.2f} -> {d:.2f} (spectrogram {parts[0]:.1f} dB, envelope {parts[1]:.1f} dB, width {parts[2]:.1f} dB), {len(log)} improvements")
