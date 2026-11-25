<!-- StagePanel.svelte — the side panel's body for a stage of the instrument or of the effect chain: its drawing
     (StageViz: a wave, a filter's response, an envelope, an LFO) and every one of its settings, the engine's main ones
     first and the others under More (engineui.js). -->
<script>
  import Param from './Param.svelte';
  import StageViz from './StageViz.svelte';
  import { session as s } from './session.svelte.js';
  import { placeOf, labelOf } from './stages.js';
  let { row, engine = true } = $props(); // a row of stages.js; engine: the instrument's (its own presentation)

  const place = (d) => (engine ? placeOf(s, d) : 'main');
  const main = $derived(row.ps.filter(({ d }) => place(d) === 'main'));
  const more = $derived(row.ps.filter(({ d }) => place(d) === 'more'));
  let open = $state(false);
</script>

{#if row.st}<div class="sp-viz"><StageViz st={row.st} params={row.ps.map((p) => p.d)} get={s.getP} /></div>{/if}
{#each main as { d, label } (d.id)}<Param {d} label={engine ? labelOf(s, d, label) : label} />{/each}
{#if more.length}
  <button class="more" aria-expanded={open} onclick={() => (open = !open)}>{open ? 'Less' : `More (${more.length})`}</button>
  {#if open}{#each more as { d, label } (d.id)}<Param {d} label={labelOf(s, d, label)} />{/each}{/if}
{/if}
