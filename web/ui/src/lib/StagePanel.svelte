<!-- StagePanel.svelte — the Settings tab of the side panel for a stage of the instrument or of the effect chain: its
     drawing (StageViz: a wave, a filter's response, an envelope, an LFO; a wavetable oscillator its table: WavetableView), every one of its settings as a table
     (setting, control, value; the engine's main ones first, those its present choices do not use hidden: engineui.js),
     then, for one of the instrument's modulators, Moves: what it moves (its slots of the instrument's matrix:
     RouteList, or the stages it acts on), and Moved by: what moves its settings (ModLinks), each section with its
     own "+ Add". An effect of the rack opens on a menu that puts another effect in its slot. The footer: Reset to
     defaults, and Remove for an effect of the rack. -->
<script>
  import Param from './Param.svelte';
  import StageViz, { vizKind } from './StageViz.svelte';
  import ModLinks from './ModLinks.svelte';
  import RouteList from './RouteList.svelte';
  import * as M from './matrix.js';
  import WavetableView from './WavetableView.svelte';
  import Section from './Section.svelte';
  import { session as s } from './session.svelte.js';
  import { placeOf, labelOf, fxStages } from './stages.js';
  import Select from './Select.svelte';
  import { rack } from './rack.svelte.js';
  let { row, engine = true, about = null } = $props(); // a row of stages.js; engine: the instrument's (its own presentation)

  const place = (d) => (engine ? placeOf(s, d) : 'main');
  const named = (ps) => ps.map(({ d, label }) => ({ d, label: engine ? labelOf(s, d, label) : label }));
  // an effect's dry / wet first, named so
  const MIX = /^(phaser|flanger|chorus|delay|reverb|tape|drive)_mix$/;
  // then, for the reverb, its kind and space before its tail; the rest in the table's order
  const ORDER = ['reverb_type', 'reverb_size', 'reverb_decay', 'reverb_predelay', 'reverb_damping', 'reverb_lowcut', 'reverb_width'];
  const LABEL = { reverb_lowcut: 'Low cut' };
  const rank = (d) => (MIX.test(d.name) ? -1 : ORDER.includes(d.name) ? ORDER.indexOf(d.name) : ORDER.length);
  const mixFirst = (ps) => ps.map((p, i) => [p, i]).sort(([a, i], [b, j]) => rank(a.d) - rank(b.d) || i - j)
    .map(([p]) => ({ ...p, label: MIX.test(p.d.name) ? 'Dry / wet' : LABEL[p.d.name] ?? p.label }));
  const shown = $derived(mixFirst(named([...row.ps.filter(({ d }) => place(d) === 'main'), ...row.ps.filter(({ d }) => place(d) === 'more')])));
  const moved = $derived(row.ps.some(({ d }) => Math.abs((s.values[d.id] ?? d.def) - d.def) > 1e-9));
  const reset = () => { for (const { d } of row.ps) if (Math.abs((s.values[d.id] ?? d.def) - d.def) > 1e-9) s.setValue(d, d.def); };
  // what an instrument's modulator moves: its slots of the matrix, or the stages it is wired to
  const msrc = $derived(engine && row.st ? M.srcOfStage(s, row.st.id) : null);
  const acts = $derived(engine && row.st?.kind === 'mod' && !msrc
    ? (row.st.targets ?? '').split(' ').filter(Boolean).map((id) => s.engine?.stages?.find((x) => x.id === id)).filter(Boolean) : []);
  // an effect of the rack: the others not in the rack can take its place (same slot of the chain)
  const slotOptions = $derived([row.key, ...rack.free()].map((id) => ({ value: id, label: fxStages(s).find((r) => r.key === id)?.title ?? id })));
</script>

{#if rack.has(row.key)}
  <Section icon="slot" title="Effect">
    <div class="slotpick"><span class="muted">In this place of the chain</span>
      <Select value={row.key} options={slotOptions} label="Effect in this place" onchange={(v) => rack.replace(row.key, v)} /></div>
  </Section>
{/if}
{#if s.engine?.name === 'wt' && (row.key === 'osca' || row.key === 'oscb')}
  <Section icon="shape" title="Wavetable"><WavetableView osc={row.key === 'osca' ? 0 : 1} prefix={row.key === 'osca' ? 'a_' : 'b_'} /></Section>
{:else if row.st && vizKind(row.st, row.ps.map((p) => p.d), s.getP)}<Section icon="shape" title="Shape"><div class="sp-viz"><StageViz st={row.st} params={row.ps.map((p) => p.d)} get={s.getP} /></div></Section>{/if}
<Section icon="sliders" title="Settings" note="{shown.length}">
  <div class="ptable">{#each shown as { d, label } (d.id)}<Param {d} {label} />{/each}</div>
</Section>
{#if msrc}<Section icon="link" title="Moves"><RouteList {msrc} /></Section>
{:else if acts.length}<Section icon="link" title="Moves">
  <p class="acts">{#each acts as a, i}{#if i}<span class="muted"> · </span>{/if}<button class="linkish" onclick={() => s.editPanel('stage', a.id)}>{a.label}</button>{/each}
    <span class="muted"> (its amounts are in its settings)</span></p></Section>{/if}
<Section icon="modsrc" title="Moved by"><ModLinks params={shown} stage={row.key} /></Section>
{@render about?.()}
<div class="sp-foot">
  <button class="btn ghost" onclick={reset} disabled={!moved} title="Every setting of this module back to its default">Reset to defaults</button>
  {#if rack.has(row.key)}<button class="btn ghost danger" onclick={() => rack.remove(row.key)}>Remove</button>{/if}
</div>

<style>
  .acts { margin: 0; font-size: 12px; }
  .linkish { padding: 0; border: 0; background: none; color: var(--accent); font: inherit; cursor: pointer; }
  .linkish:hover { text-decoration: underline; }
  .slotpick { display: flex; align-items: center; justify-content: space-between; gap: 12px; font-size: 12px; }
</style>
