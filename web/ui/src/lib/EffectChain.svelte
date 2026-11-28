<!-- EffectChain.svelte — the effects as one line, in the order the sound goes through them: the fixed stages before
     the rack (output filter, crush), a fixed number of places (SLOTS, more only when a sound uses more), then the fixed
     stages after it (master, saturation, limiter). Each place: a menu (None, or an effect not in another place), its
     dry / wet (the gain: its level), and a button that opens it in the side panel. A place dragged onto another moves its
     effect there; None takes the effect out and the places after it move up (rack.svelte.js). -->
<script>
  import Icon from './Icon.svelte';
  import Select from './Select.svelte';
  import Switch from './Switch.svelte';
  import { session as s } from './session.svelte.js';
  import { fxStages, stageOn, toggleStage } from './stages.js';
  import { rack } from './rack.svelte.js';
  import { fromNorm, toNorm, format } from './synth.js';
  import { precise } from './slider.js';

  const SLOTS = 4;
  // each effect's dry / wet (the gain has none: its level)
  const MIX = { phaser: 'phaser_mix', flanger: 'flanger_mix', chorus: 'chorus_mix', delay: 'delay_mix', reverb: 'reverb_mix', tape: 'tape_mix', gain: 'gain_level' };

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
  const mixD = (id) => s.byName[MIX[id]];
  const row = (id) => all.find((r) => r.key === id);

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
    {#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" />
    {:else}<span class="lock" title="always on"><Icon name="lock" size={11} /></span>{/if}
    <button class="name" onclick={() => s.openPanel('stage', r.key)} title="Open {r.title}">{r.title}</button>
  </div>
{/snippet}

<section class="fxchain" aria-label="Effect chain">
  <div class="ends">{#each before as r (r.key)}{@render fixed(r)}<span class="arrow">→</span>{/each}</div>
  <div class="places" role="list">
    {#each places as id, k (k)}
      {@const d = id ? mixD(id) : null}
      <div class="place" class:empty={!id} class:sel={id && s.panel?.id === id} class:over={over === k && dragged !== k} role="listitem"
           draggable={!!id} ondragstart={(e) => { dragged = k; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(k)); }}
           ondragover={(e) => { if (dragged >= 0) { e.preventDefault(); over = k; } }} ondragleave={() => over === k && (over = -1)}
           ondrop={(e) => { e.preventDefault(); drop(k); }} ondragend={() => (dragged = over = -1)}>
        <div class="head">
          <span class="num mono" title={id ? 'Drag to move it' : ''}>{k + 1}</span>
          <Select value={id} options={optionsFor(id)} label="Effect in place {k + 1}" onchange={(v) => pick(k, v)} />
          {#if id}<button class="iconbtn edit" onclick={() => s.openPanel('stage', id)} aria-label="Edit {title(id)}" title="Edit {title(id)}"><Icon name="settings" size={13} /></button>{/if}
        </div>
        {#if d}
          {@const v = s.getP(d.name)}
          {@const n = toNorm(d, v)}
          <label class="mix" title={d.help ?? ''}>
            <span class="muted">{id === 'gain' ? 'Level' : 'Dry / wet'}</span>
            <input type="range" min="0" max="1" step="0.001" value={n} style="--p:{n * 100}%" aria-label="{title(id)} {id === 'gain' ? 'level' : 'dry / wet'}"
                   use:precise={{ value: () => toNorm(d, s.getP(d.name)), set: (x) => s.setValue(d, fromNorm(d, x)) }}
                   ondblclick={() => s.setValue(d, row(id)?.st?.on ?? d.def)} />
            <span class="val mono">{id === 'gain' ? format(d, v) : `${Math.round(v * 100)} %`}</span>
          </label>
        {:else}<p class="hint0">Empty: pick an effect</p>{/if}
      </div>
      {#if k < places.length - 1}<span class="arrow">→</span>{/if}
    {/each}
  </div>
  <div class="ends">{#each after as r (r.key)}<span class="arrow">→</span>{@render fixed(r)}{/each}</div>
</section>

<style>
  .fxchain { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; margin: 0 0 14px; }
  .ends, .places { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
  .arrow { color: var(--muted); font-size: 11px; }
  .fixed { box-sizing: border-box; display: inline-flex; align-items: center; gap: 7px; height: 30px; padding: 0 8px;
    border: 1px solid var(--line); border-radius: 7px; background: var(--surface); font-size: 12px; }
  .fixed.off .name { color: var(--muted); }
  .fixed.sel, .place.sel { border-color: var(--accent); }
  .lock { color: var(--muted); display: inline-grid; place-items: center; width: 30px; }
  .name { font-weight: 500; }
  .name:hover { color: var(--accent); }
  .place { box-sizing: border-box; display: grid; gap: 4px; width: 190px; padding: 6px 8px; border: 1px solid var(--line); border-radius: 7px;
    background: var(--surface); font-size: 12px; }
  .place.empty { border-style: dashed; background: none; }
  .place.over { box-shadow: -3px 0 0 var(--accent); }
  .place[draggable='true'] .num { cursor: grab; }
  .head { display: flex; align-items: center; gap: 6px; min-width: 0; }
  .head :global(.select) { flex: 1; min-width: 0; }
  .num { color: var(--muted); font-size: 11px; width: 12px; }
  .edit { color: var(--muted); }
  .edit:hover { color: var(--accent); }
  .mix { display: grid; grid-template-columns: auto 1fr 3.6em; align-items: center; gap: 6px; margin: 0; }
  .mix .muted { font-size: 11px; }
  .val { text-align: right; color: var(--muted); font-size: 11px; }
  .hint0 { margin: 0; height: 18px; line-height: 18px; color: var(--muted); font-size: 11px; }
</style>
