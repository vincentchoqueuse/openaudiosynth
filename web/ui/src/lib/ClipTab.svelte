<!-- ClipTab.svelte — the Clip view: Live (wave or spectrum) on top, then the Sequencer (the clip: its tempo, loop
     and position, its piano roll; play / stop is in the top bar). The macros to play are above the keyboard. Kept
     mounted when hidden: the scope measures the level the top bar shows. -->
<script>
  import Scope from './Scope.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './fold.js';
  let live = $state(remembered('synth.live', 'wave')); // the visualizer: wave or spectrum
  const pickLive = (v) => { live = v; remember('synth.live', v); };
</script>

<div class="live">
  <Scope analyser={s.synth.analyser} view={live} />
  <div class="seg small livesw" role="group" aria-label="Live view">
    <button class:on={live === 'wave'} onclick={() => pickLive('wave')}>Wave</button>
    <button class:on={live === 'spectrum'} onclick={() => pickLive('spectrum')}>Spectrum</button>
  </div>
</div>

<SequenceTab />

<style>
  .live { position: relative; }
  .livesw { position: absolute; top: 8px; right: 10px; }
</style>
