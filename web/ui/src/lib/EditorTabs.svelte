<!-- EditorTabs.svelte — the Synth tab's Editor: the sound's editors in tabs, Macros (names, CC and targets), the
     engine (named after the instrument: its parameters, or the sampler's kit), Effects (the bus) and Modulation
     (envelopes, LFOs and the matrix). The tab open lives in the session: the Structure diagram opens one (and a
     stage in it) when a block is clicked; a sound picked in the library opens on Macros. The part's MIDI and
     tuning settings are in Settings (the gear in the sidebar). -->
<script>
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import MacroMatrix from './MacroMatrix.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const cap = (w) => (w ? w.charAt(0).toUpperCase() + w.slice(1) : 'Engine');
  const TABS = $derived([['macros', 'Macros'], ['engine', s.engine?.name === 'va' ? 'VA' : cap(s.engine?.name)], ['fx', 'Effects'], ['mod', 'Modulation']]);
</script>

<div class="tabs" role="tablist">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={s.editorTab === id} class:on={s.editorTab === id} onclick={() => s.goTo(id)}>{name}</button>
  {/each}
  {#if s.editorTab === 'engine'}<button class="link small" onclick={s.resetEngine} title="the instrument's default sound">Defaults</button>{/if}
</div>

{#if s.editorTab === 'macros'}
  <MacroMatrix engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
               bind:names={s.macroNames} />
{:else if s.editorTab === 'mod'}
  <ModPanel engine={s.engine} shared={s.shared} get={s.getP} set={s.setP} byName={s.byName} {human} values={s.values} setValue={s.setValue} />
{:else if s.editorTab === 'engine' && s.engine?.name === 'sampler'}
  <KitPanel kit={s.kit} kitName={s.kitLabel} byName={s.byName} values={s.values} setValue={s.setValue} synth={s.synth} onfiles={s.dropFiles} />
{:else}
  <ParamGroups tab={s.editorTab} />
{/if}
