<!-- SynthTable.svelte — every module of the sound as the records of one table, the way a database admin shows a
     collection: the instrument's stages in the order its signal flows (the sampler: its kit), the effect chain (the
     output filter and the crush, the effects added to the rack, the master, the saturation, the limiter), the sources
     of modulation (envelopes, LFOs, Wander, the controllers) and the eight macros. A toolbar: a search (names,
     settings, what a module moves), a tag per family, On / Off, then the actions: Add effect (one not yet in the
     rack: rack.svelte.js), Init and Randomize. Columns are fields: Name, Tag, Type, On (a switch: onoff.js; a lock
     when the module is always on), Summary (its main settings) and Moves (what a source or a macro moves); a click on
     a head sorts. A row's cog, or a click on it, opens the record in the side panel. Under the table, the count. -->
<script>
  import DataTable from './DataTable.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { engineStages, fxStages, summary, stageOn, ROLE } from './stages.js';
  import { rack } from './rack.svelte.js';
  import { modRows } from './mods.js';
  import { human } from './naming.js';
  import * as R from './routes.js';
  import Seg from './Seg.svelte';
  import { isOn, locked, toggle } from './onoff.js';

  const TAGS = [{ id: 'all', label: 'All' }, { id: 'engine', label: 'Engine' }, { id: 'effect', label: 'Effect' },
                { id: 'modulation', label: 'Modulation' }, { id: 'macro', label: 'Macro' }];

  const fx = $derived(fxStages(s));
  const inRack = (id) => s.rack.includes(id);
  $effect(() => rack.sync());
  const addOptions = $derived([{ value: '', label: '+ Add effect' }, ...rack.free().map((id) => ({ value: id, label: fx.find((r) => r.key === id)?.title ?? id }))]);
  function addEffect(id) { if (!id) return; rack.add(id); s.openPanel('stage', id); }
  // Init: a new sound on the same instrument; Randomize: every setting of the instrument somewhere else
  function init() {
    if (s.changedCount && !confirm('Start a new sound on this instrument? The edits of this one are not kept.')) return;
    s.newSound(s.engine.name);
  }

  // ---- every row ----
  const movesOf = (m) => m.kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k))).filter((x) => x.src && x.srcLabel === m.src);
  const lab = (p, n) => { const d = s.byName[p + n]; return d?.labels?.[Math.round(s.getP(p + n) - d.min)]; };
  function modLine(m) { // an LFO's wave and rate or division, an envelope's points and loop, Wander's amount
    if (m.key === 'wander') return `${(s.getP('wander') * 100).toFixed(0)} % · ${s.getP('wander_rate').toPrecision(2)} Hz`;
    if (m.ctl) return '';
    const p = m.prefix;
    if (m.type === 'LFO') return `${lab(p, 'wave') ?? ''} · ${Math.round(s.getP(p + 'sync')) ? lab(p, 'sync') : `${s.getP(p + 'rate').toPrecision(3)} Hz`}`;
    const loop = lab(p, 'mode');
    return `${Math.round(s.getP(p + 'points'))} points${loop && loop !== 'oneshot' ? ' · ' + loop.replace('_', ' ') : ''}`;
  }
  const rows = $derived.by(() => {
    const out = [];
    if (s.engine?.name === 'sampler') out.push({ key: 'kit', kind: 'kit', title: 'Kit', tag: 'engine', type: 'samples', words: 'kit pads samples',
                                                 sum: `${s.kit?.entries?.length ?? 0} samples${s.kitName ? ` · ${s.kitName}` : ''}`, moves: '' });
    const stage = (r, tag, type) => out.push({ ...r, kind: 'stage', tag, type, sum: stageOn(s, r.st) ? summary(s, r, 3, tag === 'engine') : '', moves: '' });
    for (const r of engineStages(s)) stage(r, 'engine', (ROLE[r.st?.kind] ?? 'stage').toLowerCase());
    const first = fx.findIndex((r) => inRack(r.key));
    fx.forEach((r, i) => { if (i < first && !inRack(r.key)) stage(r, 'effect', 'input'); });
    for (const id of rack.used()) stage(fx.find((r) => r.key === id), 'effect', 'insert');
    fx.forEach((r, i) => { if (i > first && !inRack(r.key)) stage(r, 'effect', 'output'); });
    for (const m of modRows()) {
      const moves = m.key === 'wander' ? (s.getP('wander') > 0 ? 'the macros' : '') : movesOf(m).map((x) => R.dstName(s, x)).join(', ');
      out.push({ ...m, title: m.label, kind: 'mod', tag: 'modulation', type: m.type.toLowerCase(), sum: modLine(m), moves, dim: m.ctl && !moves });
    }
    s.macroNames.forEach((name, k) => {
      const targets = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
      out.push({ key: 'm' + k, k, kind: 'macro', title: s.macroName(k), tag: 'macro', type: 'control', sum: `${Math.round(s.getP(`macro${k + 1}`) * 100)} %`,
                 moves: targets.join(', '), dim: !name && !targets.length });
    });
    return out;
  });
  // the search: every word of the query in the row's name, tag, type, its settings' names, its summary or what it moves
  const text = (r) => [r.title, r.tag, r.type, r.sum, r.moves, r.words ?? '', ...(r.ps ?? []).map(({ d, label }) => `${label} ${human(d.name)}`)].join(' ').toLowerCase();
  let sort = $state(null); // {id, dir}: a head clicked; null: the order of the signal
  const SORT = { name: (r) => r.title.toLowerCase(), tag: (r) => r.tag, type: (r) => r.type, on: (r) => (isOn(s, r) ? 0 : 1) };
  const shown = $derived.by(() => {
    const words = s.synthQuery.toLowerCase().split(/\s+/).filter(Boolean);
    const state = (r) => s.synthState === 'all' || isOn(s, r) === (s.synthState === 'on');
    const out = rows.filter((r) => (s.synthTag === 'all' || r.tag === s.synthTag) && state(r) && (!words.length || words.every((w) => text(r).includes(w))));
    if (!sort) return out;
    const f = SORT[sort.id];
    return out.map((r, i) => [r, i]).sort(([a, i], [b, j]) => (f(a) < f(b) ? -1 : f(a) > f(b) ? 1 : 0) * sort.dir || i - j).map(([r]) => r);
  });
  // the rows the panel's arrows step through: those of the table as it is shown
  $effect(() => { s.tableRows = shown.map((r) => ({ kind: r.kind, key: r.key })); });
  const count = (id) => rows.filter((r) => id === 'all' || r.tag === id).length;
  const foot = $derived(`${shown.length === rows.length ? rows.length : `${shown.length} of ${rows.length}`} modules · ${rows.filter((r) => isOn(s, r)).length} on`);
  const columns = [{ id: 'name', label: 'Name', w: '19%', sort: true }, { id: 'tag', label: 'Tag', w: '112px', sort: true }, { id: 'type', label: 'Type', w: '104px', cls: 'muted', sort: true },
                   { id: 'on', label: 'On', w: '58px', sort: true }, { id: 'sum', label: 'Summary', cls: 'sum' }, { id: 'moves', label: 'Moves', w: '22%', cls: 'sum' }];
