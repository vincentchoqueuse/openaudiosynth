<!-- SynthTab.svelte — the Synth view: shaping the sound. Five pills: Overview (what the sound is: its description,
     its instrument, and the signal path drawn: the instrument, the effects, the modulators; a click on a block opens
     its settings), Instrument (Randomize and Instrument defaults, then its parameters, or the sampler's kit),
     Effects (the rack and the master), Modulation (the only pill with tabs: every assignment, then one tab per
     modulator, a dot on those assigned) and Macros (their names, what each moves, and Wander). The tab open lives
     in the session and stays when another sound is picked. -->
<script>
  import ChainView from './ChainView.svelte';
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import MacroMatrix from './MacroMatrix.svelte';
  import InfoPanel from './InfoPanel.svelte';
  import { session as s } from './session.svelte.js';
  import Pills from './Pills.svelte';
  import Tabs from './Tabs.svelte';
  import { MODS, drives } from './mods.js';

  // the pills; `tabs` only where there is something to choose (the tab open is session.editorTab)
  const GROUPS = $derived([
    { id: 'summary', label: 'Overview', icon: 'summary', tabs: [{ id: 'summary' }] },
    { id: 'engine', label: 'Instrument', icon: 'engine', tabs: [{ id: 'engine' }] },
    { id: 'fx', label: 'Effects', icon: 'effects', tabs: [{ id: 'fx' }] },
    { id: 'mod', label: 'Modulation', icon: 'modulation',
      tabs: [{ id: 'mod-assign', label: 'All assignments' },
             ...MODS.map(([label, , src, kind], i) => ({ id: 'mod' + i, label, dot: drives(src, kind) }))] },
    { id: 'macros', label: 'Macros', icon: 'macros', tabs: [{ id: 'macros-assign' }] },
  ]);
  const group = $derived(GROUPS.find((g) => g.tabs.some((t) => t.id === s.editorTab)) ?? GROUPS[0]);
  // a block or a modulator of the signal path clicked: its settings open (its card is scrolled to and lit)
  function pick(st, kind) {
    if (kind === 'route') return s.goTo('mod-assign');
    s.goTo(s.engine?.stages?.some((x) => x.id === st.id) ? 'engine' : 'fx', st.id);
  }
</script>

<Pills items={GROUPS} value={group.id} onpick={(id) => s.goTo(GROUPS.find((g) => g.id === id).tabs[0].id)} tabs label="Synth" />
{#if group.tabs.length > 1}<Tabs items={group.tabs} value={s.editorTab} onpick={s.goTo} label={group.label} />{/if}

{#if group.id === 'summary'}
  <InfoPanel />
  <ChainView onpick={pick} />
{:else if group.id === 'macros'}
  <MacroMatrix />
{:else if group.id === 'mod'}
  <ModPanel view={s.editorTab === 'mod-assign' ? 'assign' : +s.editorTab.slice(3)} />
{:else if group.id === 'engine' && s.engine?.name === 'sampler'}
  <KitPanel />
{:else}
  {#if group.id === 'engine'}
    <div class="toolbar">
      <button class="ghost" onclick={s.randomize} title="Throw every setting of the instrument somewhere else (Undo brings the sound back)">Randomize</button>
      <button class="ghost" onclick={s.resetEngine} title="Every setting of the instrument back to its default (the effects, the modulation and the macros stay)">Instrument defaults</button>
    </div>
  {/if}
  <ParamGroups tab={group.id} />
{/if}
