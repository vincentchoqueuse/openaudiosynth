<!-- SynthTable.svelte — every module of the sound as the records of one table, the way a database admin shows a
     collection: the instrument's stages in the order its signal flows (the sampler: its kit), the effect chain (the
     output filter and the crush, the effects added to the rack, the master, the saturation, the limiter), the sources
     of modulation (envelopes, LFOs, Wander, the controllers) and the eight macros. A toolbar: a search (names,
     settings, what a module moves; the settings it finds are listed above the table, each set right there), the four types (source, transform, modulation, macro), On / Off, then the actions: Init and Randomize (effects are added in the chain above: EffectChain). Columns are fields: Type first (source, transform, modulation or macro: an icon on its colour; the type filters too), Name, On (a switch: onoff.js; a lock
     when the module is always on), Summary (its main settings) and Moves (what a source or a macro moves); a click on
     a head sorts. A row's cog, or a click on it, opens the record in the side panel. Under the table, the count. -->
<script>
  import DataTable from './DataTable.svelte';
  import Switch from './Switch.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { engineStages, fxStages, summary, stageOn, ROLE } from './stages.js';
  import { rack } from './rack.svelte.js';
  import { modRows } from './mods.js';
  import { human } from './naming.js';
  import * as R from './routes.js';
  import Seg from './Seg.svelte';
  import Param from './Param.svelte';
  // audio: what makes and shapes the sound (sources, transforms); mod: what moves it (modulators, macros)
  let { scope = 'audio' } = $props();
  const SCOPE = { audio: ['source', 'transform'], mod: ['modulation', 'macro'] };
  import { isOn, locked, toggle } from './onoff.js';

  /* every module is one of four: a source makes the sound (oscillators, noise, the kit, the voice that plays them), a
   * transform works on it (filters, amp, every effect up to the output), a modulation moves settings over time, a macro
   * moves several at once from one control */
  const TAGS = [{ id: 'all', label: 'All' }, { id: 'source', label: 'Source' }, { id: 'transform', label: 'Transform' },
                { id: 'modulation', label: 'Modulation' }, { id: 'macro', label: 'Macro' }];
  const TYPE_ICON = { source: 'source', transform: 'transform', modulation: 'modsrc', macro: 'macro' }; // icons.js
  const CLASS = { source: 'source', voice: 'source', filter: 'transform', amp: 'transform', mod: 'modulation' };

  const fx = $derived(fxStages(s));
  const inRack = (id) => s.rack.includes(id);
  $effect(() => rack.sync());
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
    if (s.engine?.name === 'sampler') out.push({ key: 'kit', kind: 'kit', title: 'Kit', tag: 'source', type: 'samples', words: 'kit pads samples',
                                                 sum: `${s.kit?.entries?.length ?? 0} samples${s.kitName ? ` · ${s.kitName}` : ''}`, moves: '' });
    // a stage: its class (tag), and what it is more precisely (type: filter, amp, effect...), for the search
    const stage = (r, tag, type, engine) => out.push({ ...r, kind: 'stage', tag, type, engine, fx: !engine, sum: stageOn(s, r.st) ? summary(s, r, 3, engine) : '', moves: '' });
    for (const r of engineStages(s)) stage(r, CLASS[r.st?.kind] ?? 'source', (ROLE[r.st?.kind] ?? 'stage').toLowerCase(), true);
    for (const r of out) if (r.tag === 'modulation') r.group = `The ${s.engine?.name ?? 'instrument'}'s own`; // its envelopes, LFOs
    const first = fx.findIndex((r) => inRack(r.key));
    fx.forEach((r, i) => { if (i < first && !inRack(r.key)) stage(r, 'transform', 'effect', false); });
    for (const id of rack.used()) stage(fx.find((r) => r.key === id), 'transform', 'effect', false);
    fx.forEach((r, i) => { if (i > first && !inRack(r.key)) stage(r, 'transform', 'output', false); });
    for (const m of modRows()) {
      const moves = m.key === 'wander' ? (s.getP('wander') > 0 ? 'the macros' : '') : movesOf(m).map((x) => R.dstName(s, x)).join(', ');
      out.push({ ...m, title: m.label, kind: 'mod', tag: 'modulation', type: m.type.toLowerCase(), sum: modLine(m), moves, dim: m.ctl && !moves,
                 group: m.ctl ? 'Controllers' : 'Envelopes and LFOs of the sound' });
    }
    s.macroNames.forEach((name, k) => {
      const targets = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
      out.push({ key: 'm' + k, k, kind: 'macro', title: s.macroName(k), tag: 'macro', type: 'control', group: 'Macros', sum: `${Math.round(s.getP(`macro${k + 1}`) * 100)} %`,
                 moves: targets.join(', '), dim: !name && !targets.length });
    });
    return out;
  });
  const inScope = $derived(rows.filter((r) => SCOPE[scope].includes(r.tag)));
  const tag = $derived(SCOPE[scope].includes(s.synthTag) ? s.synthTag : 'all'); // a type of the other scope: all of this one
  // the search: every word of the query in the row's name, tag, type, its settings' names, its summary or what it moves
  const text = (r) => [r.title, r.tag, r.type, r.sum, r.moves, r.words ?? '', ...(r.ps ?? []).map(({ d, label }) => `${label} ${human(d.name)}`)].join(' ').toLowerCase();
  let sort = $state(null); // {id, dir}: a head clicked; null: the order of the signal
  const ORDER = { source: 0, transform: 1, modulation: 2, macro: 3 };
  const SORT = { name: (r) => r.title.toLowerCase(), type: (r) => ORDER[r.tag], on: (r) => (isOn(s, r) ? 0 : 1) };
  const shown = $derived.by(() => {
    const words = s.synthQuery.toLowerCase().split(/\s+/).filter(Boolean);
    const state = (r) => s.synthState === 'all' || isOn(s, r) === (s.synthState === 'on');
    const out = inScope.filter((r) => !r.fx && (tag === 'all' || r.tag === tag) && state(r) && (!words.length || words.every((w) => text(r).includes(w))));
    if (!sort) return scope === 'mod' ? out : out.map((r) => ({ ...r, group: undefined })); // groups: the Modulation tab, unsorted
    const f = SORT[sort.id];
    return out.map((r, i) => [r, i]).sort(([a, i], [b, j]) => (f(a) < f(b) ? -1 : f(a) > f(b) ? 1 : 0) * sort.dir || i - j).map(([r]) => ({ ...r, group: undefined }));
  });
  // the search finds settings too: each one whose module, name or role has every word, set right here (a dozen at most)
  const HITS = 12;
  const hits = $derived.by(() => {
    const words = s.synthQuery.toLowerCase().split(/\s+/).filter(Boolean);
    if (!words.length) return [];
    const out = [];
    for (const r of inScope) {
      if (r.kind !== 'stage' || !r.ps) continue;
      for (const { d, label } of r.ps) {
        const t = `${r.title} ${label} ${human(d.name)} ${d.name} ${d.role ?? ''}`.toLowerCase();
        if (words.every((w) => t.includes(w))) out.push({ r, d, label });
      }
    }
    return out;
  });
  // the rows the panel's arrows step through: those of the table as it is shown
  $effect(() => { s.tableRows = shown.map((r) => ({ kind: r.kind, key: r.key })); });
  const listed = $derived(inScope.filter((r) => !r.fx)); // the effects are in the chain (EffectChain), not in the table
  const count = (id) => listed.filter((r) => id === 'all' || r.tag === id).length;
  const foot = $derived(`${shown.length === listed.length ? listed.length : `${shown.length} of ${listed.length}`} modules · ${listed.filter((r) => isOn(s, r)).length} on`);
  const columns = $derived([{ id: 'type', label: '', w: '44px', sort: true, title: 'Type' }, { id: 'name', label: 'Name', w: '19%', sort: true },
                   { id: 'on', label: 'On', w: '58px', sort: true }, { id: 'sum', label: 'Summary', cls: 'sum' },
                   ...(scope === 'mod' ? [{ id: 'moves', label: 'Moves', w: '22%', cls: 'sum' }] : [])]);
