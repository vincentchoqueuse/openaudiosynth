<!-- SynthTab.svelte — the Synth view: shaping the sound. Pills, each with its own tabs under it: Summary (the
     signal path drawn: the instrument, the effect bus, the modulators, display only; and the sound's description,
     instrument and file), Engine (the instrument, named
     after it: a Randomize button that throws all its settings elsewhere, then its parameters, or the sampler's kit), Effects (the bus and the master), Modulation (the assignments, then a tab per modulator, a dot on those assigned), and Macros (assignments, then names and CC). The tab open
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
  import { MODS, drives } from './mods.js';

  // the pills, each with its editors under it as tabs (even a single one); the tab open is session.editorTab
  const GROUPS = $derived([
    { id: 'summary', label: 'Summary', icon: 'summary', tabs: [{ id: 'summary', label: 'Signal path' }, { id: 'info', label: 'Description' }] },
    { id: 'engine', label: 'Engine', icon: 'engine',
      tabs: [{ id: 'engine', label: s.engine?.name === 'va' ? 'VA' : cap(s.engine?.name) || 'Engine' }] },
    { id: 'fx', label: 'Effects', icon: 'effects', tabs: [{ id: 'fx', label: 'Bus' }] },
    { id: 'mod', label: 'Modulation', icon: 'modulation',
      tabs: [{ id: 'mod-assign', label: 'Assignments' },
             ...MODS.map(([label, , src, kind], i) => ({ id: 'mod' + i, label, dot: drives(src, kind) }))] },
    { id: 'macros', label: 'Macros', icon: 'macros',
      tabs: [{ id: 'macros-assign', label: 'Assignments' }, { id: 'macros', label: 'Names' }] },
  ]);
  const group = $derived(GROUPS.find((g) => g.tabs.some((t) => t.id === s.editorTab)) ?? GROUPS[0]);
</script>

<Pills items={GROUPS} value={group.id} onpick={(id) => s.goTo(GROUPS.find((g) => g.id === id).tabs[0].id)} tabs label="Synth" />
<Tabs items={group.tabs} value={s.editorTab} onpick={s.goTo} label={group.label} />

{#if s.editorTab === 'summary'}
  <ChainView />
{:else if s.editorTab === 'info'}
  <InfoPanel />
{:else if group.id === 'macros'}
  <MacroMatrix view={s.editorTab === 'macros' ? 'names' : 'assign'} />
{:else if group.id === 'mod'}
  <ModPanel view={s.editorTab === 'mod-assign' ? 'assign' : +s.editorTab.slice(3)} />
{:else if s.editorTab === 'engine' && s.engine?.name === 'sampler'}
  <KitPanel />
{:else}
  {#if s.editorTab === 'engine'}
    <div class="toolbar">
      <button class="ghost" onclick={s.randomize} title="Throw every setting of the instrument somewhere else">Randomize</button>
      <span>Changes every setting of the instrument at once. Undo brings the sound back.</span>
    </div>
  {/if}
  <ParamGroups tab={s.editorTab} />
{/if}
