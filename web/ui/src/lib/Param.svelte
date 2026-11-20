<!-- Param.svelte — one parameter from the descriptor table: a thin slider in normalized space (so exp
     curves feel right) with the physical value, or a segmented / menu choice for enumerations.
     Double-click resets to the default. The descriptor's help text is the tooltip. -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  let { d, label, value, onchange } = $props();
  const norm = $derived(toNorm(d, value));
  const changed = $derived(Math.abs(value - d.def) > 1e-6);
  const idx = $derived(Math.round(value - d.min));
</script>

<div class="param" class:changed title={d.help ?? ''}>
  <span class="label" role="button" tabindex="-1" title="double-click: default value" ondblclick={() => onchange(d.def)}>{label}</span>
  {#if d.labels && d.labels.length <= 4}
    <div class="seg">
      {#each d.labels as l, i}
        <button class:on={i === idx} onclick={() => onchange(d.min + i)}>{l}</button>
      {/each}
    </div>
  {:else if d.labels}
    <select aria-label={label} value={idx} onchange={(e) => onchange(d.min + +e.target.value)}>
      {#each d.labels as l, i}<option value={i}>{l}</option>{/each}
    </select>
  {:else}
    <input type="range" aria-label={label} aria-valuetext={format(d, value)} min="0" max="1" step="0.001" value={norm} style="--p:{norm * 100}%"
           oninput={(e) => onchange(fromNorm(d, +e.target.value))} ondblclick={() => onchange(d.def)} />
    <span class="val">{format(d, value)}</span>
  {/if}
</div>
