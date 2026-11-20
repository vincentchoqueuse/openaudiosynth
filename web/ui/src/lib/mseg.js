// mseg.js — multi-segment envelope math for the editor, mirroring dsp/env/mseg.c: segment shape
// y = (2^(a x) - 1) / (2^a - 1) with a = -8 curve, its inverse at the midpoint (to drag a curve handle),
// and reading / writing a whole shape from / to the flat parameters (prefix + t<i>, l<i>, c<i>, points...).

export const MAX_POINTS = 16;
export const MIN_TIME = 0.001; // MSEG_MIN_TIME
export const MAX_TIME = 32;
const clamp = (x, a, b) => Math.min(b, Math.max(a, x));

export function shape(x, curve) {
  const a = -8 * clamp(curve, -1, 1);
  if (Math.abs(a) < 1e-3) return x;
  return (2 ** (a * x) - 1) / (2 ** a - 1);
}

// curve whose shape passes through s at x = 0.5 (shape(0.5) = 1 / (2^(a/2) + 1))
export function curveThroughMid(s) {
  s = clamp(s, 0.06, 0.94);
  return clamp(-(2 * Math.log2(1 / s - 1)) / 8, -1, 1);
}

// {t, l, c} per point (t = time from the previous point; point 0's time is unused), markers as 0-based
// indices or -1, mode 0..2, sync 0 (seconds) / 1 (beats)
export function readShape(get, prefix) {
  const n = clamp(Math.round(get(prefix + 'points')), 2, MAX_POINTS);
  const pts = [];
  for (let i = 0; i < n; i++)
    pts.push({ t: i ? get(prefix + 't' + (i + 1)) : 0, l: get(prefix + 'l' + (i + 1)), c: get(prefix + 'c' + (i + 1)) });
  const marker = (k) => Math.round(get(prefix + k)) - 1; // labels: none, 1, 2... -> -1, 0, 1...
  return {
    pts, sustain: marker('sustain'), loopStart: marker('loop_start'), loopEnd: marker('loop_end'),
    mode: Math.round(get(prefix + 'mode')), sync: Math.round(get(prefix + 'sync')),
  };
}

// writes only what differs from `old` (each write is one message to the worklet)
export function writeShape(set, prefix, s, old) {
  const w = (k, v, o) => { if (o === undefined || Math.abs(v - o) > 1e-7) set(prefix + k, v); };
  s.pts.forEach((p, i) => {
    const o = old?.pts[i];
    if (i) w('t' + (i + 1), clamp(p.t, MIN_TIME, MAX_TIME), o?.t);
    w('l' + (i + 1), clamp(p.l, 0, 1), o?.l);
    w('c' + (i + 1), clamp(p.c, -1, 1), o?.c);
  });
  w('points', s.pts.length, old?.pts.length);
  w('sustain', s.sustain + 1, old ? old.sustain + 1 : undefined);
  w('loop_start', s.loopStart + 1, old ? old.loopStart + 1 : undefined);
  w('loop_end', s.loopEnd + 1, old ? old.loopEnd + 1 : undefined);
  w('mode', s.mode, old?.mode);
  w('sync', s.sync, old?.sync);
}

export const clone = (s) => ({ ...s, pts: s.pts.map((p) => ({ ...p })) });

// cumulative time of each point
export function times(pts) {
  const x = [0];
  for (let i = 1; i < pts.length; i++) x.push(x[i - 1] + pts[i].t);
  return x;
}

// inserts a point at time tx and level l; markers after it move with their points
export function insertPoint(s, tx, l) {
  if (s.pts.length >= MAX_POINTS) return s;
  const x = times(s.pts), out = clone(s);
  let k = x.findIndex((v) => v >= tx); // new point goes before point k
  if (k < 0) { // after the end
    out.pts.push({ t: Math.max(MIN_TIME, tx - x[x.length - 1]), l, c: 0 });
    return out;
  }
  if (k === 0) k = 1;
  const before = tx - x[k - 1];
  out.pts.splice(k, 0, { t: Math.max(MIN_TIME, before), l, c: s.pts[k].c });
  out.pts[k + 1].t = Math.max(MIN_TIME, s.pts[k].t - before);
  for (const m of ['sustain', 'loopStart', 'loopEnd']) if (out[m] >= k) out[m]++;
  return out;
}

// removes point k (not the first; at least 2 points stay); the next point keeps its absolute time
export function deletePoint(s, k) {
  if (k <= 0 || s.pts.length <= 2) return s;
  const out = clone(s);
  if (k + 1 < out.pts.length) out.pts[k + 1].t += out.pts[k].t;
  out.pts.splice(k, 1);
  for (const m of ['sustain', 'loopStart', 'loopEnd']) {
    if (out[m] === k) out[m] = -1;
    else if (out[m] > k) out[m]--;
  }
  if (out.loopEnd <= out.loopStart) out.loopStart = out.loopEnd = -1;
  return out;
}

// a few starting shapes: (time unit) -> points and markers
export const PRESETS = {
  'Swell': () => ({ pts: [[0, 0, 0], [2, 1, -0.5], [1, 0.7, 0], [3, 0, 0.4]], sustain: 2 }),
  'Pluck': () => ({ pts: [[0, 1, 0], [0.4, 0.2, 0.7], [2, 0, 0.5]], sustain: -1 }),
  'Breath loop': () => ({ pts: [[0, 0, 0], [3, 1, -0.5], [4, 0.2, 0.4], [3, 0.8, -0.3], [4, 0, 0.3]], loop: [0, 4], mode: 2 }),
  'Ramp loop': () => ({ pts: [[0, 0, 0], [4, 1, 0], [0.05, 0, 0]], loop: [0, 2], mode: 2 }),
  'Random steps': () => {
    const pts = [[0, Math.random(), 0]];
    for (let i = 0; i < 8; i++) pts.push([0.02, Math.random(), 0], [1, pts[pts.length - 1][1], 0]);
    return { pts: pts.slice(0, MAX_POINTS), loop: [0, MAX_POINTS - 1], mode: 2 };
  },
};

export function fromPreset(name, s) {
  const p = PRESETS[name]();
  return {
    ...s,
    pts: p.pts.map(([t, l, c]) => ({ t, l, c })),
    sustain: p.sustain ?? -1,
    loopStart: p.loop ? p.loop[0] : -1,
    loopEnd: p.loop ? p.loop[1] : -1,
    mode: p.mode ?? 0,
  };
}
