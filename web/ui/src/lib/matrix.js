// matrix.js — an instrument's own modulation matrix (wt: 6 slots mod1..mod6 of source, destination, amount, via),
// read and written slot by slot, so that each slot is shown where its source is set (an envelope's or an LFO's
// panel, a controller's) and on the settings it moves, like the routes of the whole sound (routes.js); the Matrix
// itself is then no row of its own. Destinations scale as in dsp/engines/wt.c (dst_scale).
import { human } from './naming.js';

const SLOTS = 6;
const P = (k, n) => `mod${k}_${n}`;
export const hasMatrix = (s) => !!s.byName?.mod1_source?.labels;
const labels = (s, n) => s.byName[`mod1_${n}`]?.labels ?? [];

// the matrix's name of the source an engine stage or a controller is (null: none)
const STAGE_SRC = { env2: 'env2', lfo1: 'lfo1', lfo2: 'lfo2', amp: 'amp_env' };
export const srcOfStage = (s, id) => (hasMatrix(s) && labels(s, 'source').includes(STAGE_SRC[id]) ? STAGE_SRC[id] : null);
export const srcOfCtl = (s, name) => (hasMatrix(s) && labels(s, 'source').includes(name) ? name : null);

// every slot in use: {k, src, dst (labels), amount, via (index)}
export function slots(s) {
  if (!hasMatrix(s)) return [];
  const out = [];
  for (let k = 1; k <= SLOTS; k++) {
    const src = labels(s, 'source')[Math.round(s.getP(P(k, 'source')))];
    if (!src || src === 'none') continue;
    out.push({ k, src, dst: labels(s, 'destination')[Math.round(s.getP(P(k, 'destination')))] ?? 'none',
               amount: s.getP(P(k, 'amount')), via: Math.round(s.getP(P(k, 'via'))) });
  }
  return out;
}
export const from = (s, src) => slots(s).filter((x) => x.src === src);

// the setting a destination moves, when it is one (cutoff, a_position...), and the stage of the others
const DST_STAGE = { pitch: 'osca', detune: 'osca', amp: 'amp', pan: 'amp', dist: 'dist' };
export const dstParam = (s, dst) => s.byName[dst] ?? null;
export const dstStage = (dst) => DST_STAGE[dst] ?? null;
export const dstName = (dst) => (dst === 'none' ? '—' : dst === 'amp' ? 'Level' : dst === 'dist' ? 'Distortion' : human(dst));
export const srcName = (src) => ({ env2: 'Env 2', amp_env: 'Amp env', lfo1: 'LFO 1', lfo2: 'LFO 2', note: 'Key tracking', mod_wheel: 'Mod wheel', random: 'Random per note' }[src]
  ?? src.replace(/^./, (c) => c.toUpperCase()));

export const dstOptions = (s) => labels(s, 'destination').map((l, i) => ({ value: i, label: dstName(l) })).slice(1);
export const viaOptions = (s) => labels(s, 'via').map((l, i) => ({ value: i, label: i ? `× ${srcName(l)}` : '—' }));

// the amount in the destination's units (full scale: wt.c dst_scale)
const SCALE = { pitch: [24, 'st'], cutoff: [8, 'oct'], detune: [50, 'ct'], dist: [24, 'dB'] };
export function inUnits(x) {
  const [k, u] = SCALE[x.dst] ?? [100, '%'], v = x.amount * k;
  return `${v >= 0 ? '+' : ''}${+v.toPrecision(3)} ${u}`;
}

export const set = (s, x, n, v) => s.setP(P(x.k, n), v);
export function clear(s, x) { for (const n of ['amount', 'via', 'destination', 'source']) s.setP(P(x.k, n), 0); }
export const free = (s) => { if (!hasMatrix(s)) return null; for (let k = 1; k <= SLOTS; k++) if (!Math.round(s.getP(P(k, 'source')))) return k; return null; };
// a new slot from a source to a destination (by labels), at a quarter of its travel; null when all are taken
export function add(s, src, dst = 'cutoff') {
  const k = free(s);
  if (!k) return null;
  s.setP(P(k, 'source'), labels(s, 'source').indexOf(src));
  s.setP(P(k, 'destination'), Math.max(1, labels(s, 'destination').indexOf(dst)));
  s.setP(P(k, 'amount'), 0.25);
  return k;
}
