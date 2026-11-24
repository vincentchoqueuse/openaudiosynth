// mods.js — the modulators of a sound, in the order of their tabs in the Synth view's Modulation pill:
// [label, parameter prefix, route source name, kind ('part' | 'voice': per note), editor ('env' | 'lfo')].
import { session as s } from './session.svelte.js';

export const MODS = [
  ['Global env 1', 'mseg1_', 'mseg1', 'part', 'env'], ['Global env 2', 'mseg2_', 'mseg2', 'part', 'env'], ['Note env', 'vmseg_', 'vmseg', 'voice', 'env'],
  ['Global LFO 1', 'mlfo1_', 'mlfo1', 'part', 'lfo'], ['Global LFO 2', 'mlfo2_', 'mlfo2', 'part', 'lfo'], ['Note LFO', 'vlfo_', 'vlfo', 'voice', 'lfo'],
];

// does a modulator drive anything? (a route from it with an amount)
export function drives(src, kind) {
  const pre = kind === 'part' ? 'route' : 'vroute', n = kind === 'part' ? 8 : 4;
  const labels = s.byName[pre + '1_source']?.labels ?? [];
  for (let r = 1; r <= n; r++) if (labels[Math.round(s.getP(`${pre}${r}_source`))] === src && s.getP(`${pre}${r}_amount`) !== 0) return true;
  return false;
}
