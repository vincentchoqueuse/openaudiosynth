// test_song.mjs — the Song tab's live player under Node, the way the page uses it: every song of songs/ with its
// patches put inline, played by w_song for 4 s in 128-frame quanta (finite, not silent, within ±1), then every
// track muted with w_part_param (part_mute) and the reverb's return closed (its tails last up to half a minute):
// the output must fall silent within 2 s. Also the sequencer's clip player (w_notes): a looping clip keeps going
// past its length, a clip played once stops at its end.
// Usage: node web/test_song.mjs [build-wasm/web/wasm/synth.wasm]
import { readFileSync, readdirSync } from 'node:fs';

const path = process.argv[2] ?? 'build-wasm/web/wasm/synth.wasm';
const module = new WebAssembly.Module(readFileSync(path));
const stub = new Proxy({}, { get: () => () => 52 });
const { exports: w } = new WebAssembly.Instance(module, { wasi_snapshot_preview1: stub, env: stub });
w._initialize?.();
const SR = 48000, N = 128, PART_MUTE = 514, PARTS = 8;
const str = (p) => { const b = new Uint8Array(w.memory.buffer, p); let n = 0; while (b[n]) n++; return new TextDecoder().decode(b.slice(0, n)); };
const cstr = (s) => { const e = new TextEncoder().encode(s); const p = w.w_alloc(e.length + 1); const b = new Uint8Array(w.memory.buffer, p, e.length + 1); b.set(e); b[e.length] = 0; return p; };

// peak and RMS over `seconds`, NaN flagged
function play(seconds) {
  let peak = 0, sum = 0, n = 0, bad = false;
  for (let pos = 0; pos < seconds * SR; pos += N) {
    w.w_render(N);
    const L = new Float32Array(w.memory.buffer, w.w_buf_l(), N), R = new Float32Array(w.memory.buffer, w.w_buf_r(), N);
    for (let i = 0; i < N; i++) {
      if (!Number.isFinite(L[i]) || !Number.isFinite(R[i])) bad = true;
      peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
      sum += L[i] * L[i] + R[i] * R[i];
      n += 2;
    }
  }
  return { peak, rms: Math.sqrt(sum / n), bad };
}

w.w_init(SR);
const cat = JSON.parse(str(w.w_catalog()));
const reverbMix = cat.shared.find((d) => d.name === 'reverb_mix').id;
let fail = 0;
for (const f of readdirSync('songs').filter((f) => f.endsWith('.json')).sort()) {
  const song = JSON.parse(readFileSync('songs/' + f, 'utf8'));
  for (const t of song.tracks) if (typeof t.patch === 'string') t.patch = JSON.parse(readFileSync(t.patch, 'utf8'));
  const p = cstr(JSON.stringify(song));
  const len = w.w_song(p, 0);
  w.w_free(p);
  if (len < 0) { console.log(`FAIL ${f}: ${str(w.w_error())}`); fail++; continue; }
  const on = play(4);
  for (let k = 0; k < PARTS; k++) w.w_part_param(k, PART_MUTE, 1);
  w.w_set_param(reverbMix, 0);
  play(2);
  const off = play(0.5);
  const ok = !on.bad && on.peak > 1e-4 && on.peak <= 1 && off.peak < 1e-4;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${f}: ${len.toFixed(0)} s, peak ${(20 * Math.log10(on.peak + 1e-12)).toFixed(1)} dBFS` +
    ` over the first 4 s, muted ${(20 * Math.log10(off.peak + 1e-12)).toFixed(1)} dBFS`);
  if (!ok) fail++;
  w.w_stop();
}
// the clip player: 2 notes over 4 beats at 120 BPM (2 s), looped then once
function clip(loop) {
  const data = new Float32Array([60, 0, 1, 100, 67, 2, 1.5, 90]);
  const p = w.w_alloc(data.byteLength);
  new Float32Array(w.memory.buffer, p, data.length).set(data);
  const len = w.w_notes(p, 2, 120, 4, loop);
  w.w_free(p);
  const a = play(1);
  play(2); // past the clip's end
  return { len, sounding: a.peak > 1e-4, pos: w.w_player_pos() };
}
w.w_patch(cstr('{"version":1,"engine":"sine","name":"t","params":{}}'));
const looped = clip(1), once = clip(0);
if (Math.abs(looped.len - 2) > 1e-6 || !looped.sounding || looped.pos < 0 || once.pos !== -1) {
  console.log(`FAIL clip player: looped ${JSON.stringify(looped)}, once ${JSON.stringify(once)}`);
  fail++;
} else console.log('ok   clip player: loops past its length, played once it stops');
w.w_stop();
console.log(fail ? `FAILED: ${fail} song(s)` : 'ok');
process.exit(fail ? 1 : 0);
