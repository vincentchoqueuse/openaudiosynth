<!-- SynthTab.svelte — the Synth view: shaping the sound, every page built the same way. Five pills: Overview (what
     the sound is, and its signal path drawn), Instrument (its stages; the sampler shows its kit), Effects (the
     chain: input, six blocks, output), Modulation (the modulators and the routes) and Macros. Each page is a table
     (DataTable) whose rows say what an item is and does; Configure on a row, or a click on a block of the signal
     path, opens everything about the item in the side panel on the right (SidePanel), which stays open from page to
     page and steps through its table's rows with its arrows. The page open lives in the session. -->
<script>
  import ChainView from './ChainView.svelte';
  import KitPanel from './KitPanel.svelte';
  import InfoPanel from './InfoPanel.svelte';
  import Pills from './Pills.svelte';
  import SidePanel from './SidePanel.svelte';
  import InstrumentTable from './InstrumentTable.svelte';
  import EffectsTable from './EffectsTable.svelte';
  import ModTable from './ModTable.svelte';
  import MacroTable from './MacroTable.svelte';
  import StagePanel from './StagePanel.svelte';
  import ModulatorPanel from './ModulatorPanel.svelte';
  import RoutePanel from './RoutePanel.svelte';
  import MacroPanel from './MacroPanel.svelte';
  import { session as s } from './session.svelte.js';
  import { modRows } from './mods.js';
  import { engineStages, fxStages, stageOn, ROLE } from './stages.js';
  import * as R from './routes.js';

  const PAGES = [
    { id: 'summary', label: 'Overview', icon: 'summary' }, { id: 'engine', label: 'Instrument', icon: 'engine' },
    { id: 'fx', label: 'Effects', icon: 'effects' }, { id: 'mod', label: 'Modulation', icon: 'modulation' },
    { id: 'macros', label: 'Macros', icon: 'macros' },
  ];
  const page = $derived(PAGES.some((p) => p.id === s.editorTab) ? s.editorTab : 'summary');

  // a block or a modulator of the signal path clicked: its settings open in the panel
  function pick(st, kind) {
    if (kind === 'route') return s.goTo('mod');
    s.openPanel('stage', st.id);
  }

  // what the panel shows: its title, what it is, its body, and the rows of its table to step through
  const P = $derived.by(() => {
    const p = s.panel;
    if (!p) return null;
    if (p.kind === 'stage') {
      const eng = engineStages(s), fx = fxStages(s), inEng = eng.some((x) => x.key === p.id);
      const list = inEng ? eng : fx.filter((x) => !s.rack.includes(x.key) || stageOn(s, x.st) || x.key === p.id);
      const row = list.find((x) => x.key === p.id);
      return row && { title: row.title, sub: inEng ? `${s.engine.name} · ${(ROLE[row.st?.kind] ?? 'stage').toLowerCase()}` : s.rack.includes(row.key) ? 'effect' : 'effect chain',
                      row, engine: inEng, keys: list.map((x) => x.key) };
    }
    if (p.kind === 'mod') {
      const list = modRows(), m = list.find((x) => x.key === p.id);
      return m && { title: m.label, sub: `${m.type} · ${m.kind === 'part' ? 'whole sound' : 'each note'}`, m, wide: m.key !== 'wander', keys: list.map((x) => x.key) };
    }
    if (p.kind === 'route') {
      const x = R.get(s, p.id);
      const keys = [...R.keys('part'), ...R.keys('voice')].filter((k) => R.inUse(s, k) || k === p.id);
      return { title: x.src ? `${R.srcName(x.srcLabel)} → ${R.dstName(s, x)}` : 'New route', sub: `Route ${x.r} · ${x.kind === 'part' ? 'whole sound' : 'each note'}`, keys };
    }
    if (p.kind === 'macro') {
      const k = +p.id.slice(1);
      return { title: s.macroName(k), sub: `Macro ${k + 1}`, k, keys: Array.from({ length: 8 }, (_, i) => 'm' + i) };
    }
    return null;
  });
  $effect(() => { if (s.panel && !P) s.closePanel(); }); // its item is gone (another instrument, an effect taken out)
  const step = (d) => { const i = P.keys.indexOf(s.panel.id), k = P.keys[i + d]; return k ? () => s.openPanel(s.panel.kind, k) : null; };
</script>

<div class="synthpage" class:paneled={!!P} class:widepanel={P?.wide}>
  <Pills items={PAGES} value={page} onpick={(id) => s.goTo(id)} tabs label="Synth" />

  {#if page === 'summary'}
    <InfoPanel />
    <ChainView onpick={pick} />
  {:else if page === 'engine'}
    {#if s.engine?.name === 'sampler'}<KitPanel />{:else}<InstrumentTable />{/if}
  {:else if page === 'fx'}
    <EffectsTable />
  {:else if page === 'mod'}
    <ModTable />
  {:else}
    <MacroTable />
  {/if}
</div>

{#if P}
  <SidePanel title={P.title} sub={P.sub} wide={P.wide} onclose={s.closePanel} onprev={step(-1)} onnext={step(1)}>
    {#key s.panel.kind + s.panel.id}
      {#if s.panel.kind === 'stage'}<StagePanel row={P.row} engine={P.engine} />
      {:else if s.panel.kind === 'mod'}<ModulatorPanel m={P.m} />
      {:else if s.panel.kind === 'route'}<RoutePanel id={s.panel.id} />
      {:else}<MacroPanel k={P.k} />{/if}
    {/key}
  </SidePanel>
{/if}
