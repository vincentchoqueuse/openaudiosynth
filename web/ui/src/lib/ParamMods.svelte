<!-- ParamMods.svelte — under a setting of the side panel's Settings table, what moves it: each route that has it as
     destination (its source and its amount in the setting's units; a click opens the source, where the amount is
     set), and "+ Mod" to add one from any source of the whole sound (a new route at a tenth of the setting's travel,
     in the first free slot). Only for the settings a route can move (those that move live). -->
<script>
  import Select from './Select.svelte';
  import { session as s } from './session.svelte.js';
  import { modRows } from './mods.js';
  import * as R from './routes.js';
  let { d } = $props();

  const can = $derived(s.macroTargets.engine.some((x) => x.id === d.id) || s.macroTargets.fx.some((x) => x.id === d.id));
  const routes = $derived(R.keys('part').map((k) => R.get(s, k)).filter((x) => x.src && x.dst === d.id + 1));
  const labels = $derived(R.srcLabels(s, 'part'));
  const options = $derived([{ value: 0, label: '+ Mod' }, ...labels.map((l, i) => ({ value: i, label: R.srcName(l) })).slice(1)]);
  function add(i) {
    const k = R.free(s, 'part');
    if (!k || !i) return;
    const { r, pre } = R.parse(k);
    s.setP(`${pre}${r}_source`, i);
    s.setP(`${pre}${r}_destination`, d.id + 1);
    s.setP(`${pre}${r}_amount`, 0.1);
  }
  function open(x) {
    const m = modRows().find((m) => m.src === x.srcLabel);
    if (m) s.openPanel('mod', m.key);
    else if (/^macro\d$/.test(x.srcLabel)) s.openPanel('macro', 'm' + (+x.srcLabel.slice(5) - 1));
  }
</script>

{#if can}
  <div class="pmods" class:has={routes.length}>
    {#each routes as x (x.key)}
      <button class="pmod" onclick={() => open(x)} title="Open {R.srcName(x.srcLabel)}">{R.srcName(x.srcLabel)} <span class="mono">{R.inUnits(s, x)}</span></button>
    {/each}
    {#if R.free(s, 'part')}<Select class="pmod-add" value={0} {options} label="Modulate {d.name} from" onchange={add} />{/if}
  </div>
{/if}
