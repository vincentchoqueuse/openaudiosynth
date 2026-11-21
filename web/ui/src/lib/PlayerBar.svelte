<!-- PlayerBar.svelte — the second bar, right under the top bar and shared by both tabs: the clip's transport (play
     / stop with Space, tempo, loop on / off, position) and which clip plays. -->
<script>
  import { sequence as q } from './sequence.svelte.js';
</script>

<div class="playerbar" role="toolbar" aria-label="Player">
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Space">{q.playing ? '■' : '▶'}</button>
  <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <button class="loopbtn" class:on={q.loop} onclick={() => (q.loop = !q.loop)} aria-pressed={q.loop}
          title={q.loop ? `Loop on: the clip repeats every ${q.length} beats` : 'Loop off: the clip plays once'}>⟲ Loop</button>
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <span class="clipname muted">{q.name || 'untitled'} · {q.notes.length} notes · {q.length} beats</span>
</div>
