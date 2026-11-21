<!-- ClipTab.svelte — the Clip view: playing the sound. Live (wave or spectrum) always on top, then two tabs:
     Sequencer (the clip: its tempo, loop and position, its piano roll; play / stop is in the top bar) and Macros
     (the eight sliders to play; their targets are set in Synth). Kept mounted when hidden: the scope measures the level the top bar shows. -->
<script>
  import Scope from './Scope.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './fold.js';
  const TABS = [['sequencer', 'Sequencer'], ['macros', 'Macros']];
  let tab = $state(remembered('synth.playtab', 'sequencer'));
  if (tab !== 'macros') tab = 'sequencer';
  let live = $state(remembered('synth.live', 'wave')); // the visualizer: wave or spectrum
  const pick = (v) => { tab = v; remember('synth.playtab', v); };
  const pickLive = (v) => { live = v; remember('synth.live', v); };
</script>

<div class="live">
  <Scope analyser={s.synth.analyser} view={live} />
  <div class="seg small livesw" role="group" aria-label="Live view">
    <button class:on={live === 'wave'} onclick={() => pickLive('wave')}>Wave</button>
    <button class:on={live === 'spectrum'} onclick={() => pickLive('spectrum')}>Spectrum</button>
  </div>
</div>

<div class="tabs" role="tablist" aria-label="Clip">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => pick(id)}>{name}</button>
  {/each}
</div>

{#if tab === 'macros'}
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} />
{:else if tab === 'sequencer'}
  <SequenceTab />
{/if}

<style>
  .live { position: relative; }
  .livesw { position: absolute; top: 8px; right: 10px; }
</style>
