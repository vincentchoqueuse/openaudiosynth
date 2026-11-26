# review_library.py — a pass over every sound of the library: its loudness, how it answers velocity (the phrase at
# velocity 40 and 120: level and brightness) and what each macro does (at 0 and at 1); renders through synth_render into
# a scratch directory and writes review.json there. Usage: python tools/review_library.py <repo> <outdir>
import json, os, re, subprocess, sys
import numpy as np
import scipy.io.wavfile as wavfile

REPO, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
CAT = json.load(open(f'{REPO}/docs/catalog.json'))


def render(patch, tag):
    src = f'{OUT}/{patch["name"]}_{tag}.json'
    wav = src[:-5] + '.wav'
    json.dump(patch, open(src, 'w'))
    r = subprocess.run([f'{REPO}/build-host/host/synth_render', '--patch', src, '--out', wav, '--tail', '1.5'],
                       capture_output=True, text=True, cwd=REPO)
    if r.returncode != 0 or not os.path.exists(wav):
        return None
    sr, x = wavfile.read(wav)
    x = x.astype(np.float64)
    if np.abs(x).max() > 2:
        x /= 32768.0
    os.remove(wav)
    os.remove(src)
    return sr, (x.mean(axis=1) if x.ndim == 2 else x)


def loudness(m, sr):
    """gated power of 50 ms frames: frames within 20 dB of the loudest, in dB (a rough integrated loudness)"""
    n = int(0.05 * sr)
    f = m[: len(m) // n * n].reshape(-1, n)
    p = (f ** 2).mean(axis=1)
    if p.max() <= 0:
        return -200.0
    g = p[p > p.max() * 0.01]
    return 10 * np.log10(g.mean())


def centroid(m, sr):
    s = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1 / sr)
    return float((s * f).sum() / max(s.sum(), 1e-30))


def describe(m, sr):
    return loudness(m, sr), centroid(m, sr)


rows = []
for fn in sorted(os.listdir(f'{REPO}/patches')):
    if not fn.endswith('.json'):
        continue
    p = json.load(open(f'{REPO}/patches/{fn}'))
    eng = p.get('engine')
    params = CAT['engines'].get(eng, {}).get('params', {})
    r = render(p, 'base')
    if r is None:
        rows.append({'name': p['name'], 'engine': eng, 'error': 'render failed'})
        continue
    sr, m = r
    L, C = describe(m, sr)
    row = {'name': p['name'], 'engine': eng, 'loud': round(L, 1), 'centroid': round(C), 'peak': round(20 * np.log10(np.abs(m).max() + 1e-12), 1)}
    # velocity: the phrase at 40 and at 120, accents removed
    vel = {}
    for v in (40, 120):
        q = json.loads(json.dumps(p))
        a = q.setdefault('audition', {})
        a['velocity'] = v
        if 'notes' in a:
            a['notes'] = re.sub(r'[!?]', '', a['notes'])
        q.pop('clip', None)
        rv = render(q, f'v{v}')
        vel[v] = describe(rv[1], rv[0]) if rv else (None, None)
    if vel[40][0] is not None and vel[120][0] is not None:
        row['vel_db'] = round(vel[120][0] - vel[40][0], 1)
        row['vel_bright'] = round(100 * (vel[120][1] / max(vel[40][1], 1) - 1))
    # macros: each one at 0 and at 1
    macs = []
    for k, mac in enumerate(p.get('macros', [])):
        if not mac.get('targets'):
            continue
        res = {}
        for v in (0.0, 1.0):
            q = json.loads(json.dumps(p))
            q['macros'][k]['value'] = v
            rv = render(q, f'm{k}_{int(v)}')
            res[v] = describe(rv[1], rv[0]) if rv else (None, None)
        d_db = res[1.0][0] - res[0.0][0] if res[0.0][0] is not None else None
        d_c = 100 * (res[1.0][1] / max(res[0.0][1], 1) - 1) if res[0.0][1] else None
        # a range given as 0..1 numbers for a setting in units (Hz, s, dB...) is probably a slip
        slips = []
        for t in mac['targets']:
            d = params.get(t['param'], {})
            lo, hi = (d.get('range') or [None, None])[:2]
            if isinstance(t.get('from'), (int, float)) and isinstance(t.get('to'), (int, float)) and lo is not None:
                if t['from'] == t['to']:
                    slips.append(f"{t['param']}: from = to")
                elif hi is not None and hi - lo > 2 and 0 <= t['from'] <= 1 and 0 <= t['to'] <= 1 and d.get('unit') not in (None, ''):
                    slips.append(f"{t['param']}: {t['from']}..{t['to']} {d.get('unit')} (range {lo}..{hi})")
        macs.append({'k': k + 1, 'name': mac.get('name', ''), 'db': None if d_db is None else round(d_db, 1),
                     'bright': None if d_c is None else round(d_c), 'slips': slips,
                     'targets': [t['param'] for t in mac['targets']]})
    row['macros'] = macs
    rows.append(row)
    print(f"{p['name']:32s} {L:6.1f} dB  vel {row.get('vel_db')} dB / {row.get('vel_bright')} %  macros {len(macs)}", flush=True)

json.dump(rows, open(f'{OUT}/review.json', 'w'), indent=1)
