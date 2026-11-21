// naming.js — how parameter names read in the interface ("osc1_wave" -> group "Osc 1", label "Wave"), and how
// parameters gather into sections: by name prefix, or by the stages of the signal chain an engine describes.

const WORDS = { lfo: 'LFO', mseg: 'MSEG', fenv: 'Filter env', penv: 'Pitch env', amp: 'Amp', env: 'Env', fx: 'FX', vel: 'Velocity', op: 'Op', bpm: 'BPM', pw: 'PW', pwm: 'PWM', fm: 'FM', db: 'dB' };
const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
const word = (w) => WORDS[w] ?? w.replace(/^([a-z]+)(\d+)$/, (_, a, n) => (WORDS[a] ?? cap(a)) + ' ' + n);
export const human = (s) => cap(s.split('_').map(word).join(' '));

// [group, [{d, label}]] by name prefix ("osc1_wave", "osc1_level" -> "Osc 1"); single ones go to "Main", first
export function grouped(params) {
  const count = {};
  for (const d of params) { const p = d.name.split('_')[0]; count[p] = (count[p] || 0) + 1; }
  const out = new Map();
  for (const d of params) {
    const parts = d.name.split('_'), p = parts[0], own = count[p] > 1 && parts.length > 1;
    const g = own ? human(p) : 'Main';
    if (!out.has(g)) out.set(g, []);
    out.get(g).push({ d, label: own ? human(parts.slice(1).join('_')) : human(d.name) });
  }
  return [...out].sort((a, b) => (a[0] === 'Main' ? -1 : b[0] === 'Main' ? 1 : 0));
}

// [label, [{d, label}], stage]: one section per stage of the signal chain, in its order; parameters no stage
// claims go to "Other" (a stage's params: names, or prefixes ending in '_')
const stageMatch = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
export function byStage(stages, params) {
  const used = new Set(), out = [];
  for (const st of stages) {
    const ps = params.filter((d) => !used.has(d.id) && stageMatch(st.params, d.name));
    ps.forEach((d) => used.add(d.id));
    if (ps.length) out.push([st.label, ps.map((d) => ({ d, label: human(d.name) })), st]);
  }
  const rest = params.filter((d) => !used.has(d.id));
  if (rest.length) out.push(['Other', rest.map((d) => ({ d, label: human(d.name) })), null]);
  return out;
}
