// stages.js — the stages of the sound as rows: the instrument's (from its stage descriptors, in the order its signal
// flows) and the effect chain's (dsp/synth.c synth_fx_stages), each with its parameters, a role, a one-line summary
// of its main settings and its on / off switch. Shared by the tables of the Synth view and the side panel.
import { byStage, grouped } from './naming.js';
import { format } from './synth.js';
import { PART_PARAM } from './synth.js';
import { ENGINE_UI } from './engineui.js';

export const ROLE = { source: 'Source', filter: 'Filter', amp: 'Amp', mod: 'Modulator', voice: 'Voice', fx: 'Effect' };
const RANK = { source: 0, filter: 1, amp: 2, mod: 3, voice: 4 };

// the instrument's stages: [{key, title, ps: [{d, label}], st}] (st null for an engine without stage descriptors)
export function engineStages(s) {
  const e = s.engine;
  if (!e) return [];
  if (!e.stages?.length) return grouped(e.params).map(([title, ps]) => ({ key: 'g:' + title, title, ps, st: null }));
  return byStage(e.stages, e.params).map(([title, ps, st], i) => ({ key: st?.id ?? 'g:' + title, title, ps, st, i }))
    .sort((a, b) => (RANK[a.st?.kind] ?? 5) - (RANK[b.st?.kind] ?? 5) || a.i - b.i);
}
// the effect chain's stages, in the order the signal goes through them now
export function fxStages(s) {
  const shared = s.shared.filter((d) => d.id < PART_PARAM);
  return byStage(s.fxStages, shared, false).map(([title, ps, st]) => ({ key: st.id, title, ps, st }));
}
// a stage of either, by key
export const stageByKey = (s, key) => [...engineStages(s), ...fxStages(s)].find((x) => x.key === key);

// how an engine presents a parameter (engineui.js): 'main', 'more' or 'hide', and its label
export const placeOf = (s, d) => ENGINE_UI[s.engine?.name]?.place?.(d, s) ?? 'main';
export const labelOf = (s, d, label) => ENGINE_UI[s.engine?.name]?.label?.(d, s) ?? label;

// the first main settings of a stage, as one line ("Mix 0.25 · Cutoff 1.2 kHz · Resonance 40 %"); its switch is left
// out when it is only on / off (a mix or a level that switches it says how much)
export function summary(s, row, n = 3, engine = true) {
  const onoff = (d) => d.name === row.st?.enable && (d.labels || d.curve === 'int');
  const ps = row.ps.filter(({ d }) => !onoff(d) && (!engine || placeOf(s, d) === 'main'));
  return ps.slice(0, n).map(({ d, label }) => `${engine ? labelOf(s, d, label) : label} ${format(d, s.getP(d.name))}`).join(' · ');
}

// a stage's switch: on when its enabling parameter is away from its off value; off remembers the value to come back to
const lastOn = {};
export const stageOn = (s, st) => !st?.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
export function toggleStage(s, st) {
  if (stageOn(s, st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
  else s.setP(st.enable, lastOn[st.enable] ?? st.on);
}
