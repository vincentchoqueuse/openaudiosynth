<!-- EffectChain.svelte — the effects as one line (it scrolls sideways when narrower), each name before its switch as in the table, in the order the sound goes through them: the fixed stages before
     the rack (output filter, crush), a fixed number of places (SLOTS, more only when a sound uses more), then the fixed
     stages after it (master, saturation, limiter). Each place: a menu (None, or an effect not in another place) and a
     button that opens it in the side panel (its dry / wet first: StagePanel). A place dragged onto another moves its
     effect there; None takes the effect out and the places after it move up (rack.svelte.js). -->
<script>
  import Icon from './Icon.svelte';
  import Select from './Select.svelte';
  import Switch from './Switch.svelte';
  import { session as s } from './session.svelte.js';
  import { fxStages, stageOn, toggleStage } from './stages.js';
  import { rack } from './rack.svelte.js';

  const SLOTS = 4;

  const all = $derived(fxStages(s));
  $effect(() => rack.sync());
  const firstUnit = $derived(all.findIndex((r) => s.rack.includes(r.key)));
  const before = $derived(all.filter((r, i) => !s.rack.includes(r.key) && i < firstUnit));
  const after = $derived(all.filter((r, i) => !s.rack.includes(r.key) && i > firstUnit));
  const used = $derived(rack.used());
  const places = $derived(Array.from({ length: Math.max(SLOTS, used.length) }, (_, k) => used[k] ?? ''));
  const title = (id) => all.find((r) => r.key === id)?.title ?? id;
  const optionsFor = (id) => [{ value: '', label: 'None' }, ...[...(id ? [id] : []), ...rack.free()].map((x) => ({ value: x, label: title(x) }))];

  function pick(k, id) {
    const was = places[k];
    if (id === was) return;
    if (!id) rack.remove(was);
    else if (was) rack.replace(was, id);
    else { rack.add(id); s.openPanel('stage', id); }
  }

  // a place dragged onto another: its effect goes there
  let dragged = $state(-1), over = $state(-1);
  function drop(k) {
    const from = dragged;
    dragged = over = -1;
    if (from < 0 || from === k || !places[from]) return;
    const order = used.filter((x) => x !== places[from]);
    order.splice(Math.min(k, order.length), 0, places[from]);
    s.setRack([...order, ...s.rack.filter((x) => !order.includes(x))]);
  }
</script>

{#snippet fixed(r)}
  <div class="fixed" class:off={!stageOn(s, r.st)} class:sel={s.panel?.id === r.key}>
    <button class="name" onclick={() => s.openPanel('stage', r.key)} title="Open {r.title}">{r.title}</button>
    {#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" />
    {:else}<span class="lock" title="always on"><Icon name="lock" size={11} /></span>{/if}
  </div>
{/snippet}

<section class="fxchain" aria-label="Effect chain">
  <div class="ends">{#each before as r (r.key)}{@render fixed(r)}<span class="arrow">→</span>{/each}</div>
  <div class="places" role="list">
    {#each places as id, k (k)}
      <div class="place" class:empty={!id} class:sel={id && s.panel?.id === id} class:over={over === k && dragged !== k} role="listitem"
           draggable={!!id} ondragstart={(e) => { dragged = k; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(k)); }}
           ondragover={(e) => { if (dragged >= 0) { e.preventDefault(); over = k; } }} ondragleave={() => over === k && (over = -1)}
           ondrop={(e) => { e.preventDefault(); drop(k); }} ondragend={() => (dragged = over = -1)}>
        <div class="head">
          <span class="num mono" title={id ? 'Drag to move it' : ''}>{k + 1}</span>
          <Select value={id} options={optionsFor(id)} label="Effect in place {k + 1}" onchange={(v) => pick(k, v)} />
          {#if id}<button class="iconbtn edit" onclick={() => s.openPanel('stage', id)} aria-label="Edit {title(id)}" title="Edit {title(id)}"><Icon name="next" size={13} /></button>{/if}
        </div>
      </div>
      {#if k < places.length - 1}<span class="arrow">→</span>{/if}
    {/each}
  </div>
  <div class="ends">{#each after as r (r.key)}<span class="arrow">→</span>{@render fixed(r)}{/each}</div>
</section>

<style>
  .fxchain { display: flex; align-items: center; gap: 4px; margin: 0 0 14px; padding-bottom: 4px; overflow-x: auto; }
  .ends, .places { display: flex; flex: none; align-items: center; gap: 4px; }
  .arrow { color: var(--muted); font-size: 11px; }
  .fixed { box-sizing: border-box; flex: none; white-space: nowrap; display: inline-flex; align-items: center; gap: 7px; height: 30px; padding: 0 8px;
    border: 1px solid var(--line); border-radius: 7px; background: var(--surface); font-size: 12px; }
  .fixed.off .name { color: var(--muted); }
  .fixed.sel, .place.sel { border-color: var(--accent); }
  .lock { color: var(--muted); display: inline-grid; place-items: center; width: 14px; }
  .name { font-weight: 500; }
  .name:hover { color: var(--accent); }
  .place { box-sizing: border-box; flex: none; display: flex; align-items: center; width: 140px; height: 30px; padding: 0 6px 0 8px; border: 1px solid var(--line); border-radius: 7px;
    background: var(--surface); font-size: 12px; }
  .place.empty { width: 96px; border-style: dashed; background: none; }
  .place.over { box-shadow: -3px 0 0 var(--accent); }
  .place[draggable='true'] .num { cursor: grab; }
  .head { flex: 1; display: flex; align-items: center; gap: 6px; min-width: 0; }
  .head :global(.select) { flex: 1; min-width: 0; }
  .num { color: var(--muted); font-size: 11px; width: 12px; }
  .edit { color: var(--muted); }
  .edit:hover { color: var(--accent); }
</style>
