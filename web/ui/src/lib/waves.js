// waves.js — what a waveform looks like, for drawings: one cycle of each wave the engines name (osc and LFO
// choices), phase 0..1 -> -1..1, and a small icon of it (16 x 16 SVG path) for the wave buttons. A picture of the
// choice, not computed by the DSP.
const ALIAS = { triangle: 'tri', saw_down: 'saw', saw_up: 'rsaw', sample_hold: 'sh' };
const KNOWN = new Set(['sine', 'saw', 'rsaw', 'square', 'pulse', 'wide', 'narrow', 'tri', 'shark', 'sh', 'supersaw']);

export const isWave = (name) => KNOWN.has(ALIAS[name] ?? name);

export function wave(name, p, pw = 0.5) {
  switch (ALIAS[name] ?? name) {
    case 'saw': case 'supersaw': return 1 - 2 * p;
    case 'rsaw': return 2 * p - 1;
    case 'square': return p < 0.5 ? 1 : -1;
    case 'pulse': return p < pw ? 1 : -1;
    case 'wide': return p < 0.3 ? 1 : -1;
    case 'narrow': return p < 0.12 ? 1 : -1;
    case 'tri': return 1 - 4 * Math.abs(p - 0.5);
    case 'shark': return p < 0.5 ? -1 + 4 * p * p * 2 * 0.5 : 1 - 2 * (p - 0.5) * 2; // rounded rise, straight fall
    case 'sh': return [0.6, -0.4, 0.9, -0.7][Math.floor(p * 4) % 4];
    default: return Math.sin(2 * Math.PI * p);
  }
}

// shapes drawn by hand (the LFO's fourier and wiener have no single cycle to show; duck: a drop, then the climb back)
const DRAWN = { fourier: 'M2 8c1.5-6 3-6 4 0s2 3.5 3 0 2-5 3-2.5 1.5 4 2 2.5',
  wiener: 'M2 9l1.4-2 1 1.6 1.4-4.2 1.2 2.2 1.4-1 1 3.2 1.4-2.2 1 1.2 2.2-1.4',
  duck: 'M2 3v10c1.5-5 4-8 12-9.5' };

// one cycle and a bit, in a 16 x 16 box with a 2-unit margin; pulse drawn at 30 %, supersaw as two saws
export function waveIcon(name) {
  if (name in DRAWN) return DRAWN[name];
  if (!isWave(name)) return 'M2 8h12';
  const n = ALIAS[name] ?? name, pts = [];
  for (let i = 0; i <= 48; i++) {
    const t = i / 48, ph = (t * 1.25) % 1;
    let v = n === 'pulse' ? wave('pulse', ph, 0.3) : wave(n, ph);
    if (n === 'supersaw') v = (v + wave('saw', (ph * 1.5) % 1)) / 2;
    pts.push([2 + 12 * t, 8 - 5.5 * v]);
  }
  return pts.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(2)},${y.toFixed(2)}`).join(' ');
}
