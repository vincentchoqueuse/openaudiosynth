<!-- SynthTable.svelte — every module of the sound in one table: the instrument's stages in the order its signal
     flows (the sampler: its kit), the effect chain (input, the rack's six blocks, output), the sources of modulation
     (envelopes, LFOs, Wander, the controllers) and the eight macros. Above it, a search (in the modules' names, their
     settings' names and what they move) and a tag per family. Each row: its name, its tag, its switch, the line of
     its main settings (or what it moves); its cog, or a click on it, opens it in the side panel.
     A block of the rack holds one effect type, picked from the list in its row: Empty takes it out (switched off),
     an effect another block holds swaps places with this one (fx_slot1..6, session.setRack). An effect stays in its
     block once loaded, or while a macro moves it (a mix dragged to 0 does not empty it). -->
<script>
  import DataTable from './DataTable.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import Pills from './Pills.svelte';
  import { session as s } from './session.svelte.js';
  import { engineStages, fxStages, summary, stageOn, toggleStage } from './stages.js';
  import { modRows } from './mods.js';
  import { human } from './naming.js';
  import * as R from './routes.js';

  const TAGS = [{ id: 'all', label: 'All' }, { id: 'engine', label: 'Engine' }, { id: 'effect', label: 'Effect' },
                { id: 'modulation', label: 'Modulation' }, { id: 'macro', label: 'Macro' }];

  // ---- the rack's blocks ----
  const fx = $derived(fxStages(s));
  const inRack = (id) => s.rack.includes(id);
  let loaded = $state({}), emptied = $state({});
  $effect(() => { s.patchGen; loaded = {}; emptied = {}; });
  $effect(() => {
    for (const r of fx) {
      if (!inRack(r.key) || loaded[r.key]) continue;
      if (stageOn(s, r.st)) { loaded[r.key] = true; delete emptied[r.key]; }
      else if (!emptied[r.key] && r.ps.some(({ d }) => s.macrosOf[d.id])) loaded[r.key] = true;
    }
  });
  // the blocks: the rack's places as last arranged here (empty blocks may sit anywhere), or, for a sound just loaded,
  // the effects in use first in their order and the empty blocks after; only the order of the effects in use
  // reaches the sound, so arranging empty blocks changes nothing until an effect goes in one
  let view = $state(null);
  $effect(() => { s.patchGen; view = null; });
  const order = $derived.by(() => {
    const used = s.rack.filter((id) => loaded[id]);
    if (view && view.length === s.rack.length && view.filter((id) => loaded[id]).join() === used.join()) return view;
    return [...used, ...s.rack.filter((id) => !loaded[id])];
  });
  const slots = $derived(order.map((id) => (loaded[id] ? id : null)));
  function arrange(o) { view = o; s.setRack(o); }
  const fxRow = (id) => fx.find((r) => r.key === id);
  const options = (k) => [{ value: '', label: 'Empty' },
    ...s.rack.map((id) => { const at = slots.indexOf(id); return { value: id, label: fxRow(id)?.title + (at >= 0 && at !== k ? ` (block ${at + 1})` : '') }; })];
  function unload(id) {
    const st = fxRow(id).st;
    if (stageOn(s, st)) toggleStage(s, st);
    delete loaded[id];
    emptied[id] = true;
    if (s.panel?.id === id) s.closePanel();
  }
  function load(k, id) { // an effect into block k: one from another block swaps with it, a new one replaces it
    const o = [...order], old = o[k];
    if (!id) { if (loaded[old]) unload(old); return; }
    if (old === id) return;
    const wasIn = !!loaded[id], j = o.indexOf(id);
    [o[k], o[j]] = [id, old];
    arrange(o);
    if (!wasIn && loaded[old]) unload(old);
    const st = fxRow(id).st;
    loaded[id] = true;
    delete emptied[id];
    if (!stageOn(s, st)) toggleStage(s, st);
  }
  function move(from, to) { // a block dragged onto another: it takes that place, the others close up behind it
    if (from === to) return;
    const o = [...order], [id] = o.splice(from, 1);
    o.splice(to, 0, id);
    arrange(o);
  }

  // ---- every row ----
  const movesOf = (m) => m.kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k))).filter((x) => x.src && x.srcLabel === m.src);
  const rows = $derived.by(() => {
    const out = [];
    if (s.engine?.name === 'sampler') out.push({ key: 'kit', kind: 'kit', title: 'Kit', tag: 'engine', group: 'Engine', words: 'kit pads samples' });
    for (const r of engineStages(s)) out.push({ ...r, kind: 'stage', tag: 'engine', group: 'Engine' });
    const first = fx.findIndex((r) => inRack(r.key));
    fx.forEach((r, i) => { if (i < first && !inRack(r.key)) out.push({ ...r, kind: 'stage', tag: 'effect', group: 'Effects' }); });
    slots.forEach((id, k) => out.push(id ? { ...fxRow(id), k, kind: 'stage', tag: 'effect', group: 'Effects' }
                                          : { key: 'slot:' + k, k, kind: 'slot', title: 'Empty', tag: 'effect', group: 'Effects', open: false, dim: true }));
    fx.forEach((r, i) => { if (i > first && !inRack(r.key)) out.push({ ...r, kind: 'stage', tag: 'effect', group: 'Effects' }); });
    for (const m of modRows()) {
      const moves = movesOf(m).map((x) => R.dstName(s, x));
      const idle = m.key === 'wander' ? !(s.getP('wander') > 0) : !moves.length;
      out.push({ ...m, title: m.label, kind: 'mod', tag: 'modulation', group: 'Modulation', moves, dim: m.ctl && idle, idle });
    }
    s.macroNames.forEach((name, k) => {
      const targets = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
      out.push({ key: 'm' + k, k, kind: 'macro', title: s.macroName(k), tag: 'macro', group: 'Macros', targets, dim: !name && !targets.length });
    });
    return out;
  });
  // the line of a row: its main settings, or what it moves
  function line(r) {
    if (r.kind === 'stage') return stageOn(s, r.st) ? summary(s, r, 3, r.tag === 'engine') : 'off';
    if (r.kind === 'mod') return r.key === 'wander' ? (r.idle ? 'off' : `moves the macros · ${(s.getP('wander') * 100).toFixed(0)} %`) : r.moves.length ? `→ ${r.moves.join(', ')}` : '—';
    if (r.kind === 'macro') return r.targets.length ? `→ ${r.targets.join(', ')}` : '—';
    if (r.kind === 'kit') return `${s.kit?.entries?.length ?? 0} samples${s.kitName ? ` · ${s.kitName}` : ""}`;
    return '';
  }
  // the search: every word of the query in the row's name, tag, its settings' names or its line
  const text = (r) => [r.title, r.tag, r.type ?? '', line(r), r.words ?? '', ...(r.ps ?? []).map(({ d, label }) => `${label} ${human(d.name)}`)].join(' ').toLowerCase();
  const shown = $derived.by(() => {
    const words = s.synthQuery.toLowerCase().split(/\s+/).filter(Boolean);
    return rows.filter((r) => (s.synthTag === 'all' || r.tag === s.synthTag) && (!words.length || (r.kind !== 'slot' && words.every((w) => text(r).includes(w)))));
  });
  // the rows the panel's arrows step through: those of the table as it is shown
  $effect(() => { s.tableRows = shown.filter((r) => r.open !== false).map((r) => ({ kind: r.kind, key: r.key })); });
  const count = (id) => rows.filter((r) => r.kind !== 'slot' && (id === 'all' || r.tag === id)).length;
  const columns = [{ id: 'name', label: 'Module', w: '24%' }, { id: 'tag', label: 'Tag', w: '190px' },
                   { id: 'on', label: 'On', w: '56px' }, { id: 'sum', label: 'Settings', cls: 'sum' }];
