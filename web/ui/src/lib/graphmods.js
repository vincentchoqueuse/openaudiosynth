// graphmods.js — the sound as the Graph draws it: four parts along the signal (Event: the notes and the voice that
// plays them; Engine: what makes the sound; Transform: filters, amp, output filter, crush and the effects of the
// chain; Output: master, saturation, limiter), and for each block what moves it, as small pictograms: the
// engine's own modulators (its envelopes and LFOs), the routes of the whole sound (Global env / LFO, controllers)
// and the macros. Each pictogram knows what to open.
import * as R from './routes.js';
import { modRows } from './mods.js';
import * as M from './matrix.js';

export const PARTS = [
  { id: 'event', label: 'Event', icon: 'sequence', about: 'the notes and the voice that plays them' },
  { id: 'engine', label: 'Engine', icon: 'source', about: 'what makes the sound' },
  { id: 'transform', label: 'Transform', icon: 'transform', about: 'filters, amp and effects' },
  { id: 'output', label: 'Output', icon: 'volume', about: 'master, saturation, limiter' },
];
const OUTPUT = new Set(['master', 'saturation', 'limiter']);

// the part a stage belongs to (engine stages by their kind, effect stages by their place)
export function partOf(st) {
  if (st.kind === 'voice') return 'event';
  if (st.kind === 'source') return 'engine';
  if (st.kind === 'mod') return null; // drawn as pictograms on what it moves
  if (OUTPUT.has(st.id)) return 'output';
  return 'transform';
}

// the stage of a parameter, by its name ("osc2_" a prefix, otherwise a whole name)
const matches = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
export function stageOf(s, name) {
  for (const st of [...(s.engine?.stages ?? []), ...(s.fxStages ?? [])]) if (st.kind !== 'mod' && st.kind !== 'voice' && matches(st.params, name)) return st.id;
  if (!s.engine?.stages?.length) return 'engine';
  // a setting no stage names (the engine's volume): on its amp, the end of the engine's signal
  return s.engine.stages.find((st) => st.kind === 'amp')?.id ?? null;
}

const isOn = (s, st) => !st.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
const kindOfSrc = (label) => (/^(mseg|vmseg)/.test(label) ? 'env' : /^(mlfo|vlfo)/.test(label) ? 'lfo' : /^macro/.test(label) ? 'macro' : 'ctl');

// stage id -> [{kind: 'env' | 'lfo' | 'macro' | 'ctl', label, open()}], one per modulator
export function modsByStage(s) {
  const out = {};
  const add = (id, m) => { if (!id) return; const l = (out[id] ??= []); if (!l.some((x) => x.label === m.label)) l.push(m); };
  // the engine's own modulators, on the stages they name; with a matrix (wt), on what its slots move
  const matrix = M.hasMatrix(s);
  for (const st of s.engine?.stages ?? []) {
    if (st.kind !== 'mod' || !isOn(s, st) || matrix) continue;
    const m = { kind: /env/i.test(st.label) ? 'env' : 'lfo', label: st.label, open: () => s.openPanel('stage', st.id) };
    for (const t of (st.targets ?? '').split(' ').filter(Boolean)) add(t, m);
  }
  const STAGE_OF = { env2: 'env2', lfo1: 'lfo1', lfo2: 'lfo2', amp_env: 'amp' };
  for (const x of M.slots(s)) {
    if (!x.amount || x.dst === 'none') continue;
    const d = M.dstParam(s, x.dst), at = d ? stageOf(s, d.name) : M.dstStage(x.dst);
    const kind = /env/.test(x.src) ? 'env' : /^lfo/.test(x.src) ? 'lfo' : 'ctl';
    const row = modRows().find((r) => r.src === x.src);
    add(at, { kind, label: M.srcName(x.src), open: () => (STAGE_OF[x.src] ? s.openPanel('stage', STAGE_OF[x.src]) : row && s.openPanel('mod', row.key)) });
  }
  // the routes of the whole sound
  const rows = modRows();
  for (const key of R.keys('part')) {
    const x = R.get(s, key);
    if (!x.src || !x.amount) continue;
    const d = s.paramById(x.dst - 1);
    const row = rows.find((r) => r.src === x.srcLabel);
    add(d && stageOf(s, d.name), { kind: kindOfSrc(x.srcLabel), label: R.srcName(x.srcLabel), open: () => row && s.openPanel('mod', row.key) });
  }
  // the macros
  for (const m of s.macroMaps) {
    if (m.macro < 0 || !m.dst) continue;
    const k = m.macro;
    add(stageOf(s, m.dst.name), { kind: 'macro', label: s.macroName(k), open: () => s.openPanel('macro', 'm' + k) });
  }
  return out;
}

// the pictogram of each kind (icons.js)
export const MOD_ICON = { env: 'env', lfo: 'shape', macro: 'macro', ctl: 'modsrc' };
