<!-- SynthTab.svelte — the Synth view: what the sound is (InfoPanel), its signal path drawn (ChainView: only what is
     active; a click on a block opens it), then every module of the sound in one table with a search and tags
     (SynthTable). Configure on a row opens the module in the side panel (SidePanel), the same way for everything:
     Settings (a stage's settings, a modulator's editor and what it moves, a macro's targets, the sampler's kit) and
     About (what it does, where its signal goes, what each setting means). The panel lies over the page, stays open
     while the table is searched, and its arrows step through the rows as they are shown. -->
<script>
  import ChainView from './ChainView.svelte';
  import KitPanel from './KitPanel.svelte';
  import InfoPanel from './InfoPanel.svelte';
  import SidePanel from './SidePanel.svelte';
  import SynthTable from './SynthTable.svelte';
  import StagePanel from './StagePanel.svelte';
  import ModulatorPanel from './ModulatorPanel.svelte';
  import MacroPanel from './MacroPanel.svelte';
  import AboutPanel from './AboutPanel.svelte';
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
  // what the panel shows: its title, what it is, and for About its links and settings
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
      return { title: row.title, sub: inEng ? `${s.engine.name} · ${(ROLE[row.st?.kind] ?? 'stage').toLowerCase()}` : 'effect', row, engine: inEng,
               params: row.ps, links: [...(from ? [['From', from]] : []), ...(to ? [['To', to]] : [])] };
    }
    if (p.kind === 'mod') {
      const m = modRows().find((x) => x.key === p.id);
      if (!m) return null;
      const moves = routesOf(m).map((x) => `${R.dstName(s, x)} ${R.inUnits(s, x)}`);
      const params = m.prefix ? s.shared.filter((d) => d.name.startsWith(m.prefix)).filter((d) => !/\d+$/.test(d.name.slice(m.prefix.length)))
                                 .map((d) => ({ d, label: human(d.name.slice(m.prefix.length)) }))
                    : m.key === 'wander' ? [{ d: s.byName.wander, label: 'Amount' }, { d: s.byName.wander_rate, label: 'Rate' }] : [];
      return { title: m.label, sub: `${m.type.toLowerCase()} · ${m.kinds.map((k) => (k === 'part' ? 'whole sound' : 'each note')).join(' and ')}`,
               m, row: m, wide: !m.ctl && m.key !== 'wander', params, links: [['Moves', moves.join(', ') || 'nothing yet']] };
    }
    if (p.kind === 'macro') {
      const k = +p.id.slice(1);
      const t = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
      return { title: s.macroName(k), sub: `macro ${k + 1}`, k, row: {}, wide: true, params: [], links: [['Moves', t.join(', ') || 'nothing yet']] };
    }
    if (p.kind === 'kit') return { title: 'Kit', sub: 'sampler', row: {}, wide: true, params: [], links: [] };
    return null;
  });
  $effect(() => { if (s.panel && !P) s.closePanel(); }); // its item is gone (another instrument, an effect taken out)
  const step = (d) => {
    const i = s.tableRows.findIndex((r) => r.key === s.panel?.id), r = i >= 0 ? s.tableRows[i + d] : null;
    return r ? () => s.openPanel(r.kind, r.key) : null;
  };
</script>

<InfoPanel />
<ChainView onpick={pick} />
<SynthTable />

{#if P}
  <SidePanel title={P.title} sub={P.sub} wide={P.wide} onclose={s.closePanel} onprev={step(-1)} onnext={step(1)} bind:tab={s.panelTab}>
    {#key s.panel.kind + s.panel.id}
      {#if s.panelTab === 'about'}<AboutPanel kind={s.panel.kind} row={P.row} params={P.params} links={P.links} />
      {:else if s.panel.kind === 'stage'}<StagePanel row={P.row} engine={P.engine} />
      {:else if s.panel.kind === 'mod'}<ModulatorPanel m={P.m} />
      {:else if s.panel.kind === 'kit'}<KitPanel />
      {:else}<MacroPanel k={P.k} />{/if}
    {/key}
  </SidePanel>
{/if}
