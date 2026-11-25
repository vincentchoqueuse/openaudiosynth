// onoff.js — the switch of every row of the Synth table. A stage or an effect: its own enabling setting (one with
// none, the amp or the master, is always on). A source of modulation: on while one of its routes moves something;
// off mutes its routes, their amounts kept to come back. Wander: its amount. A macro: on while it moves something;
// off detaches its settings, kept to come back. What is kept lasts while the sound is the same (another sound
// forgets it). locked: a switch that cannot be turned (always on, or off with nothing to turn on).
import { stageOn, toggleStage } from './stages.js';
import * as R from './routes.js';

let gen = -1, kept = {};
const memory = (s) => { if (gen !== s.patchGen) { gen = s.patchGen; kept = {}; } return kept; };
const WANDER = { enable: 'wander', off: 0, on: 0.5 };
const routesOf = (s, r) => r.kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k))).filter((x) => x.src && x.srcLabel === r.src);
const mapsOf = (s, k) => s.macroMaps.filter((m) => m.dst && m.macro === k);

export function isOn(s, r) {
  if (r.kind === 'stage') return stageOn(s, r.st);
  if (r.kind === 'mod') return r.key === 'wander' ? s.getP('wander') > 0 : routesOf(s, r).some((x) => x.amount !== 0);
  if (r.kind === 'macro') return mapsOf(s, r.k).length > 0;
  return r.kind === 'kit';
}
// can it be switched? not a stage that is always there, not a source or a macro off with nothing kept to bring back
export function locked(s, r) {
  if (r.kind === 'stage') return !r.st?.enable;
  if (r.kind === 'kit' || r.kind === 'slot') return true;
  if (isOn(s, r) || r.key === 'wander') return false;
  return !memory(s)[r.key];
}
export function toggle(s, r) {
  const mem = memory(s);
  if (r.kind === 'stage') return toggleStage(s, r.st);
  if (r.key === 'wander') return toggleStage(s, WANDER);
  if (r.kind === 'mod') {
    if (isOn(s, r)) { // mute: every route of it at 0, its amounts kept
      const xs = routesOf(s, r).filter((x) => x.amount !== 0);
      mem[r.key] = xs.map((x) => [x.key, x.amount]);
      for (const x of xs) s.setP(`${x.pre}${x.r}_amount`, 0);
    } else for (const [key, a] of mem[r.key] ?? []) { const x = R.get(s, key); if (x.srcLabel === r.src) s.setP(`${x.pre}${x.r}_amount`, a); }
    return;
  }
  if (r.kind === 'macro') {
    if (isOn(s, r)) { // detach: its settings keep their slot and their range, the slot no longer follows the macro
      const ms = mapsOf(s, r.k);
      mem[r.key] = ms.map((m) => m.slot);
      for (const m of ms) s.setP(`map${m.slot}_macro`, 0);
    } else for (const slot of mem[r.key] ?? []) s.setP(`map${slot}_macro`, r.k + 1);
  }
}
