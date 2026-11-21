<!-- PlayTab.svelte — the Play tab: playing the sound. The transport first, at the top (play / stop the clip,
     tempo, loop, position), then Live (wave or spectrum), Macros (the eight knobs, to play; their targets are set in
     the Synth tab) and Sequencer (the clip and its piano roll). Kept mounted when hidden: the scope measures the level the top
     bar shows, and the clip keeps playing. -->
<script>
  import Section from './Section.svelte';
  import Scope from './Scope.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { remembered, remember } from './fold.js';
  let live = $state(remembered('synth.live', 'wave')); // the Live view: wave or spectrum
  const pickLive = (v) => { live = v; remember('synth.live', v); };
</script>

<!-- the transport first: where the clip is played from -->
<div class="transport toolbar" role="toolbar" aria-label="Transport">
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Space">{q.playing ? '■' : '▶'}</button>
  <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <button class="loopbtn" class:on={q.loop} onclick={() => (q.loop = !q.loop)} aria-pressed={q.loop}
          title={q.loop ? `Loop on: the clip repeats every ${q.length} beats` : 'Loop off: the clip plays once'}>⟲ Loop</button>
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <span class="muted">{q.name || 'untitled'} · {q.notes.length} notes · {q.length} beats</span>
</div>

<Section id="live" title="Live">
  {#snippet tools()}
    <div class="seg small" role="group" aria-label="Live view">
      <button class:on={live === 'wave'} onclick={() => pickLive('wave')}>Wave</button>
      <button class:on={live === 'spectrum'} onclick={() => pickLive('spectrum')}>Spectrum</button>
    </div>
  {/snippet}
  <Scope analyser={s.synth.analyser} view={live} />
</Section>

<Section id="macros" title="Macros" summary={s.macroNames.filter(Boolean).join(' · ')}>
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} />
</Section>

<Section id="sequencer" title="Sequencer" summary="{q.name || 'untitled'} · {q.notes.length} notes · {q.length} beats">
  <SequenceTab />
</Section>
