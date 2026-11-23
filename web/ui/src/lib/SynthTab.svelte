<!-- SynthTab.svelte — the Synth view: shaping the sound. Pills, each with its own tabs under it: Summary (the
     signal chain drawn: the instrument, the effect bus, the modulators; display only), Engine (the instrument, named
     after it: its parameters, or the sampler's kit), Effects (the bus and the master), Modulation (modulators and
     their assignments), Macros (names, CC and targets) and Info (its description, instrument, file). The tab open
     lives in the session; a sound picked in the library opens on Summary. -->
<script>
  import { cap } from './naming.js';
  import ChainView from './ChainView.svelte';
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import MacroMatrix from './MacroMatrix.svelte';
  import InfoPanel from './InfoPanel.svelte';
  import { session as s } from './session.svelte.js';
  import Pills from './Pills.svelte';
  import Tabs from './Tabs.svelte';

  // the pills, each with its editors under it as tabs (even a single one); the tab open is session.editorTab
  const GROUPS = $derived([
    { id: 'summary', label: 'Summary', icon: 'summary', tabs: [{ id: 'summary', label: 'Signal chain' }] },
    { id: 'engine', label: 'Engine', icon: 'engine',
      tabs: [{ id: 'engine', label: s.engine?.name === 'va' ? 'VA' : cap(s.engine?.name) || 'Engine' }] },
    { id: 'fx', label: 'Effects', icon: 'effects', tabs: [{ id: 'fx', label: 'Bus' }] },
    { id: 'mod', label: 'Modulation', icon: 'modulation',
      tabs: [{ id: 'mod', label: 'Modulators' }, { id: 'mod-assign', label: 'Assignments' }] },
    { id: 'macros', label: 'Macros', icon: 'macros',
      tabs: [{ id: 'macros', label: 'Names' }, { id: 'macros-assign', label: 'Assignments' }] },
    { id: 'info', label: 'Info', icon: 'info', tabs: [{ id: 'info', label: 'Description' }] },
  ]);
  const group = $derived(GROUPS.find((g) => g.tabs.some((t) => t.id === s.editorTab)) ?? GROUPS[0]);
</script>

<Pills items={GROUPS} value={group.id} onpick={(id) => s.goTo(id)} tabs label="Synth" />
<Tabs items={group.tabs} value={s.editorTab} onpick={s.goTo} label={group.label} />

{#if s.editorTab === 'summary'}
  <ChainView />
{:else if s.editorTab === 'info'}
  <InfoPanel />
{:else if group.id === 'macros'}
  <MacroMatrix view={s.editorTab === 'macros' ? 'names' : 'assign'} />
{:else if group.id === 'mod'}
  <ModPanel view={s.editorTab === 'mod' ? 'mods' : 'assign'} />
{:else if s.editorTab === 'engine' && s.engine?.name === 'sampler'}
  <KitPanel />
{:else}
  <ParamGroups tab={s.editorTab} />
{/if}
