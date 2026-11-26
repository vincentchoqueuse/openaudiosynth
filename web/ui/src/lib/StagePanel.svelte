<!-- StagePanel.svelte — the Settings tab of the side panel for a stage of the instrument or of the effect chain: its
     drawing (StageViz: a wave, a filter's response, an envelope, an LFO), every one of its settings as a table
     (setting, control, value; the engine's main ones first, those its present choices do not use hidden: engineui.js),
     then Modulation: what moves its settings (ModLinks). The footer, as in every panel: what can be added on the left
     (a modulation), Reset to defaults, and Remove on the right for an effect of the rack. -->
<script>
  import Param from './Param.svelte';
  import StageViz from './StageViz.svelte';
  import ModLinks from './ModLinks.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { placeOf, labelOf } from './stages.js';
  import { rack } from './rack.svelte.js';
  let { row, engine = true } = $props(); // a row of stages.js; engine: the instrument's (its own presentation)

  const place = (d) => (engine ? placeOf(s, d) : 'main');
  const named = (ps) => ps.map(({ d, label }) => ({ d, label: engine ? labelOf(s, d, label) : label }));
  const shown = $derived(named([...row.ps.filter(({ d }) => place(d) === 'main'), ...row.ps.filter(({ d }) => place(d) === 'more')]));
  const moved = $derived(row.ps.some(({ d }) => Math.abs((s.values[d.id] ?? d.def) - d.def) > 1e-9));
  const reset = () => { for (const { d } of row.ps) if (Math.abs((s.values[d.id] ?? d.def) - d.def) > 1e-9) s.setValue(d, d.def); };
  let links = $state();
</script>

{#if row.st}<div class="sp-viz"><StageViz st={row.st} params={row.ps.map((p) => p.d)} get={s.getP} /></div>{/if}
<div class="ptable">
  <div class="ptable-head"><span>Setting</span><span>Value</span></div>
  {#each shown as { d, label } (d.id)}<Param {d} {label} />{/each}
</div>
<h3 class="sub">Modulation</h3>
<ModLinks bind:this={links} params={shown} />
<div class="sp-foot">
  <button class="btn ghost" onclick={() => links.add()} disabled={!links?.canAdd()}><Icon name="plus" size={12} /> Add modulation</button>
  <button class="btn ghost" onclick={reset} disabled={!moved} title="Every setting of this module back to its default">Reset to defaults</button>
  {#if rack.has(row.key)}<button class="btn ghost danger" onclick={() => rack.remove(row.key)}>Remove</button>{/if}
</div>
