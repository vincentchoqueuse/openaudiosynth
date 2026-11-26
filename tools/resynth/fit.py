# fit.py — fit one of our engines to a reference synth part: render a candidate patch with the extracted clip
# (synth_render), process it like the reference (drums out by HPSS, bass out by a high-pass), compare features that do
# not depend on exact timing (average log-mel spectrum, average envelope after each note start, centroid trajectory
# after each start), and search the parameters with a (1+lambda) evolution. Writes the best patch, its render and a
# comparison plot.
# Usage: python fit.py SPEC.json   (SPEC: ref, clip, engine, base params, space {name: [lo, hi, "lin"|"log"]}, iters)
import json, os, subprocess, sys
import numpy as np, librosa, soundfile as sf, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.signal import butter, sosfiltfilt
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import patchtree  # noqa: E402

spec = json.load(open(sys.argv[1]))
OUT = spec['outdir']; os.makedirs(OUT, exist_ok=True)
RENDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../build-host/host/synth_render')
SR = 48000
clip = json.load(open(spec['clip']))
spb = 60.0 / clip['bpm']
starts = sorted({n['start'] for n in clip['notes']})
lo = spec.get('lo', 180.0)

def prep(y, sr, hpss):
    if sr != SR: y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    if hpss: y, _ = librosa.effects.hpss(y, margin=3.0)
    return sosfiltfilt(butter(4, lo * 0.84, 'hp', fs=SR, output='sos'), y)

def features(y):
    hop = 256
    M = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=2048, hop_length=hop, n_mels=64, fmin=lo, fmax=12000)
    Mdb = 10 * np.log10(M + 1e-10)
    mel = Mdb.mean(axis=1); mel -= mel.mean()                      # timbre: the average spectrum's shape
    rms = librosa.feature.rms(y=y, frame_length=1024, hop_length=hop)[0]
    cen = librosa.feature.spectral_centroid(y=y, sr=SR, n_fft=2048, hop_length=hop)[0]
    W = int(0.45 * SR / hop)                                         # 450 ms after each start
    envs, cens = [], []
    for b in starts:
        f0 = int(b * spb * SR / hop)
        if f0 + W <= len(rms):
            e = rms[f0:f0 + W]; envs.append(20 * np.log10(e / (e.max() + 1e-9) + 1e-6))
            cens.append(np.log2(cen[f0:f0 + W] + 1))
    env = np.mean(envs, axis=0) if envs else np.zeros(W)
    cn = np.mean(cens, axis=0) if cens else np.zeros(W)
    return mel, env, cn - cn.mean(), float(np.mean(cens)) if cens else 0.0

ref_y, ref_sr = sf.read(spec['ref']); ref_y = ref_y.mean(axis=1) if ref_y.ndim > 1 else ref_y
REF = features(prep(ref_y, ref_sr, True))

def render(params, path):
    patch = {"version": 2, "engine": spec['engine'], "name": "fit", "description": "fit",
             **patchtree.tree(spec['engine'], params), "clip": dict(clip, notes=clip['notes'])}
    pj = path + '.json'; json.dump(patch, open(pj, 'w'))
    r = subprocess.run([RENDER, '--patch', pj, '--out', path, '--tail', '0.5'], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(path): return None
    y, sr = sf.read(path); return y.mean(axis=1) if y.ndim > 1 else y

def distance(F):
    mel, env, cn, cabs = F
    d_mel = np.sqrt(np.mean((mel - REF[0]) ** 2))                    # dB
    d_env = np.sqrt(np.mean((np.clip(env, -40, 0) - np.clip(REF[1], -40, 0)) ** 2)) * 0.5
    d_cen = np.sqrt(np.mean((cn - REF[2]) ** 2)) * 6 + abs(cabs - REF[3]) * 4
    return float(d_mel + d_env + d_cen), (float(d_mel), float(d_env), float(d_cen))

space = spec['space']
def decode(u):  # u in [0,1]^n -> params
    p = dict(spec['base'])
    for (k, (a, b, sc)), v in zip(space.items(), u):
        p[k] = float(a * (b / a) ** v) if sc == 'log' else float(a + (b - a) * v)
        if sc == 'int': p[k] = int(round(a + (b - a) * v))
    return p

rng = np.random.default_rng(spec.get('seed', 1))
n = len(space)
def encode(p):  # params -> u (the inverse of decode), to start from an earlier result
    u = []
    for k, (a, b, sc) in space.items():
        v = p.get(k, None)
        if v is None: u.append(0.5); continue
        u.append(float(np.log(v / a) / np.log(b / a)) if sc == 'log' else (float(v) - a) / (b - a))
    return np.clip(np.array(u), 0, 1)
best_u = encode(json.load(open(spec['start']))['params']) if 'start' in spec else np.array([0.5] * n, float)
tmp = f'{OUT}/cand.wav'
y = render(decode(best_u), tmp); best_d, parts = distance(features(prep(y, SR, False)))
log = [(0, best_d, parts)]
sigma = 0.25
for it in range(1, spec.get('iters', 120) + 1):
    cand = np.clip(best_u + rng.normal(0, sigma, n), 0, 1)
    y = render(decode(cand), tmp)
    if y is None: continue
    d, parts = distance(features(prep(y, SR, False)))
    if d < best_d:
        best_d, best_u = d, cand; log.append((it, d, parts)); sigma = min(0.35, sigma * 1.3)
    else:
        sigma = max(0.04, sigma * 0.97)
best = decode(best_u)
y = render(best, f'{OUT}/best.wav')
F = features(prep(y, SR, False)); d, parts = distance(F)
json.dump({"params": best, "distance": d, "parts": {"mel_db": parts[0], "env": parts[1], "centroid": parts[2]}, "log": log},
          open(f'{OUT}/best.json', 'w'), indent=1)
# comparison plot
fig, ax = plt.subplots(1, 3, figsize=(15, 4))
ax[0].plot(REF[0], label='reference'); ax[0].plot(F[0], label='ours'); ax[0].set_title(f'average spectrum (mel, dB) — {parts[0]:.1f} dB rms'); ax[0].legend()
t = np.arange(len(REF[1])) * 256 / SR * 1000
ax[1].plot(t, REF[1], label='reference'); ax[1].plot(t, F[1], label='ours'); ax[1].set_title('envelope after a start (dB)'); ax[1].set_xlabel('ms'); ax[1].set_ylim(-40, 2)
ax[2].plot(t, REF[2] + REF[3], label='reference'); ax[2].plot(t, F[2] + F[3], label='ours'); ax[2].set_title('brightness after a start (log2 Hz)'); ax[2].set_xlabel('ms')
fig.suptitle(f"{spec['name']}: {spec['engine']}, distance {d:.2f}"); fig.tight_layout(); fig.savefig(f'{OUT}/compare.png', dpi=100)
print(f"{spec['name']}: distance {log[0][1]:.2f} -> {d:.2f} (mel {parts[0]:.1f} dB, env {parts[1]:.2f}, centroid {parts[2]:.2f}), {len(log)} improvements")
