<!-- Transport.svelte — the bar at the top, as in a DAW: play / stop the clip (Space), its tempo, loop on / off
     (off: it plays once), where it is (bar . beat . sixteenth), which clip, undo / redo, the output level and the listening volume. Always in
     view, whatever tab is open. -->
<script>
  import { sequence as q } from './sequence.svelte.js';
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
</script>

<div class="transport" role="toolbar" aria-label="Transport">
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the sequence' : 'Play the sequence'}
          title="Space">{q.playing ? '■' : '▶'}</button>
  <label class="bpm"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <button class="loopbtn" class:on={q.loop} onclick={() => (q.loop = !q.loop)} aria-pressed={q.loop}
          title={q.loop ? `Loop on: the clip repeats every ${q.length} beats` : 'Loop off: the clip plays once'}>⟲ Loop</button>
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <span class="seqname">{q.name || 'untitled'}</span>
  <span class="spacer"></span>
  <span class="hist">
    <button class="ghost" onclick={h.undo} disabled={!h.canUndo} title="Undo (⌘Z)" aria-label="Undo">↶</button>
    <button class="ghost" onclick={h.redo} disabled={!h.canRedo} title="Redo (⇧⌘Z)" aria-label="Redo">↷</button>
  </span>
  <div class="vu" class:hot={s.meter.hold > -1} title="Output peak (dBFS), before the volume">
    <div class="track"><div class="fill" style="width:{s.meter.level * 100}%"></div></div>
    <span class="mono">{isFinite(s.meter.hold) && s.meter.hold > -90 ? s.meter.hold.toFixed(0) : '−∞'}</span>
  </div>
  <label class="volume" title="Listening volume (double-click: 0 dB)">
    <span aria-hidden="true">Vol</span>
    <input type="range" min="-60" max="0" step="0.5" value={s.volume} oninput={(e) => s.setVolume(+e.target.value)}
           ondblclick={() => s.setVolume(0)} style="--p:{((s.volume + 60) / 60) * 100}%" aria-label="Volume" />
    <span class="mono db">{s.volume <= -60 ? 'off' : `${s.volume.toFixed(1)} dB`}</span>
  </label>
</div>
