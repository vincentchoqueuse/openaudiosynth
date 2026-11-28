<!-- EffectChain.svelte — the effects as a table like the instrument's (DataTable), one row per stage in the order the
     sound goes through them: the fixed stages before the rack (output filter, crush), a fixed number of places
     (SLOTS, more only when a sound uses more), then the fixed stages after it (master, saturation, limiter). Columns:
     the type, the name (a place: its number and a menu, None or an effect not in another place), On (a switch, a lock
     when always on), the summary of its main settings, and for an effect in a place, up / down to move it in the
     chain. A click on a row, or its arrow, opens it in the side panel (its dry / wet first: StagePanel); None takes
     the effect out and the places after it move up (rack.svelte.js). -->
<script>
  import DataTable from './DataTable.svelte';
  import Icon from './Icon.svelte';
  import Select from './Select.svelte';
  import Switch from './Switch.svelte';
  import { session as s } from './session.svelte.js';
  import { fxStages, summary } from './stages.js';
  import { isOn, locked, toggle } from './onoff.js';
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
  const stage = (r) => ({ ...r, kind: 'stage', sum: summary(s, r, 3, false) });
  const rows = $derived([
    ...before.map(stage),
    ...places.map((id, k) => (id ? { ...stage(all.find((r) => r.key === id)), place: k, id }
                                  : { key: 'place:' + k, place: k, id: '', open: false, dim: true })),
    ...after.map(stage),
  ]);

  function pick(k, id) {
    const was = places[k];
    if (id === was) return;
    if (!id) rack.remove(was);
    else if (was) rack.replace(was, id);
    else { rack.add(id); s.openPanel('stage', id); }
  }
  // an effect one place earlier or later in the chain
  function move(k, d) {
    const order = [...used], j = k + d;
    if (j < 0 || j >= order.length) return;
    [order[k], order[j]] = [order[j], order[k]];
    s.setRack([...order, ...s.rack.filter((x) => !order.includes(x))]);
  }
  const columns = [{ id: 'type', label: '', w: '44px' }, { id: 'name', label: 'Name', w: '19%' }, { id: 'on', label: 'On', w: '58px' },
                   { id: 'sum', label: 'Summary', cls: 'sum' }, { id: 'order', label: '', w: '64px' }];
</script>

<DataTable label="Effects of the sound" {columns} {rows} selected={s.panel?.id ?? null} onopen={(r) => s.openPanel('stage', r.key)}>
  {#snippet cell(r, c)}
    {#if c.id === 'type'}<span class="ticon cls-transform" class:ghost={r.place !== undefined && !r.id} aria-hidden="true"><Icon name={r.place !== undefined ? 'effects' : 'transform'} size={14} /></span>
    {:else if c.id === 'name'}
      {#if r.place !== undefined}
        <span class="place"><span class="num mono">{r.place + 1}</span>
          <Select value={r.id} options={optionsFor(r.id)} label="Effect in place {r.place + 1}" onchange={(v) => pick(r.place, v)} /></span>
      {:else}<span class="strong">{r.title}</span>{/if}
    {:else if c.id === 'on'}
      {#if r.st}
        {#if locked(s, r) && isOn(s, r)}<span class="lock" title="always on"><Icon name="lock" size={13} /></span>
        {:else}<Switch on={isOn(s, r)} disabled={locked(s, r)} onchange={() => toggle(s, r)} name="{r.title} on/off" />{/if}
      {/if}
    {:else if c.id === 'sum'}
      {#if !r.st}<span class="muted">empty: choose an effect</span>
      {:else}<span class:off={!isOn(s, r)}>{isOn(s, r) ? r.sum : 'off'}</span>{/if}
    {:else if c.id === 'order' && r.id}
      <span class="order">
        <button class="iconbtn" onclick={() => move(r.place, -1)} disabled={r.place === 0} aria-label="Earlier in the chain" title="Earlier in the chain"><Icon name="up" size={13} /></button>
        <button class="iconbtn" onclick={() => move(r.place, 1)} disabled={r.place >= used.length - 1} aria-label="Later in the chain" title="Later in the chain"><Icon name="down" size={13} /></button>
      </span>
    {/if}
  {/snippet}
</DataTable>

<style>
  .place { display: flex; align-items: center; gap: 8px; min-width: 0; }
  .place :global(.select) { flex: 1; min-width: 0; max-width: 220px; }
  .num { color: var(--muted); font-size: 11px; width: 12px; }
  .ghost { opacity: .4; }
  .order { display: inline-flex; gap: 2px; }
</style>
