<!-- SynthTab.svelte — the Synth tab: shaping the sound. Structure (its signal chain, which is also the editor's map:
     a click on a block opens its settings) and Editor (Macros, the engine, Effects, Modulation). -->
<script>
  import Section from './Section.svelte';
  import ChainView from './ChainView.svelte';
  import EditorTabs from './EditorTabs.svelte';
  import { session as s } from './session.svelte.js';
  // a block of the diagram -> the tab and the card that hold its settings
  function pick(st, kind) {
    if (kind === 'route') return s.goTo('mod');
    const bus = (s.catalog.fx ?? []).some((f) => f.id === st.id);
    s.goTo(bus ? 'fx' : 'engine', st.id);
  }
</script>

<Section id="structure" title="Structure">
  <ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} onpick={pick} />
</Section>

<Section id="editor" title="Editor">
  <EditorTabs />
</Section>
