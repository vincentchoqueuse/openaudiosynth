<!-- EditorTabs.svelte — the Synth tab's Sound section: the sound's editors in tabs, Description, Macros (their
     names and targets), Sound (the engine's parameters, or the sampler's kit), Effects, Part, Modulation
     (envelopes, LFOs and the matrix). A sound picked in the library opens on Macros. -->
<script>
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import DescriptionTab from './DescriptionTab.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const TABS = [['desc', 'Description'], ['macros', 'Macros'], ['sound', 'Sound'], ['fx', 'Effects'], ['part', 'Part'], ['mod', 'Modulation']];
  let tab = $state('macros');
  $effect(() => { s.picks; tab = 'macros'; }); // a sound picked in the library opens on its live controls
</script>

<div class="tabs" role="tablist">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => (tab = id)}>{name}</button>
  {/each}
  {#if tab === 'sound'}<button class="link small" onclick={s.resetEngine}>Defaults</button>{/if}
</div>

{#if tab === 'desc'}
  <DescriptionTab />
{:else if tab === 'macros'}
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} />

{:else if tab === 'mod'}
  <ModPanel engine={s.engine} shared={s.shared} get={s.getP} set={s.setP} byName={s.byName} {human} values={s.values} setValue={s.setValue} />
{:else if tab === 'sound' && s.engine?.name === 'sampler'}
  <KitPanel kit={s.kit} kitName={s.kitLabel} byName={s.byName} values={s.values} setValue={s.setValue} synth={s.synth} onfiles={s.dropFiles} />
{:else}
  <ParamGroups {tab} />
{/if}
