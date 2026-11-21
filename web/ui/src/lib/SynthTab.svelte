<!-- SynthTab.svelte — the Synth tab: shaping the sound. Structure (its signal chain) and Sound (its editors in
     tabs: Description, Macros with their targets, Sound, Effects, Part, Modulation). -->
<script>
  import Section from './Section.svelte';
  import ChainView from './ChainView.svelte';
  import EditorTabs from './EditorTabs.svelte';
  import { session as s } from './session.svelte.js';
</script>

<Section id="structure" title="Structure">
  <ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} />
</Section>

<Section id="sound" title="Sound">
  <EditorTabs />
</Section>
