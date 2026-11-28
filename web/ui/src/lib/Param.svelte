<!-- Param.svelte — one parameter from the descriptor table, as a row: its label, its control, its value in its
     unit. A thin slider in normalized space (exp curves feel right), a switch for off / on, drawn buttons for a
     choice of waveforms, pills for a few choices, a menu for more, sixteen step buttons for a rhythm pattern, a row
     of named toggles for a mask (what a gate LFO opens: MASKS). Its value, the sound's value (`base`) and the setter come from the session unless given. A dot after the label:
     moved since the sound was loaded; double-click on the
     label or the slider puts that value back. The descriptor's help text is the tooltip.
     A slider is exact to set (slider.js): Shift while dragging is ten times finer, the arrows step it, and a
     double-click on the value types it (Enter sets it, Escape leaves it). A macro dragged from the Live player drops on it to move it. -->
<script>
  import { fromNorm, toNorm, format, MACRO_DRAG } from './synth.js';
  import { isWave, waveIcon } from './waves.js';
  import Seg from './Seg.svelte';
  import Pills from './Pills.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import { session } from './session.svelte.js';
  import { precise, parseValue } from './slider.js';
  import { MASKS } from './engineui.js';
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
  // a rhythm of 16 steps kept as a number (bit k = step k + 1): drawn as steps
  const steps = $derived(/(^|_)pattern$/.test(d.name) && d.min === 0 && d.max === 65535);
  const bits = $derived(Math.round(value));
  // its length: the steps of the same name ("pulse1_pattern" -> "pulse1_steps"); the steps past it are dimmed
  const used = $derived(steps ? Math.round(session.byName[d.name.replace(/pattern$/, 'steps')] ? session.getP(d.name.replace(/pattern$/, 'steps')) : 16) : 16);
  // a mask kept as a number: its bits by name, each a toggle
  const mask = $derived(Object.entries(MASKS).find(([re]) => new RegExp(re).test(d.name))?.[1] ?? null);
  let typing = $state(false); // the value is being typed
  let over = $state(false); // a macro dragged over it
  function typed(e) {
    if (e.key === 'Escape') { e.stopPropagation(); typing = false; return; }
    if (e.key !== 'Enter') return;
    const v = parseValue(d, e.target.value);
    if (v !== null) onchange(v);
    typing = false;
  }
  const focus = (node) => { node.focus(); node.select(); };
</script>

<div class="param" class:changed class:droppable={session.macroDrag >= 0 && session.macroTargetIds.has(d.id)} class:dropover={over} title={d.help ?? ''}
     role="group" aria-label={label}
     ondragover={(e) => { if (e.dataTransfer.types.includes(MACRO_DRAG) && session.macroTargetIds.has(d.id)) { e.preventDefault(); e.dataTransfer.dropEffect = 'link'; over = true; } }}
     ondragleave={() => (over = false)}
     ondrop={(e) => { over = false; const k = +e.dataTransfer.getData(MACRO_DRAG); if (e.dataTransfer.types.includes(MACRO_DRAG)) { e.preventDefault(); session.assignMacro(k, d.id); session.macroDrag = -1; } }}>
  <span class="label" role="button" tabindex="-1" title={changed ? 'moved · double-click: back to the sound\'s value' : ''} ondblclick={() => onchange(base)}>{label}</span>
  {#if steps}
    <div class="cell steps" role="group" aria-label={label}>
      {#each Array(16) as _, k}
        <button class:on={(bits >> k) & 1} class:past={k >= used} aria-pressed={!!((bits >> k) & 1)} aria-label="Step {k + 1}" onclick={() => onchange(bits ^ (1 << k))}></button>
      {/each}
    </div>
  {:else if mask}
    <div class="cell masks" role="group" aria-label={label}>
      {#each mask as name, k}
        <button class:on={(bits >> k) & 1} aria-pressed={!!((bits >> k) & 1)} onclick={() => onchange(bits ^ (1 << k))}>{name}</button>
      {/each}
    </div>
  {:else if toggle}
    <span class="cell"><Switch on={idx === 1} onchange={(v) => onchange(d.min + (v ? 1 : 0))} name={label} /></span>
  {:else if waves}
    <div class="cell"><Pills items={d.labels.map((l, i) => ({ id: i, label: l, draw: waveIcon(l) }))} value={idx} onpick={(i) => onchange(d.min + i)} size="icon" {label} /></div>
  {:else if d.labels && d.labels.length <= 4}
    <div class="cell"><Seg items={d.labels.map((l, i) => ({ id: i, label: l }))} value={idx} onpick={(i) => onchange(d.min + i)} wide {label} /></div>
  {:else if d.labels}
    <Select {label} value={idx} onchange={(i) => onchange(d.min + i)} options={d.labels.map((l, i) => ({ value: i, label: l }))} />
  {:else}
    <input type="range" aria-label={label} aria-valuetext={format(d, value)} min="0" max="1" step="0.001" value={norm} style="--p:{norm * 100}%"
           use:precise={{ value: () => norm, set: (n) => onchange(fromNorm(d, n)), step: d.curve === 'int' ? 1 / (d.max - d.min || 1) : undefined }}
           ondblclick={() => onchange(base)} />
    {#if typing}
      <input class="val mono" type="text" value={format(d, value)} aria-label="{label}: type a value" use:focus onkeydown={typed} onblur={() => (typing = false)} />
    {:else}
      <span class="val editable" role="button" tabindex="-1" title="double-click: type a value" ondblclick={() => (typing = true)}>{format(d, value)}</span>
    {/if}
  {/if}
</div>
