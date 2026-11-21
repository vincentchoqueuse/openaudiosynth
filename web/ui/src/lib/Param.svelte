<!-- Param.svelte — one parameter from the descriptor table, as a row: its label, its control, its value in its
     unit. A thin slider in normalized space (exp curves feel right), a switch for off / on, drawn buttons for a
     choice of waveforms, pills for a few choices, a menu for more. Its value, the sound's value (`base`) and the setter come from the session unless given. A dot after the label:
     moved since the sound was loaded; double-click on the
     label or the slider puts that value back. The descriptor's help text is the tooltip. -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { isWave, waveIcon } from './waves.js';
  import Seg from './Seg.svelte';
  import Pills from './Pills.svelte';
  import Switch from './Switch.svelte';
  import { session } from './session.svelte.js';
  // the value, the sound's value and the setter default to the session's: pass them only for something else
  let { d, label, value: given, base: givenBase, onchange: given_set } = $props();
  const value = $derived(given ?? session.values[d.id] ?? d.def);
  const base = $derived(givenBase ?? session.baseline[d.id] ?? d.def);
  const onchange = (v) => (given_set ?? ((x) => session.setValue(d, x)))(v);
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
    <span class="cell"><Switch on={idx === 1} onchange={(v) => onchange(d.min + (v ? 1 : 0))} name={label} /></span>
  {:else if waves}
    <div class="cell"><Pills items={d.labels.map((l, i) => ({ id: i, label: l, draw: waveIcon(l) }))} value={idx} onpick={(i) => onchange(d.min + i)} size="icon" {label} /></div>
  {:else if d.labels && d.labels.length <= 4}
    <div class="cell"><Seg items={d.labels.map((l, i) => ({ id: i, label: l }))} value={idx} onpick={(i) => onchange(d.min + i)} wide {label} /></div>
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
