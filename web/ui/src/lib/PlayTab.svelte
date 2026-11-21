<!-- PlayTab.svelte — the Play tab: playing the sound. Live (its waveform and spectrum), Macros (the eight knobs, to
     play; their targets are set in the Synth tab) and Sequencer (the clip: play / stop, tempo, loop, position on its
     title line, then the clip and its piano roll). Kept mounted when hidden: the scope measures the level the top
     bar shows, and the clip keeps playing. -->
<script>
  import Section from './Section.svelte';
  import Scope from './Scope.svelte';
  import MacroStrip from './MacroStrip.svelte';
  import SequenceTab from './SequenceTab.svelte';
  import { session as s } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
</script>

<Section id="live" title="Live">
  <Scope analyser={s.synth.analyser} />
</Section>

<Section id="macros" title="Macros" summary={s.macroNames.filter(Boolean).join(' · ')}>
  <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
              bind:names={s.macroNames} baseline={s.baseline} editable={false} />
</Section>

<Section id="sequencer" title="Sequencer" summary="{q.name || 'untitled'} · {q.notes.length} notes · {q.length} beats">
  {#snippet tools()}
    <button class="play small" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Space">{q.playing ? '■' : '▶'}</button>
    <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
    <button class="loopbtn" class:on={q.loop} onclick={() => (q.loop = !q.loop)} aria-pressed={q.loop}
            title={q.loop ? `Loop on: the clip repeats every ${q.length} beats` : 'Loop off: the clip plays once'}>⟲ Loop</button>
    <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  {/snippet}
  <SequenceTab />
</Section>
