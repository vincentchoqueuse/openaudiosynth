<!-- Transport.svelte — the bar at the top, as in a DAW: play / stop the sequence (Space), its tempo, where it is
     (bar . beat), and the sequence playing. Always in view, whatever tab is open. -->
<script>
  import { sequence as q } from './sequence.svelte.js';
</script>

<div class="transport" role="toolbar" aria-label="Transport">
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the sequence' : 'Play the sequence'}
          title="Space">{q.playing ? '■' : '▶'}</button>
  <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <span class="pos mono" title="bar . beat">{q.position}</span>
  <span class="seqname">{q.name || 'untitled'}</span>
</div>
