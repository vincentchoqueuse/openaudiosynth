<!-- EffectsTable.svelte — the Effects page: the effect chain as one table, in the order of the signal. Input (the
     output filter, the crush), the rack's six blocks, Output (the master, the saturation, the limiter). A block holds
     one effect type, picked from the list in its row: Empty takes it out (switched off), an effect another block
     holds swaps places with this one (the rack's order is fx_slot1..6, session.setRack). An effect is in a block
     when it is on, when a macro moves it, or since it was loaded into one (a mix dragged to 0 does not empty it).
     Every row: its switch, the line of its settings, Configure for all of them in the side panel. -->
<script>
  import DataTable from './DataTable.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import { session as s } from './session.svelte.js';
  import { fxStages, summary, stageOn, toggleStage } from './stages.js';

  const all = $derived(fxStages(s));
  const inRack = (id) => s.rack.includes(id);
  let loaded = $state({}), emptied = $state({});
  $effect(() => { s.patchGen; loaded = {}; emptied = {}; });
  $effect(() => {
    for (const r of all) {
      if (!inRack(r.key) || loaded[r.key]) continue;
      if (stageOn(s, r.st)) { loaded[r.key] = true; delete emptied[r.key]; }
      else if (!emptied[r.key] && r.ps.some(({ d }) => s.macrosOf[d.id])) loaded[r.key] = true;
    }
  });
  const slots = $derived.by(() => {
    const used = s.rack.filter((id) => loaded[id]);
    return Array.from({ length: s.rackUnits.length }, (_, k) => used[k] ?? null);
  });
  const byKey = (id) => all.find((r) => r.key === id);
  const options = (k) => [{ value: '', label: 'Empty' },
    ...s.rack.map((id) => { const at = slots.indexOf(id); return { value: id, label: byKey(id)?.title + (at >= 0 && at !== k ? ` (block ${at + 1})` : '') }; })];
  function load(k, id) {
    const A = [...slots], old = A[k];
    if (old === id) return;
    const j = id ? A.indexOf(id) : -1;
    if (j >= 0) A[j] = old; // the effect leaves its block for this one: the two swap
    A[k] = id || null;
    const used = A.filter(Boolean);
    s.setRack([...used, ...s.rack.filter((x) => !used.includes(x))]);
    if (old && !used.includes(old)) { // the effect that was here goes out, switched off
      const st = byKey(old).st;
      if (stageOn(s, st)) toggleStage(s, st);
      delete loaded[old];
      emptied[old] = true;
      if (s.panel?.id === old) s.closePanel();
    }
    if (id) {
      const st = byKey(id).st;
      loaded[id] = true;
      delete emptied[id];
      if (!stageOn(s, st)) toggleStage(s, st);
    }
  }

  const first = $derived(all.findIndex((r) => inRack(r.key)));
  const rows = $derived([
    ...all.filter((r, i) => i < first && !inRack(r.key)).map((r) => ({ ...r, group: 'Input' })),
    ...slots.map((id, k) => (id ? { ...byKey(id), k, group: 'Effects' } : { key: 'slot:' + k, k, group: 'Effects', open: false, dim: true })),
    ...all.filter((r, i) => i > first && !inRack(r.key)).map((r) => ({ ...r, group: 'Output' })),
  ]);
  const columns = [{ id: 'num', label: '#', w: '40px', cls: 'muted mono' }, { id: 'name', label: 'Effect', w: '24%' },
                   { id: 'on', label: 'On', w: '56px' }, { id: 'sum', label: 'Settings', cls: 'sum' }];
</script>

<DataTable label="Effect chain" {columns} {rows} selected={s.panel?.kind === 'stage' ? s.panel.id : null}
           onopen={(r) => s.openPanel('stage', r.key)}>
  {#snippet cell(r, c)}
    {#if c.id === 'num'}{r.k !== undefined ? r.k + 1 : ''}
    {:else if c.id === 'name'}
      {#if r.k !== undefined}<Select class="fxpick" value={r.st?.id ?? ''} options={options(r.k)} label="Block {r.k + 1}" onchange={(v) => load(r.k, v)} />
      {:else}<span class="strong">{r.title}</span>{/if}
    {:else if c.id === 'on'}{#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" />{/if}
    {:else if r.st}<span class:off={!stageOn(s, r.st)}>{stageOn(s, r.st) ? summary(s, r, 3, false) : 'off'}</span>{/if}
  {/snippet}
</DataTable>