</script>

<div class="finder">
  <label class="search">
    <Icon name="search" size={14} />
    <input type="search" placeholder="Search" bind:value={s.synthQuery} aria-label="Search modules and settings" spellcheck="false" />
    {#if s.synthQuery}<button class="iconbtn" onclick={() => (s.synthQuery = '')} aria-label="Clear the search" title="Clear"><Icon name="close" size={11} /></button>{/if}
  </label>
  <Seg items={TAGS.map((t) => ({ ...t, label: `${t.label} ${count(t.id)}` }))} value={s.synthTag} onpick={(id) => (s.synthTag = id)} label="Tags" />
  <Seg items={[{ id: 'all', label: 'All' }, { id: 'on', label: 'On', title: 'only what is on' }, { id: 'off', label: 'Off', title: 'only what is off' }]}
       value={s.synthState} onpick={(id) => (s.synthState = id)} label="On or off" />
  <span class="tools">
    <button class="tool" onclick={init} title="A new sound on this instrument: every setting at its default"><Icon name="init" size={13} /> Init</button>
    <button class="tool" onclick={s.randomize} title="Throw every setting of the instrument somewhere else (Undo brings the sound back)"><Icon name="dice" size={13} /> Randomize</button>
    <Select class="tool primary" value="" options={addOptions} disabled={addOptions.length < 2} label="Add an effect" onchange={addEffect} />
  </span>
</div>
<DataTable label="Modules of the sound" {columns} rows={shown} selected={s.panel?.id ?? null} onopen={(r) => s.openPanel(r.kind, r.key)}
           empty="Nothing matches." bind:sort {foot}>
  {#snippet cell(r, c)}
    {#if c.id === 'name'}<span class="strong">{r.title}</span>
    {:else if c.id === 'tag'}<span class="tag tag-{r.tag}">{r.tag}</span>
    {:else if c.id === 'type'}{r.type}
    {:else if c.id === 'on'}
      {#if locked(s, r) && isOn(s, r)}<span class="lock" title="always on"><Icon name="lock" size={13} /></span>
      {:else}<Switch on={isOn(s, r)} disabled={locked(s, r)} onchange={() => toggle(s, r)} name="{r.title} on/off"
                     title={locked(s, r) ? 'moves nothing yet: open it to give it something to move' : isOn(s, r) ? 'on' : 'off'} />{/if}
    {:else if c.id === 'sum'}<span class:off={!isOn(s, r)}>{isOn(s, r) || r.kind !== 'stage' ? r.sum : 'off'}</span>
    {:else}{r.moves}{/if}
  {/snippet}
</DataTable>
