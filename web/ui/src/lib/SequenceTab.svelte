<!-- SequenceTab.svelte — the clip in the Play tab's Sequencer: a first toolbar for the clip itself (which one,
     saved in this browser: New, Save, Delete; Random, Clear; Export song), then the piano roll with its toolbar
     (the clip's length, grid, key and octave on the left, the editing tools on the right). -->
<script>
  import PianoRoll from './PianoRoll.svelte';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, GRIDS } from './seqExport.js';
  const BARS = [1, 2, 4, 8, 16], OCTAVES = [1, 2, 3, 4, 5];
</script>

<div class="toolbar">
  <label>Clip
    <select value={q.saved[q.name] ? q.name : ''} onchange={(e) => q.pick(e.target.value)}>
      <option value="">{q.name ? q.name + (q.saved[q.name] ? '' : ' (not saved)') : 'untitled'}</option>
      {#each Object.keys(q.saved).sort() as n}<option value={n}>{n}</option>{/each}
    </select>
  </label>
  <button class="ghost" onclick={q.fresh}>New</button>
  <button class="ghost" onclick={q.save}>Save</button>
  <button class="ghost" onclick={q.remove} disabled={!q.saved[q.name]}>Delete</button>
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
<p class="hint">Draw: click, drag to stretch · Select: drag a rectangle (or Cmd-drag) · Shift-click: add to the selection ·
  drag notes to move them, their right edge to stretch them, Alt-drag or the lane below for velocity · click in the
  roll, then: Delete, Cmd+A, Cmd+C / X / V, Cmd+D duplicate, arrows move (Shift: octave), Cmd+U quantize · Cmd+Z undo</p>
