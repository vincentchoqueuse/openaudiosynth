<!-- ViewSection.svelte — the Visualize section: what the sound makes, in two tabs, Live (waveform and spectrum)
     and Structure (the signal chain), folded away on a click. The scope stays running when hidden: it measures
     the level the transport's meter shows. -->
<script>
  import Scope from './Scope.svelte';
  import ChainView from './ChainView.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './fold.js';
  let view = $state(remembered('synth.view', 'live'));
  let open = $state(remembered('synth.view.open', true));
  const pick = (v) => { view = v; open = true; remember('synth.view', v); remember('synth.view.open', true); };
  const fold = () => { open = !open; remember('synth.view.open', open); };
</script>

<section class="section">
  <div class="section-head">
    <h2>Visualize</h2>
    <div class="seg small">
      <button class:on={open && view === 'live'} onclick={() => pick('live')}>Live</button>
      <button class:on={open && view === 'structure'} onclick={() => pick('structure')}>Structure</button>
    </div>
    <button class="fold" onclick={fold} aria-expanded={open} title={open ? 'Fold' : 'Unfold'}>{open ? '˄' : '˅'}</button>
  </div>
  <div hidden={!open || view !== 'live'}><Scope analyser={s.synth.analyser} /></div>
  {#if open && view === 'structure'}
    <ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} />
  {/if}
</section>
