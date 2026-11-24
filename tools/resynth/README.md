# resynth — from a reference cut to a preset

Three scripts that turn a short WAV of a synth part (cut with `references/cutter.html`) into a clip and a patch for one of our engines. They need librosa, which needs a Python with numba (3.12 works; the project's `.venv` may be newer):

```sh
uv venv -p 3.12 /tmp/rvenv && uv pip install -p /tmp/rvenv librosa==0.10.2.post1 soundfile matplotlib
```

1. `tempo.py CUT.wav` — the tempo, from the autocorrelation of the spectral flux (80..170 BPM).
2. `notes.py CUT.wav OUTDIR --bpm B [--lo HZ] [--thresh DB] [--max-poly N]` — drums out (HPSS), bass out (high-pass at `--lo`), a semitone constant-Q on a sixteenth grid, harmonics suppressed: writes `CUT.clip.json` (our clip format) and `CUT.roll.png` (the notes over the spectrogram, to check by eye). A note whose second harmonic dominates comes out an octave high: check the roll.
3. `fit.py SPEC.json` (a part in a mix) or `fit_aligned.py SPEC.json` (an isolated part with known notes: the clip of `patch`, aligned on the reference by `offset`, compared frame by frame on the mel spectrogram, the loudness and the stereo width) — renders candidate patches with the clip through `synth_render`, compares features that do not depend on exact timing (average log-mel spectrum, average envelope and brightness after each start) and searches the parameters with a (1+λ) evolution. SPEC: `name, outdir, ref, clip, engine, lo, iters, base {params}, space {param: [lo, hi, "lin"|"log"|"int"]}`, optional `start` (an earlier `best.json`) and `seed`. Writes `best.json`, `best.wav` and `compare.png`.

The distance is a guide, not the ear: leakage from the other parts of the mix (hi-hats, reverb) stays in the reference. The fits are dry; effects are added by hand afterwards. Presets made this way: `poly_warm_chords`, `poly_soft_arp_chords`.
