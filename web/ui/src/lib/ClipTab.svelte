<!-- ClipTab.svelte — the Sequence view, a record with two tabs: Clip (Live, wave or spectrum, then the Sequencer: the
     clip, its tempo, loop and position, its piano roll; play / stop is in the top bar) and Code (the clip as text:
     CodeView). Kept mounted when hidden, the scope too: it measures the level the top bar shows. -->
<script>
  import Seg from './Seg.svelte';
  import Scope from './Scope.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import Tabs from './Tabs.svelte';
  import CodeView from './CodeView.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './prefs.js';
  let live = $state(remembered('synth.live', 'wave')); // the visualizer: wave or spectrum
  const pickLive = (v) => { live = v; remember('synth.live', v); };
  let tab = $state(remembered('synth.seqtab', 'clip'));
  $effect(() => remember('synth.seqtab', tab));
</script>

{#if !s.phone}<Tabs items={[{ id: 'clip', label: 'Clip' }, { id: 'code', label: 'Code' }]} value={tab} onpick={(id) => (tab = id)} label="Sequence" />{/if}
{#if tab === 'code' && !s.phone}<CodeView doc="clip" />{/if}
<div class="live" hidden={tab === 'code' && !s.phone}>
  <Scope analyser={s.synth.analyser} view={live} />
  <div class="livesw"><Seg items={[{ id: 'wave', label: 'Wave' }, { id: 'spectrum', label: 'Spectrum' }]} value={live} onpick={pickLive} label="Live view" /></div>
</div>

{#if !s.phone}<div hidden={tab === 'code'}><SequenceTab /></div>{/if} <!-- a phone plays the clip (top bar), it does not edit it -->

<style>
  .live { position: relative; }
  .livesw { position: absolute; top: 8px; right: 10px; }
</style>