</script>

<div class="finder">
  <input class="search" type="search" placeholder="Search a module or a setting (cutoff, delay, velocity…)" bind:value={s.synthQuery} aria-label="Search" />
  <Pills items={TAGS.map((t) => ({ ...t, label: `${t.label} ${count(t.id)}` }))} value={s.synthTag} onpick={(id) => (s.synthTag = id)} label="Tags" />
</div>
<DataTable label="Modules of the sound" {columns} rows={shown} selected={s.panel?.id ?? null} onopen={(r) => s.openPanel(r.kind, r.key)}
           drag={{ can: (r) => r.tag === 'effect' && r.k !== undefined, move: (a, b) => move(a.k, b.k) }}
           empty="Nothing matches.">
  {#snippet cell(r, c)}
    {#if c.id === 'name'}
      {#if r.k !== undefined && r.tag === 'effect'}<span class="dt-slot"><span class="muted mono">{r.k + 1}</span>
        <Select class="fxpick" value={r.st?.id ?? ''} options={options(r.k)} label="Block {r.k + 1}" onchange={(v) => load(r.k, v)} /></span>
      {:else}<span class="strong">{r.title}</span>{/if}
    {:else if c.id === 'tag'}<span class="tag tag-{r.tag}">{r.tag}</span>{#if r.type}<span class="muted"> · {r.type.toLowerCase()}</span>{/if}
    {:else if c.id === 'on'}{#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" />{/if}
    {:else}<span class:off={line(r) === 'off' || line(r) === '—'}>{line(r)}</span>{/if}
  {/snippet}
</DataTable>
