<!-- Transport.svelte — the bar at the top, as in a DAW: play / stop the clip (Space), its tempo, loop on / off
     (off: it plays once), where it is (bar . beat . sixteenth) and which clip. Always in view, whatever tab is open. -->
<script>
  import { sequence as q } from './sequence.svelte.js';
</script>

<div class="transport" role="toolbar" aria-label="Transport">
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the sequence' : 'Play the sequence'}
          title="Space">{q.playing ? '■' : '▶'}</button>
  <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <button class="loopbtn" class:on={q.loop} onclick={() => (q.loop = !q.loop)} aria-pressed={q.loop}
          title={q.loop ? `Loop on: the clip repeats every ${q.length} beats` : 'Loop off: the clip plays once'}>⟲ Loop</button>
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <span class="seqname">{q.name || 'untitled'}</span>
</div>
