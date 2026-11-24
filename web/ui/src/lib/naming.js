// naming.js — how parameter names read in the interface ("osc1_wave" -> group "Osc 1", label "Wave"), and how
// parameters gather into sections: by name prefix, or by the stages of the signal chain an engine describes.

const WORDS = { chat: 'Closed hat', ohat: 'Open hat', ltom: 'Low tom', mtom: 'Mid tom', htom: 'High tom', lfo: 'LFO', mseg: 'MSEG', fenv: 'Filter env', penv: 'Pitch env', amp: 'Amp', env: 'Env', fx: 'FX', vel: 'Velocity', op: 'Op', bpm: 'BPM', pw: 'PW', pwm: 'PWM', fm: 'FM', db: 'dB', mpe: 'MPE', cc: 'CC', a4: 'A4' };
export const cap = (s) => (s ? s.charAt(0).toUpperCase() + s.slice(1) : '');
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

// a parameter's label inside its stage's card: without the words the card's title already says ("reverb_mix" in
// Reverb -> "Mix", "osc2_level" in Osc 2 -> "Level", "amp_attack" in Amp -> "Attack")
export function inStage(name, st) {
  const title = st.label.toLowerCase().replace(/\s+/g, '');
  // a prefix is dropped only when it is the card's own name ("osc1_" in Osc 1, "reverb_" in Reverb): a stage may hold
  // another part's parameters ("supersaw_" in Osc 1), which keep their full name
  const pre = st.params.split(' ').find((t) => t.endsWith('_') && name.startsWith(t) && ((p) => title.startsWith(p) || title.endsWith(p))(t.slice(0, -1).replace(/_/g, '')));
  let rest = pre ? name.slice(pre.length) : name;
  const first = rest.split('_')[0];
  if (rest.includes('_') && (title.startsWith(first) || first.startsWith(title))) rest = rest.slice(first.length + 1);
  return human(rest || name);
}

// [label, [{d, label}], stage]: one section per stage of the signal chain, in its order; parameters no stage
// claims go to "Other" (a stage's params: names, or prefixes ending in '_'); `rest` false leaves them out
const stageMatch = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
export function byStage(stages, params, rest = true) {
  const used = new Set(), out = [];
  for (const st of stages) {
    const ps = params.filter((d) => !used.has(d.id) && stageMatch(st.params, d.name));
    ps.forEach((d) => used.add(d.id));
    if (ps.length) out.push([st.label, ps.map((d) => ({ d, label: inStage(d.name, st) })), st]);
  }
  const left = params.filter((d) => !used.has(d.id));
  if (rest && left.length) out.push(['Other', left.map((d) => ({ d, label: human(d.name) })), null]);
  return out;
}

// the shared parameters the effect stages do not claim (tempo, clock, tuning, concert pitch): the Settings'
export function unclaimed(stages, params) {
  return params.filter((d) => !stages.some((st) => stageMatch(st.params, d.name)));
}
