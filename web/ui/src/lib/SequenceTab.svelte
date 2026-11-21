<!-- SequenceTab.svelte — the clip in the Play tab's Sequencer (it belongs to the sound: Save in the top bar keeps
     both): a first toolbar (Random, Clear, Export song), then the piano roll with its toolbar (the clip's length,
     grid, key and octave on the left, the editing tools on the right). -->
<script>
  import PianoRoll from './PianoRoll.svelte';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, GRIDS } from './seqExport.js';
  const BARS = [1, 2, 4, 8, 16], OCTAVES = [1, 2, 3, 4, 5];
</script>

<div class="toolbar">
  <span class="muted">The clip is saved with the sound.</span>
  <span class="spacer"></span>
  <button class="ghost" onclick={q.random}>Random</button>
  <button class="ghost" onclick={q.clear}>Clear</button>
  <select bind:value={q.loops} aria-label="Times the clip plays in the exported song">{#each LOOPS as n}<option value={n}>× {n}</option>{/each}</select>
  <button class="ghost" onclick={q.exportSong}>Export song</button>
</div>
{#if q.message}<p class="message">{q.message}</p>{/if}
<PianoRoll>
  {#snippet settings()}
    <label>Length <select value={q.length} onchange={(e) => (q.length = +e.target.value)}>
      {#if !BARS.some((b) => b * 4 === q.length)}<option value={q.length}>{q.length} beats</option>{/if}
      {#each BARS as b}<option value={b * 4}>{b} bar{b > 1 ? 's' : ''}</option>{/each}
    </select></label>
    <label>Grid <select bind:value={q.grid}>{#each GRIDS as [l, v]}<option value={v}>{l}</option>{/each}</select></label>
    <label>Key <select bind:value={q.root}>{#each NAMES as n, i}<option value={i}>{n}</option>{/each}</select>
      <select bind:value={q.scale}>{#each Object.keys(SCALES) as sc}<option value={sc}>{sc}</option>{/each}</select></label>
    <label>Octave <select bind:value={q.octave}>{#each OCTAVES as o}<option value={o}>{o}</option>{/each}</select></label>
  {/snippet}
</PianoRoll>
<p class="hint">Click in the roll to edit it with the keyboard; every shortcut: press ?</p>
