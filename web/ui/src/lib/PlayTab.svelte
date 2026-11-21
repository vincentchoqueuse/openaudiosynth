<!-- PlayTab.svelte — the Play tab: playing the sound, in three tabs: Visualizer (wave or spectrum), Macros (the eight
     sliders to play; their targets are set in the Synth tab) and Sequencer (the clip and its piano roll; it plays
     from the player bar). All three stay mounted: the scope measures the level the top bar shows. -->
<script>
  import Scope from './Scope.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './fold.js';
  const TABS = [['visual', 'Visualizer'], ['macros', 'Macros'], ['sequencer', 'Sequencer']];
  let tab = $state(remembered('synth.playtab', 'macros'));
  let live = $state(remembered('synth.live', 'wave')); // the visualizer: wave or spectrum
  const pick = (v) => { tab = v; remember('synth.playtab', v); };
  const pickLive = (v) => { live = v; remember('synth.live', v); };
</script>

<div class="tabs sub" role="tablist" aria-label="Play">
  {#each TABS as [id, name]}
    <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => pick(id)}>{name}</button>
  {/each}
  {#if tab === 'visual'}
    <div class="seg small tabtools" role="group" aria-label="Visualizer view">
      <button class:on={live === 'wave'} onclick={() => pickLive('wave')}>Wave</button>
      <button class:on={live === 'spectrum'} onclick={() => pickLive('spectrum')}>Spectrum</button>
    </div>
  {/if}
</div>

<div hidden={tab !== 'visual'}><Scope analyser={s.synth.analyser} view={live} /></div>
{#if tab === 'macros'}
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} />
{:else if tab === 'sequencer'}
  <SequenceTab />
{/if}
