<!-- EffectChain.svelte — the effects as one line, in the order the sound goes through them: the fixed stages before
     the rack (output filter, crush), the rack's effects (rack.svelte.js), then the fixed ones after it (master,
     saturation, limiter). An effect is in the chain or not: its switch bypasses it without taking it out, a drag moves
     it, × takes it out, and "+ Effect" puts one at the end of the rack (a drag then places it). A click opens it in the
     side panel. -->
<script>
  import Icon from './Icon.svelte';
  import Select from './Select.svelte';
  import Switch from './Switch.svelte';
  import { session as s } from './session.svelte.js';
  import { fxStages, stageOn, toggleStage } from './stages.js';
  import { rack } from './rack.svelte.js';

  const all = $derived(fxStages(s));
  $effect(() => rack.sync());
  const firstUnit = $derived(all.findIndex((r) => s.rack.includes(r.key)));
  const before = $derived(all.filter((r, i) => !s.rack.includes(r.key) && i < firstUnit));
  const after = $derived(all.filter((r, i) => !s.rack.includes(r.key) && i > firstUnit));
  const used = $derived(rack.used().map((id) => all.find((r) => r.key === id)).filter(Boolean));
  const addOptions = $derived([{ value: '', label: '+ Effect' }, ...rack.free().map((id) => ({ value: id, label: all.find((r) => r.key === id)?.title ?? id }))]);
  let addPick = $state('');
  function addEffect(id) { addPick = ''; if (!id) return; rack.add(id); s.openPanel('stage', id); }

  // drag to reorder the rack's effects
  let dragged = $state(null), over = $state(null);
  function drop(target) {
    const from = dragged;
    dragged = over = null;
    if (!from || from === target) return;
    const order = rack.used().filter((x) => x !== from);
    order.splice(target === '$end' ? order.length : order.indexOf(target), 0, from);
    s.setRack([...order, ...s.rack.filter((x) => !order.includes(x))]);
  }
  const open = (r) => s.openPanel('stage', r.key);
</script>

{#snippet chip(r, movable)}
  <div class="chip" class:off={!stageOn(s, r.st)} class:sel={s.panel?.id === r.key} class:over={over === r.key && dragged !== r.key}
       draggable={movable} role="listitem"
       ondragstart={(e) => { dragged = r.key; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', r.key); }}
       ondragover={(e) => { if (movable && dragged) { e.preventDefault(); over = r.key; } }}
       ondragleave={() => over === r.key && (over = null)}
       ondrop={(e) => { e.preventDefault(); drop(r.key); }} ondragend={() => (dragged = over = null)}>
    {#if movable}<span class="grip" title="Drag to move it in the chain"><Icon name="grip" size={11} /></span>{/if}
    {#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" title={stageOn(s, r.st) ? 'on (click: bypass)' : 'bypassed'} />
    {:else}<span class="lock" title="always on"><Icon name="lock" size={11} /></span>{/if}
    <button class="name" onclick={() => open(r)} title="Open {r.title}">{r.title}</button>
    {#if movable}<button class="x" onclick={() => rack.remove(r.key)} aria-label="Take {r.title} out of the chain" title="Take it out of the chain"><Icon name="close" size={9} /></button>{/if}
  </div>
{/snippet}

<section class="fxchain" aria-label="Effect chain">
  <span class="lbl">Effects</span>
  <div class="line" role="list">
    {#each before as r (r.key)}{@render chip(r, false)}<span class="arrow">→</span>{/each}
    {#each used as r (r.key)}{@render chip(r, true)}<span class="arrow">→</span>{/each}
    <span class="add" class:over={over === '$end'} role="presentation"
          ondragover={(e) => { if (dragged) { e.preventDefault(); over = '$end'; } }} ondrop={(e) => { e.preventDefault(); drop('$end'); }}>
      <Select class="tool addfx" bind:value={addPick} options={addOptions} disabled={addOptions.length < 2} label="Add an effect at the end of the rack" onchange={addEffect} />
    </span>
    {#each after as r (r.key)}<span class="arrow">→</span>{@render chip(r, false)}{/each}
  </div>
</section>

<style>
  .fxchain { display: flex; align-items: flex-start; gap: 12px; margin: 0 0 14px; }
  .lbl { flex: none; padding-top: 7px; font-size: 10.5px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
  .line { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
  .arrow { color: var(--muted); font-size: 11px; }
  .chip { box-sizing: border-box; display: inline-flex; align-items: center; gap: 7px; height: 30px; padding: 0 8px; border: 1px solid var(--line);
    border-radius: 7px; background: var(--surface); font-size: 12px; }
  .chip.sel { border-color: var(--accent); box-shadow: 0 0 0 1px var(--accent); }
  .chip.over { box-shadow: -3px 0 0 var(--accent); }
  .chip.off .name { color: var(--muted); text-decoration: line-through; text-decoration-color: color-mix(in srgb, var(--muted) 50%, transparent); }
  .chip[draggable='true'] { cursor: grab; }
  .grip { color: var(--muted); display: inline-grid; }
  .lock { color: var(--muted); display: inline-grid; place-items: center; width: 30px; }
  .name { font-weight: 500; }
  .name:hover { color: var(--accent); }
  .x { color: var(--muted); display: inline-grid; place-items: center; width: 16px; height: 16px; border-radius: 4px; }
  .x:hover { color: var(--text); background: var(--faint); }
  .add.over :global(.select) { box-shadow: -3px 0 0 var(--accent); }
</style>
