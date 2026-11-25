<!-- StagePanel.svelte — the side panel's body for a stage of the instrument or of the effect chain: its drawing
     (StageViz: a wave, a filter's response, an envelope, an LFO) and every one of its settings, all in view (the
     engine's main ones first, then the others; those its present choices do not use are hidden: engineui.js). -->
<script>
  import Param from './Param.svelte';
  import StageViz from './StageViz.svelte';
  import { session as s } from './session.svelte.js';
  import { placeOf, labelOf } from './stages.js';
  let { row, engine = true } = $props(); // a row of stages.js; engine: the instrument's (its own presentation)

  const place = (d) => (engine ? placeOf(s, d) : 'main');
  const shown = $derived([...row.ps.filter(({ d }) => place(d) === 'main'), ...row.ps.filter(({ d }) => place(d) === 'more')]);
</script>

{#if row.st}<div class="sp-viz"><StageViz st={row.st} params={row.ps.map((p) => p.d)} get={s.getP} /></div>{/if}
{#each shown as { d, label } (d.id)}<Param {d} label={engine ? labelOf(s, d, label) : label} />{/each}
