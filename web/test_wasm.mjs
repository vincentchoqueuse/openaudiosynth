// test_wasm.mjs — smoke test of synth.wasm under Node, the way the worklet uses it: every patch of patches/
// is loaded (with the sample kit it names, from build-host/kits/NAME.oidk, as the page does), its audition
// rendered for 2 s in 128-frame quanta; the output must be finite, not silent and within ±1. Also checks that the
// engines' stage descriptions cover their parameters, and reports how fast wasm renders compared with real time.
// Usage: node web/test_wasm.mjs [build-wasm/web/wasm/synth.wasm]
import { readFileSync, readdirSync, existsSync } from 'node:fs';

const path = process.argv[2] ?? 'build-wasm/web/wasm/synth.wasm';
const module = new WebAssembly.Module(readFileSync(path));
const stub = new Proxy({}, { get: () => () => 52 });
const { exports: w } = new WebAssembly.Instance(module, { wasi_snapshot_preview1: stub, env: stub });
w._initialize?.();
const SR = 48000, N = 128;
const str = (p) => { const b = new Uint8Array(w.memory.buffer, p); let n = 0; while (b[n]) n++; return new TextDecoder().decode(b.slice(0, n)); };
const cstr = (s) => { const e = new TextEncoder().encode(s); const p = w.w_alloc(e.length + 1); const b = new Uint8Array(w.memory.buffer, p, e.length + 1); b.set(e); b[e.length] = 0; return p; };

w.w_init(SR);
const cat = JSON.parse(str(w.w_catalog()));
console.log(`catalog: ${cat.engines.length} engines, ${cat.shared.length} shared parameters`);
// an engine that describes its stages (the page's cards and diagram) gives every parameter to one, and names no
// parameter it does not have
const claims = (tok, name) => tok.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
let stageFail = 0;
for (const e of cat.engines.filter((e) => e.stages?.length)) {
  const left = e.params.map((p) => p.name).filter((n) => !e.stages.some((st) => claims(st.params, n)));
  const ghost = e.stages.flatMap((st) => st.params.split(' ').filter(Boolean)
    .filter((t) => !e.params.some((p) => (t.endsWith('_') ? p.name.startsWith(t) : p.name === t))).map((t) => `${st.id}:${t}`));
  if (left.length || ghost.length) { console.log(`FAIL ${e.name} stages: unclaimed ${left.join(' ')} unknown ${ghost.join(' ')}`); stageFail++; }
}


let fail = stageFail, frames = 0, t0 = performance.now();
for (const f of readdirSync('patches').filter((f) => f.endsWith('.json')).sort()) {
  const p = cstr(readFileSync('patches/' + f, 'utf8'));
  const rc = w.w_patch(p);
  w.w_free(p);
  if (rc) { console.log(`FAIL ${f}: ${str(w.w_error())}`); fail++; continue; }
  const kit = str(w.w_patch_kit()), kitFile = `build-host/kits/${kit}.oidk`;
  if (kit) {
    if (!existsSync(kitFile)) { console.log(`FAIL ${f}: kit ${kitFile} not built (just build)`); fail++; continue; }
    const bytes = readFileSync(kitFile), k = w.w_alloc(bytes.length);
    new Uint8Array(w.memory.buffer, k, bytes.length).set(bytes);
    if (w.w_kit(k, bytes.length) <= 0) { console.log(`FAIL ${f}: kit ${kit} refused`); fail++; continue; }
  }
  const e = cstr('');
  const len = w.w_audition(e, 0);
  w.w_free(e);
  let peak = 0, bad = false;
  for (let pos = 0; pos < 2 * SR; pos += N) {
    w.w_render(N);
    const L = new Float32Array(w.memory.buffer, w.w_buf_l(), N), R = new Float32Array(w.memory.buffer, w.w_buf_r(), N);
    for (let i = 0; i < N; i++) {
      if (!Number.isFinite(L[i]) || !Number.isFinite(R[i])) bad = true;
      peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
    }
    frames += N;
  }
  if (bad || peak < 1e-4 || peak > 1 || len <= 0) { console.log(`FAIL ${f}: peak ${peak} nan ${bad} len ${len}`); fail++; }
}
const s = (performance.now() - t0) / 1000;
console.log(`${fail ? 'FAILED' : 'ok'}: ${fail} failure(s); ${(frames / SR).toFixed(0)} s of audio in ${s.toFixed(1)} s (${(frames / SR / s).toFixed(0)}x real time)`);
process.exit(fail ? 1 : 0);
