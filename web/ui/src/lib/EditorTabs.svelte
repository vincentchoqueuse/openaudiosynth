<!-- EditorTabs.svelte — the editors of the current sound in tabs: Sound (the engine's parameters, or the sampler's
     kit), Effects, Part, Modulation (envelopes and routes) and JSON (the patch file). A new sound opens on Sound. -->
<script>
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import JsonPanel from './JsonPanel.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const TABS = [['sound', 'Sound'], ['fx', 'Effects'], ['part', 'Part'], ['mod', 'Modulation'], ['json', 'JSON']];
  let tab = $state('sound');
  $effect(() => { s.current; tab = 'sound'; }); // a sound picked in the library opens on its parameters
</script>

<div class="tabs" role="tablist">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => (tab = id)}>{name}</button>
  {/each}
  {#if tab === 'sound'}<button class="link small" onclick={s.resetEngine}>Defaults</button>{/if}
</div>

{#if tab === 'json'}
  <JsonPanel getPatch={s.patchWithAudition} apply={s.applyFromEditor} />
{:else if tab === 'mod'}
  <ModPanel engine={s.engine} shared={s.shared} get={s.getP} set={s.setP} byName={s.byName} {human} values={s.values} setValue={s.setValue} />
{:else if tab === 'sound' && s.engine?.name === 'sampler'}
  <KitPanel kit={s.kit} kitName={s.kitLabel} byName={s.byName} values={s.values} setValue={s.setValue} synth={s.synth} onfiles={s.dropFiles} />
{:else}
  <ParamGroups {tab} />
{/if}
