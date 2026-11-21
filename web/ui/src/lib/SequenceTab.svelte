<!-- SequenceTab.svelte — the Sequence tab: which clip (saved in this browser; New, Save, Delete), its length in
     bars, the grid it snaps to, the key the piano roll shows and its octave, the piano roll, and Random, Clear,
     Export song. Play, tempo, loop and position are in the transport; its JSON is in the JSON tab. -->
<script>
  import PianoRoll from './PianoRoll.svelte';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, GRIDS } from './seqExport.js';
  const BARS = [1, 2, 4, 8, 16], OCTAVES = [1, 2, 3, 4, 5];
</script>

<div class="seq">
  <div class="seq-bar">
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
  <div class="seq-bar">
    <label>Length <select value={q.length} onchange={(e) => (q.length = +e.target.value)}>
      {#if !BARS.some((b) => b * 4 === q.length)}<option value={q.length}>{q.length} beats</option>{/if}
      {#each BARS as b}<option value={b * 4}>{b} bar{b > 1 ? 's' : ''}</option>{/each}
    </select></label>
    <label>Grid <select bind:value={q.grid}>{#each GRIDS as [l, v]}<option value={v}>{l}</option>{/each}</select></label>
    <label>Key <select bind:value={q.root}>{#each NAMES as n, i}<option value={i}>{n}</option>{/each}</select>
      <select bind:value={q.scale}>{#each Object.keys(SCALES) as s}<option value={s}>{s}</option>{/each}</select></label>
    <label>Octave <select bind:value={q.octave}>{#each OCTAVES as o}<option value={o}>{o}</option>{/each}</select></label>
    <span class="spacer"></span>
    <span class="muted">{q.notes.length} note{q.notes.length === 1 ? '' : 's'}</span>
  </div>
  {#if q.message}<p class="message">{q.message}</p>{/if}
  <PianoRoll />
  <p class="muted hint">Click to draw (drag to stretch) · drag a note to move it, its right edge to stretch it · Alt-drag:
    velocity · double-click or right-click: delete · Shift: finer than the grid · drag the end of the loop on the ruler</p>
</div>
