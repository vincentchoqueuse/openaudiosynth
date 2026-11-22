# notes.py — the synth part of a mix as a clip: harmonic/percussive split (drums out), a high-pass (bass out),
# a constant-Q semitone salience on a sixteenth grid at the detected tempo, the strong pitches per cell with their
# harmonics suppressed, cells merged into notes. Writes NAME.clip.json (our clip format) and NAME.roll.png.
# Usage: python notes.py IN.wav OUTDIR [--lo HZ] [--hi HZ] [--bpm BPM] [--thresh DB] [--max-poly N]
import argparse, json, os
import numpy as np, librosa, soundfile as sf, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.signal import butter, sosfiltfilt

ap = argparse.ArgumentParser()
ap.add_argument('wav'); ap.add_argument('outdir')
ap.add_argument('--lo', type=float, default=180.0, help='lowest synth pitch (Hz): the bass is under it')
ap.add_argument('--hi', type=float, default=2100.0, help='highest fundamental considered (Hz)')
ap.add_argument('--bpm', type=float, default=0.0)
ap.add_argument('--thresh', type=float, default=20.0, help='keep pitches within this many dB of the loudest pitch of the whole excerpt')
ap.add_argument('--max-poly', type=int, default=4)
ap.add_argument('--min-db', type=float, default=-38.0, help='cells quieter than this (dB re the loudest) are rests')
a = ap.parse_args()
name = os.path.splitext(os.path.basename(a.wav))[0]
os.makedirs(a.outdir, exist_ok=True)

y, sr = librosa.load(a.wav, sr=None, mono=True)
# tempo from the full mix (the drums help), unless given
if a.bpm <= 0:
    tempo, _ = librosa.beat.beat_track(y=y, sr=sr, units='time')
    a.bpm = float(np.atleast_1d(tempo)[0])
    while a.bpm < 80: a.bpm *= 2
    while a.bpm > 170: a.bpm /= 2
# drums out, bass out
H, _ = librosa.effects.hpss(y, margin=3.0)
H = sosfiltfilt(butter(4, a.lo * 0.84, 'hp', fs=sr, output='sos'), H)
# semitone constant-Q salience from a.lo
fmin = a.lo * 2 ** (-0.5 / 12)
nbins = int(np.ceil(12 * np.log2(8000 / fmin)))
hop = 256
C = np.abs(librosa.cqt(H, sr=sr, hop_length=hop, fmin=fmin, n_bins=nbins, bins_per_octave=12))
Cdb = librosa.amplitude_to_db(C, ref=np.max)
freqs = librosa.cqt_frequencies(nbins, fmin=fmin, bins_per_octave=12)
midi0 = int(round(librosa.hz_to_midi(freqs[0])))
nfund = int(np.searchsorted(freqs, a.hi)) + 1
# sixteenth grid
step = 60.0 / a.bpm / 4
times = librosa.frames_to_time(np.arange(C.shape[1]), sr=sr, hop_length=hop)
ncell = int(np.floor(times[-1] / step))
roll = np.zeros((ncell, nfund), bool)
cell_db = np.full(ncell, -120.0)
for c in range(ncell):
    m = (times >= c * step + step * 0.15) & (times < (c + 1) * step - step * 0.1)  # skip the attack's smear
    if not m.any(): continue
    s = C[:, m].mean(axis=1)
    sdb = 20 * np.log10(s / C.mean(axis=1).max() / 4 + 1e-12)  # re the loudest pitch on average (x4: its peaks)
    cell_db[c] = sdb.max()
    if sdb.max() < a.min_db: continue
    sdb = sdb[:nfund + 28]
    picked = []
    for k in np.argsort(-sdb[:nfund]):
        if len(picked) >= a.max_poly or sdb[k] < -a.thresh: break
        if k > 0 and sdb[k] < sdb[k - 1] or k + 1 < len(sdb) and sdb[k] < sdb[k + 1]: continue  # a local peak only
        # not a harmonic (2x..6x) of a stronger pitch already picked (within 8 dB), nor its neighbour
        if any(k - p in (12, 19, 24, 28, 31) and sdb[k] < sdb[p] + 8 or abs(k - p) == 1 for p in picked): continue
        picked.append(k)
    roll[c, picked] = True
# cells -> notes (a pitch held over consecutive cells is one note)
notes = []
for k in range(nfund):
    c = 0
    while c < ncell:
        if roll[c, k]:
            c1 = c
            while c1 + 1 < ncell and roll[c1 + 1, k]: c1 += 1
            v = int(np.clip(127 + 2.2 * cell_db[c], 40, 127))
            notes.append({"pitch": midi0 + k, "start": round(c * 0.25, 4), "length": round((c1 - c + 1) * 0.25, 4), "velocity": v})
            c = c1 + 1
        else:
            c += 1
notes.sort(key=lambda n: (n['start'], n['pitch']))
length = max(4, int(np.ceil(ncell * 0.25 / 4)) * 4)
clip = {"bpm": round(a.bpm, 2), "length": length, "loop": True, "grid": "1/16", "key": "C chromatic", "notes": notes}
json.dump(clip, open(f'{a.outdir}/{name}.clip.json', 'w'), indent=1)
# the check: notes over the semitone spectrogram
fig, ax = plt.subplots(figsize=(14, 6))
ax.imshow(Cdb[:nfund + 24], origin='lower', aspect='auto', cmap='magma', vmin=-60, vmax=0,
          extent=[0, times[-1], midi0 - 0.5, midi0 + nfund + 23.5])
for n in notes:
    t0 = n['start'] * 4 * step  # beats -> seconds
    ax.add_patch(plt.Rectangle((t0, n['pitch'] - 0.4), n['length'] * 4 * step, 0.8, fill=False, ec='cyan', lw=1.2))
ax.set_xlabel('s'); ax.set_ylabel('MIDI'); ax.set_title(f'{name}: {len(notes)} notes, {a.bpm:.1f} BPM, synth above {a.lo:.0f} Hz')
fig.tight_layout(); fig.savefig(f'{a.outdir}/{name}.roll.png', dpi=110)
pcs = sorted({n['pitch'] for n in notes})
print(f'{name}: {a.bpm:.2f} BPM, {ncell} sixteenths, {len(notes)} notes, pitches {pcs}')
