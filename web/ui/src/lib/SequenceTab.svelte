<!-- SequenceTab.svelte — the Sequence tab: which sequence (saved in this browser; New, Save, Delete), its settings
     (step, steps, key, octave, range, gate, swing), the grid, and Random, Clear, Export song. Play, tempo and
     position are in the transport; its JSON is in the JSON tab. -->
<script>
  import StepGrid from './StepGrid.svelte';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, STEP_LENGTHS, STEP_COUNTS } from './seqExport.js';
  const OCTAVES = [1, 2, 3, 4, 5], SPANS = [1, 2, 3];
</script>

<div class="seq">
  <div class="seq-bar">
    <label>Sequence
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
    <select bind:value={q.loops} aria-label="Times the sequence plays in the exported song">{#each LOOPS as n}<option value={n}>× {n}</option>{/each}</select>
    <button class="ghost" onclick={q.exportSong}>Export song</button>
  </div>
  <div class="seq-bar">
    <label>Step <select bind:value={q.stepLen}>{#each STEP_LENGTHS as [l, v]}<option value={v}>{l}</option>{/each}</select></label>
    <label>Steps <select bind:value={q.steps}>{#each STEP_COUNTS as n}<option value={n}>{n}</option>{/each}</select></label>
    <label>Key <select bind:value={q.root}>{#each NAMES as n, i}<option value={i}>{n}</option>{/each}</select>
      <select bind:value={q.scale}>{#each Object.keys(SCALES) as s}<option value={s}>{s}</option>{/each}</select></label>
    <label>Octave <select bind:value={q.octave}>{#each OCTAVES as o}<option value={o}>{o}</option>{/each}</select></label>
    <label>Range <select bind:value={q.span}>{#each SPANS as o}<option value={o}>{o} oct</option>{/each}</select></label>
    <label>Gate <input type="range" min="0.05" max="1" step="0.01" bind:value={q.gate} style="--p:{q.gate * 100}%" /></label>
    <label>Swing <input type="range" min="0" max="0.5" step="0.01" bind:value={q.swing} style="--p:{q.swing * 200}%" /></label>
  </div>
  {#if q.message}<p class="message">{q.message}</p>{/if}
  <StepGrid />
  <p class="muted hint">Click: note → accent → soft → off · Shift-click: hold · right-click: off · drag to paint · Space: play / stop</p>
</div>
