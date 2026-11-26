<!-- SynthTab.svelte — the Synth view, a record with two tabs like every record of the page: General (the sound as
     fields, and its signal path: SoundGeneral) and Settings (every module of the sound in one table with a search,
     tags and On / Off: SynthTable). Configure on a row, or a click on a block of the signal path, opens the module in
     the side panel (SidePanel) with the same two tabs: General (what it is and does, where its signal goes, what it
     moves and what moves it, each setting's help: ModuleGeneral) and Settings (a stage's settings, a modulator's
     editor and what it moves, a macro's targets, the sampler's kit). The panel lies over the page, and its arrows
     step through the table's rows as they are shown. -->
<script>
  import KitPanel from './KitPanel.svelte';
  import SoundGeneral from './SoundGeneral.svelte';
  import Tabs from './Tabs.svelte';
  import SidePanel from './SidePanel.svelte';
  import SynthTable from './SynthTable.svelte';
  import StagePanel from './StagePanel.svelte';
  import ModulatorPanel from './ModulatorPanel.svelte';
  import MacroPanel from './MacroPanel.svelte';
  import ModuleGeneral from './ModuleGeneral.svelte';
  import { isOn } from './onoff.js';
  import { session as s } from './session.svelte.js';
  import { modRows } from './mods.js';
  import { engineStages, fxStages, ROLE } from './stages.js';
  import { human } from './naming.js';
  import * as R from './routes.js';

  // a block of the signal path, or one of its modulator chips: it opens in the panel
  function pick(st, kind) {
    if (kind === 'route') {
      const m = modRows().find((x) => R.srcName(x.src) === st.label || x.label === st.label);
      return m && s.openPanel('mod', m.key);
    }
    s.openPanel('stage', st.id);
  }

  const routesOf = (m) => m.kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k))).filter((x) => x.src && x.srcLabel === m.src);
  // what the panel shows: its title, what it is, and for General its fields and settings
  const P = $derived.by(() => {
    const p = s.panel;
    if (!p) return null;
    if (p.kind === 'stage') {
      const eng = engineStages(s), fx = fxStages(s), all = [...eng, ...fx], inEng = eng.some((x) => x.key === p.id);
      const row = all.find((x) => x.key === p.id);
      if (!row) return null;
      const name = (id) => all.find((x) => x.key === id)?.title ?? (id === 'engine' ? 'the instrument' : id);
      const from = row.st?.inputs?.split(' ').filter(Boolean).map(name).join(', ');
      const to = all.filter((x) => x.st?.inputs?.split(' ').includes(row.key)).map((x) => x.title).join(', ');
      const by = R.keys('part').map((k) => R.get(s, k)).filter((x) => x.src && row.ps.some(({ d }) => x.dst === d.id + 1))
        .map((x) => `${R.srcName(x.srcLabel)} → ${R.dstName(s, x)}`);
      return { title: row.title, sub: inEng ? `${s.engine.name} · ${(ROLE[row.st?.kind] ?? 'stage').toLowerCase()}` : 'effect', row, engine: inEng,
               params: row.ps, fields: [['Type', inEng ? ({ source: 'source', voice: 'source', filter: 'transform', amp: 'transform', mod: 'modulation' })[row.st?.kind] ?? 'source' : 'transform'],
                                        ['Role', ROLE[row.st?.kind] ?? (inEng ? 'stage' : 'effect')],
                                        ['State', isOn(s, { kind: 'stage', st: row.st }) ? 'on' : 'off'], ...(from ? [['From', from]] : []), ...(to ? [['To', to]] : []),
                                        ['Modulated by', by.join(', ') || 'nothing'], ['Settings', String(row.ps.length)]] };
    }
    if (p.kind === 'mod') {
      const m = modRows().find((x) => x.key === p.id);
      if (!m) return null;
      const moves = routesOf(m).map((x) => `${R.dstName(s, x)} ${R.inUnits(s, x)}`);
      const params = m.prefix ? s.shared.filter((d) => d.name.startsWith(m.prefix)).filter((d) => !/\d+$/.test(d.name.slice(m.prefix.length)))
                                 .map((d) => ({ d, label: human(d.name.slice(m.prefix.length)) }))
                    : m.key === 'wander' ? [{ d: s.byName.wander, label: 'Amount' }, { d: s.byName.wander_rate, label: 'Rate' }] : [];
      return { title: m.label, sub: `${m.type.toLowerCase()} · ${m.kinds.map((k) => (k === 'part' ? 'whole sound' : 'each note')).join(' and ')}`,
               m, row: m, wide: !m.ctl && m.key !== 'wander', params,
               fields: [['Type', 'modulation'], ['Kind', m.type], ['Scope', m.kinds.map((k) => (k === 'part' ? 'the whole sound' : 'each note')).join(' and ')],
                        ['State', isOn(s, { ...m, kind: 'mod' }) ? 'on' : 'off'], ['Moves', moves.join(', ') || 'nothing yet']] };
    }
    if (p.kind === 'macro') {
      const k = +p.id.slice(1);
      const t = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
      return { title: s.macroName(k), sub: `macro ${k + 1}`, k, row: {}, wide: true, params: [],
               fields: [['Type', 'macro'], ['Value', `${Math.round(s.getP(`macro${k + 1}`) * 100)} %`], ['MIDI CC', String(Math.round(s.getP(`macro${k + 1}_cc`)) || 'none')],
                        ['State', t.length ? 'on' : 'off'], ['Moves', t.join(', ') || 'nothing yet']] };
    }
    if (p.kind === 'kit') return { title: 'Kit', sub: 'sampler', row: {}, wide: true, params: [],
                                   fields: [['Type', 'source'], ['Kit', s.kitName || 'dropped files'], ['Samples', String(s.kit?.entries?.length ?? 0)]] };
    return null;
  });
  $effect(() => { s.panelTitle = P?.title ?? ''; });
  $effect(() => { if (s.panel && !P) s.closePanel(); }); // its item is gone (another instrument, an effect taken out)
  const step = (d) => {
    const i = s.tableRows.findIndex((r) => r.key === s.panel?.id), r = i >= 0 ? s.tableRows[i + d] : null;
    return r ? () => s.openPanel(r.kind, r.key) : null;
  };
</script>

<Tabs items={[{ id: 'settings', label: 'Settings' }, { id: 'general', label: 'General' }]} value={s.synthTab} onpick={(id) => (s.synthTab = id)} label="Sound" />
{#if s.synthTab === 'general'}<SoundGeneral onpick={pick} />{:else}<SynthTable />{/if}

{#if P}
  <SidePanel title={P.title} sub={P.sub} wide={P.wide} onclose={s.closePanel} onprev={step(-1)} onnext={step(1)} bind:tab={s.panelTab}>
    {#key s.panel.kind + s.panel.id}
      {#if s.panelTab === 'general'}<ModuleGeneral kind={s.panel.kind} row={P.row} params={P.params} fields={P.fields} />
      {:else if s.panel.kind === 'stage'}<StagePanel row={P.row} engine={P.engine} />
      {:else if s.panel.kind === 'mod'}<ModulatorPanel m={P.m} />
      {:else if s.panel.kind === 'kit'}<KitPanel />
      {:else}<MacroPanel k={P.k} />{/if}
    {/key}
  </SidePanel>
{/if}
