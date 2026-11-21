<!-- Param.svelte — one parameter from the descriptor table, as a row: its label, its control, its value in its
     unit. A thin slider in normalized space (exp curves feel right), a switch for off / on, drawn buttons for a
     choice of waveforms, pills for a few choices, a menu for more. A dot after the label: moved since the sound was loaded (`base`); double-click on the
     label or the slider puts that value back. The descriptor's help text is the tooltip. -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { isWave, waveIcon } from './waves.js';
  let { d, label, value, base = d.def, onchange } = $props();
  const norm = $derived(toNorm(d, value));
  const changed = $derived(Math.abs(value - base) > 1e-6);
  const idx = $derived(Math.round(value - d.min));
  // a choice of waveforms: drawn buttons
  const waves = $derived(!!d.labels && /(^|_)wave$/.test(d.name) && d.labels.every(isWave));
  const toggle = $derived(d.labels?.length === 2 && d.labels[0] === 'off' && d.labels[1] === 'on');
</script>

<div class="param" class:changed title={d.help ?? ''}>
  <span class="label" role="button" tabindex="-1" title={changed ? 'moved · double-click: back to the sound\'s value' : ''} ondblclick={() => onchange(base)}>{label}</span>
  {#if toggle}
    <span class="switch-cell"><button class="onoff" class:on={idx === 1} role="switch" aria-checked={idx === 1} aria-label={label}
            onclick={() => onchange(d.min + (idx === 1 ? 0 : 1))}></button></span>
  {:else if waves}
    <div class="waves" role="group" aria-label={label}>
      {#each d.labels as l, i}
        <button class:on={i === idx} onclick={() => onchange(d.min + i)} title={l} aria-label={l} aria-pressed={i === idx}>
          <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d={waveIcon(l)} fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round" /></svg>
        </button>
      {/each}
    </div>
  {:else if d.labels && d.labels.length <= 4}
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
           oninput={(e) => onchange(fromNorm(d, +e.target.value))} ondblclick={() => onchange(base)} />
    <span class="val">{format(d, value)}</span>
  {/if}
</div>
