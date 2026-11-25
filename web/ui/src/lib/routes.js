// routes.js — the modulation routes of a sound, read and written by key: 'p1'..'p8' the whole sound's (source,
// destination: any live setting of the instrument, the master or the effects, amount, via) and 'v1'..'v4' each
// note's (source, one of the note's destinations, amount). Shared by the Modulation page's tables and the side panel.
import { human } from './naming.js';

const LABEL = { mlfo1: 'Global LFO 1', mlfo2: 'Global LFO 2', mseg1: 'Global env 1', mseg2: 'Global env 2', vmseg: 'Note env', vlfo: 'Note LFO',
  mod_wheel: 'Mod wheel', pitch_bend: 'Pitch bend', wander: 'Wander' };
export const srcName = (l) => (!l || l === 'none' ? '—' : LABEL[l] ?? l.replace(/^macro(\d)$/, 'Macro $1').replace(/^./, (c) => c.toUpperCase()));

export const parse = (key) => ({ kind: key[0] === 'p' ? 'part' : 'voice', r: +key.slice(1), pre: key[0] === 'p' ? 'route' : 'vroute' });
export const keys = (kind) => Array.from({ length: kind === 'part' ? 8 : 4 }, (_, i) => (kind === 'part' ? 'p' : 'v') + (i + 1));
export const srcLabels = (s, kind) => s.byName[(kind === 'part' ? 'route' : 'vroute') + '1_source']?.labels ?? [];

export function get(s, key) {
  const { kind, r, pre } = parse(key);
  const src = Math.round(s.getP(`${pre}${r}_source`)), dst = Math.round(s.getP(`${pre}${r}_destination`));
  return { key, kind, r, pre, src, srcLabel: srcLabels(s, kind)[src], dst, amount: s.getP(`${pre}${r}_amount`),
           via: kind === 'part' ? Math.round(s.getP(`${pre}${r}_via`)) : 0 };
}
// the destination's name
export function dstName(s, x) {
  if (x.kind === 'voice') return human(s.byName.vroute1_destination?.labels?.[x.dst] ?? '');
  const d = s.paramById(x.dst - 1);
  return d ? human(d.name) : '—';
}
// the amount in the destination's units
export function inUnits(s, x) {
  const a = x.amount, sign = (v) => (v >= 0 ? '+' : '');
  if (x.kind === 'voice') { // per-note destinations: pitch +/-12 st, level +/-24 dB, pan, brightness +/-4 oct, timbre
    if (x.dst === 0) return Math.abs(a * 12) < 1 ? `${sign(a)}${(a * 1200).toFixed(0)} ct` : `${sign(a)}${(a * 12).toFixed(2)} st`;
    if (x.dst === 1) return `${sign(a)}${(a * 24).toFixed(1)} dB`;
    if (x.dst === 3) return `${sign(a)}${(a * 4).toFixed(2)} oct`;
    return `${sign(a)}${(a * 100).toFixed(0)} %`;
  }
  const d = s.paramById(x.dst - 1);
  if (!d) return '';
  const v = d.curve === 'exp' ? a * Math.log2(d.max / d.min) : a * (d.max - d.min);
  return `${sign(v)}${+v.toPrecision(3)} ${d.curve === 'exp' ? 'oct' : d.unit || ''}`.trim();
}
export const inUse = (s, key) => get(s, key).src > 0;
export const free = (s, kind) => keys(kind).find((k) => !inUse(s, k));

export function setSource(s, key, v) {
  const { kind, r, pre } = parse(key);
  if (!v) return clear(s, key);
  s.setP(`${pre}${r}_source`, v);
  const dests = s.macroTargets.engine;
  if (kind === 'part' && !Math.round(s.getP(`${pre}${r}_destination`)) && dests.length) s.setP(`${pre}${r}_destination`, dests[0].id + 1);
}
export function clear(s, key) {
  const { kind, r, pre } = parse(key);
  s.setP(`${pre}${r}_amount`, 0);
  s.setP(`${pre}${r}_source`, 0);
  if (kind === 'part') { s.setP(`${pre}${r}_destination`, 0); s.setP(`${pre}${r}_via`, 0); }
}
// a new route from a source (by its name): the first free slot of its kind, or null
export function addFrom(s, kind, srcName_) {
  const k = free(s, kind);
  if (!k) return null;
  const i = srcLabels(s, kind).indexOf(srcName_);
  if (i > 0) setSource(s, k, i);
  return k;
}
