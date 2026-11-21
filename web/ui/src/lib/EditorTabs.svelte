<!-- EditorTabs.svelte — everything about the sound in tabs: Description, Macros (its live controls), Sound (the
     engine's parameters, or the sampler's kit), Effects, Part, Modulation (envelopes and routes), Sequence (its
     clip) and Code (the documents it is made of, in a code editor). A sound picked in the library opens on Macros. -->
<script>
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import JsonWorkspace from './JsonWorkspace.svelte';
  import DescriptionTab from './DescriptionTab.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import { sequence } from './sequence.svelte.js';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const TABS = [['desc', 'Description'], ['macros', 'Macros'], ['sound', 'Sound'], ['fx', 'Effects'], ['part', 'Part'], ['mod', 'Modulation'], ['seq', 'Sequence'], ['code', 'Code']];
  let tab = $state('macros');
  $effect(() => { s.picks; tab = 'macros'; }); // a sound picked in the library opens on its live controls
</script>

<div class="tabs" role="tablist">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => (tab = id)}>{name}{#if id === 'seq' && sequence.playing}<span class="live" title="Playing"></span>{/if}</button>
  {/each}
  {#if tab === 'sound'}<button class="link small" onclick={s.resetEngine}>Defaults</button>{/if}
</div>

{#if tab === 'code'}
  <JsonWorkspace />
{:else if tab === 'desc'}
  <DescriptionTab />
{:else if tab === 'macros'}
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} />
{:else if tab === 'seq'}
  <SequenceTab />
{:else if tab === 'mod'}
  <ModPanel engine={s.engine} shared={s.shared} get={s.getP} set={s.setP} byName={s.byName} {human} values={s.values} setValue={s.setValue} />
{:else if tab === 'sound' && s.engine?.name === 'sampler'}
  <KitPanel kit={s.kit} kitName={s.kitLabel} byName={s.byName} values={s.values} setValue={s.setValue} synth={s.synth} onfiles={s.dropFiles} />
{:else}
  <ParamGroups {tab} />
{/if}
