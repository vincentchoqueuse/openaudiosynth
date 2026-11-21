<!-- ClipTab.svelte — the Clip view: Live (wave or spectrum) on top, then the Sequencer (the clip: its tempo, loop
     and position, its piano roll; play / stop is in the top bar). The macros to play are above the keyboard. Kept
     mounted when hidden: the scope measures the level the top bar shows. -->
<script>
  import Seg from './Seg.svelte';
  import Scope from './Scope.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './prefs.js';
  let live = $state(remembered('synth.live', 'wave')); // the visualizer: wave or spectrum
  const pickLive = (v) => { live = v; remember('synth.live', v); };
</script>

<div class="live">
  <Scope analyser={s.synth.analyser} view={live} />
  <div class="livesw"><Seg items={[{ id: 'wave', label: 'Wave' }, { id: 'spectrum', label: 'Spectrum' }]} value={live} onpick={pickLive} label="Live view" /></div>
</div>

<SequenceTab />

<style>
  .live { position: relative; }
  .livesw { position: absolute; top: 8px; right: 10px; }
</style>