</script>

<div class="finder">
  <label class="search">
    <Icon name="search" size={14} />
    <input type="search" placeholder="Search" bind:value={s.synthQuery} aria-label="Search modules and settings" spellcheck="false" />
    {#if s.synthQuery}<button class="iconbtn" onclick={() => (s.synthQuery = '')} aria-label="Clear the search" title="Clear"><Icon name="close" size={11} /></button>{/if}
  </label>
  <Seg items={TAGS.filter((t) => t.id === 'all' || SCOPE[scope].includes(t.id)).map((t) => ({ ...t, icon: TYPE_ICON[t.id], label: t.id === 'all' ? `All ${count(t.id)}` : String(count(t.id)), title: `${t.label}: ${count(t.id)}` }))} value={tag} onpick={(id) => (s.synthTag = id)} label="Tags" />
  <Seg items={[{ id: 'all', label: 'All' }, { id: 'on', label: 'On', title: 'only what is on' }, { id: 'off', label: 'Off', title: 'only what is off' }]}
       value={s.synthState} onpick={(id) => (s.synthState = id)} label="On or off" />
  <span class="tools">
    <button class="tool" onclick={init} title="A new sound on this instrument: every setting at its default"><Icon name="init" size={13} /> Init</button>
    <button class="tool" onclick={s.randomize} title="Throw every setting of the instrument somewhere else (Undo brings the sound back)"><Icon name="dice" size={13} /> Randomize</button>
  </span>
</div>
{#if hits.length}
  <section class="hits" aria-label="Settings found">
    <h4>Settings <span class="muted">{hits.length > HITS ? `${HITS} of ${hits.length}` : hits.length}</span></h4>
    {#each hits.slice(0, HITS) as h (h.d.id)}
      <div class="hit"><button class="hmod" onclick={() => s.openPanel('stage', h.r.key)} title="Open {h.r.title}">{h.r.title}</button><Param d={h.d} label={h.label} /></div>
    {/each}
  </section>
{/if}
<DataTable label="Modules of the sound" {columns} rows={shown} selected={s.panel?.id ?? null} onopen={(r) => s.openPanel(r.kind, r.key)}
           empty="Nothing matches." bind:sort {foot}>
  {#snippet cell(r, c)}
    {#if c.id === 'name'}<span class="strong">{r.title}</span>
    {:else if c.id === 'type'}<span class="ticon cls-{r.tag}" title="{r.tag} · {r.type}" aria-label={r.tag}><Icon name={TYPE_ICON[r.tag]} size={14} /></span>
    {:else if c.id === 'on'}
      {#if locked(s, r) && isOn(s, r)}<span class="lock" title="always on"><Icon name="lock" size={13} /></span>
      {:else}<Switch on={isOn(s, r)} disabled={locked(s, r)} onchange={() => toggle(s, r)} name="{r.title} on/off"
                     title={locked(s, r) ? 'moves nothing yet: open it to give it something to move' : isOn(s, r) ? 'on' : 'off'} />{/if}
    {:else if c.id === 'sum'}<span class:off={!isOn(s, r)}>{isOn(s, r) || r.kind !== 'stage' ? r.sum : 'off'}</span>
    {:else}{r.moves}{/if}
  {/snippet}
</DataTable>

<style>
  .hits { margin: 0 0 14px; padding: 8px 10px; border: 1px solid var(--line); border-radius: 7px; background: var(--surface); }
  .hits h4 { margin: 0 0 6px; font-size: 10.5px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
  .hit { display: grid; grid-template-columns: 9em minmax(0, 1fr); align-items: center; gap: 10px; }
  .hmod { text-align: left; color: var(--muted); font-size: 11.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .hmod:hover { color: var(--accent); }
</style>
