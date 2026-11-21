<!-- SynthTab.svelte — the Synth view: shaping the sound. Its Structure always on top (the signal chain; a click on a
     block opens the tab and the card that hold its settings), then tabs: Info (its description, instrument, file), Macros (names, CC and targets), the engine (named after the
     instrument: its parameters, or the sampler's kit), Effects (the bus) and Modulation (modulators and their
     assignments). The tab open lives in the session; a sound picked in the library opens on Macros. -->
<script>
  import ChainView from './ChainView.svelte';
  import ParamGroups from './ParamGroups.svelte';
  import ModPanel from './ModPanel.svelte';
  import KitPanel from './KitPanel.svelte';
  import MacroMatrix from './MacroMatrix.svelte';
  import InfoPanel from './InfoPanel.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const cap = (w) => (w ? w.charAt(0).toUpperCase() + w.slice(1) : 'Engine');
  const TABS = $derived([['info', 'Info'], ['macros', 'Macros'], ['engine', s.engine?.name === 'va' ? 'VA' : cap(s.engine?.name)],
    ['fx', 'Effects'], ['mod', 'Modulation']]);
  // a block of the diagram -> the tab and the card that hold its settings
  function pick(st, kind) {
    if (kind === 'route') return s.goTo('mod');
    const bus = (s.catalog.fx ?? []).some((f) => f.id === st.id);
    s.goTo(bus ? 'fx' : 'engine', st.id);
  }
</script>

<ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} onpick={pick} />

<div class="tabs" role="tablist" aria-label="Synth">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={s.editorTab === id} class:on={s.editorTab === id} onclick={() => s.goTo(id)}>{name}</button>
  {/each}
</div>

{#if s.editorTab === 'info'}
  <InfoPanel />
{:else if s.editorTab === 'macros'}
  <MacroMatrix engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
               bind:names={s.macroNames} />
{:else if s.editorTab === 'mod'}
  <ModPanel engine={s.engine} shared={s.shared} get={s.getP} set={s.setP} byName={s.byName} {human} values={s.values} setValue={s.setValue} />
{:else if s.editorTab === 'engine' && s.engine?.name === 'sampler'}
  <KitPanel kit={s.kit} kitName={s.kitLabel} byName={s.byName} values={s.values} setValue={s.setValue} synth={s.synth} onfiles={s.dropFiles} />
{:else}
  <ParamGroups tab={s.editorTab} />
{/if}
