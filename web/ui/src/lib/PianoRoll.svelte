<!-- PianoRoll.svelte — the clip's notes on a piano roll: pitches high on top (the key's notes lighter, C named),
     time from left to right over the clip's length, which is also the loop (the brace on the ruler; drag its end).
     Click on an empty place draws a note one grid step long, keep dragging to stretch it; drag a note to move it
     (time and pitch), its right edge to stretch or shorten it, Alt-drag up / down for its velocity (the note's
     brightness); double-click or right-click deletes it. Everything snaps to the grid (Shift: finer). A drawn note
     sounds. The playhead runs while the clip plays. -->
<script>
  import { session } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { noteName, inScale } from './seqExport.js';

  const ROW = 15, EDGE = 7; // row height, grab width of a note's right edge (px)
  let width = $state(600); // of the time area
  const ppb = $derived(width / q.length); // pixels per beat
  // pitches shown: two octaves from the key's octave, widened to every note, high first
  const pitches = $derived.by(() => {
    const base = 12 * (q.octave + 1) + q.root;
    let lo = base, hi = base + 24;
    for (const n of q.notes) { lo = Math.min(lo, n.pitch); hi = Math.max(hi, n.pitch); }
    lo = Math.max(0, lo - 1); hi = Math.min(127, hi + 1);
    return Array.from({ length: hi - lo + 1 }, (_, i) => hi - i);
  });
  const top = $derived(pitches[0]);
  const black = (p) => [1, 3, 6, 8, 10].includes(p % 12);
  const bars = $derived(Array.from({ length: Math.ceil(q.length / 4) }, (_, i) => i));

  // ---- pointer gestures ----
  let area; // the time area (notes and grid)
  let drag = null; // {id, mode: 'move' | 'size' | 'vel', beat0, pitch0, start0, length0, vel0, y0}
  const snapTo = (b, fine) => { const g = fine ? q.grid / 4 : q.grid; return Math.round(b / g) * g; };
  function at(e) {
    const r = area.getBoundingClientRect();
    return { beat: Math.max(0, (e.clientX - r.left) / ppb), pitch: top - Math.floor((e.clientY - r.top) / ROW) };
  }
  function sound(p, v = 100) {
    session.synth.noteOn(p, v);
    setTimeout(() => session.synth.noteOff(p), 180);
  }
  function down(e) {
    if (e.button !== 0) return;
    e.preventDefault();
    area.setPointerCapture(e.pointerId);
    const { beat, pitch } = at(e);
    const hit = e.target.closest?.('.note');
    if (hit) {
      const n = q.notes.find((x) => x.id === +hit.dataset.id);
      const r = hit.getBoundingClientRect();
      const mode = e.altKey ? 'vel' : e.clientX > r.right - EDGE ? 'size' : 'move';
      drag = { id: n.id, mode, beat0: beat, pitch0: pitch, note0: n.pitch, start0: n.start, length0: n.length, vel0: n.velocity, y0: e.clientY };
      return;
    }
    if (pitch < 0 || pitch > 127 || beat >= q.length) return;
    const start = Math.min(Math.floor(beat / q.grid) * q.grid, q.length - q.grid);
    const id = q.add(pitch, start, q.grid);
    sound(pitch);
    drag = { id, mode: 'size', beat0: start + q.grid, pitch0: pitch, note0: pitch, start0: start, length0: q.grid, vel0: 100, y0: e.clientY };
  }
  function move(e) {
    if (!drag) return;
    const { beat, pitch } = at(e), fine = e.shiftKey, g = fine ? q.grid / 4 : q.grid;
    if (drag.mode === 'size') {
      const end = Math.max(drag.start0 + g, snapTo(drag.start0 + drag.length0 + beat - drag.beat0, fine));
      q.update(drag.id, { length: end - drag.start0 });
    } else if (drag.mode === 'move') {
      const start = Math.min(Math.max(0, snapTo(drag.start0 + beat - drag.beat0, fine)), q.length - g);
      const p = Math.min(127, Math.max(0, drag.note0 + pitch - drag.pitch0));
      const before = q.notes.find((x) => x.id === drag.id)?.pitch;
      q.update(drag.id, { start, pitch: p });
      if (p !== before) sound(p);
    } else {
      q.update(drag.id, { velocity: Math.min(127, Math.max(1, Math.round(drag.vel0 + (drag.y0 - e.clientY) / 2))) });
    }
  }
  function up() { drag = null; }
  function remove(e) {
    const hit = e.target.closest?.('.note');
    if (!hit) return;
    e.preventDefault();
    q.delete(+hit.dataset.id);
  }

  // ---- the loop's end on the ruler ----
  let sizing = null;
  function loopDown(e) {
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    sizing = { x0: e.clientX, len0: q.length, ppb0: ppb };
  }
  function loopMove(e) {
    if (!sizing) return;
    const beats = Math.round(sizing.len0 + (e.clientX - sizing.x0) / sizing.ppb0);
    q.length = Math.min(64, Math.max(1, beats));
  }
</script>

<div class="roll" style="--ppb:{ppb}px; --row:{ROW}px; --grid:{q.grid * ppb}px">
  <div class="corner"></div>
  <div class="ruler" class:off={!q.loop}>
    {#each bars as b}
      <span class="bar" style="left:{b * 4 * ppb}px">{b + 1}</span>
    {/each}
    <div class="brace" style="width:{q.length * ppb}px" title={q.loop ? `Loop: ${q.length} beats` : `Plays once: ${q.length} beats`}>
      <span class="handle" role="slider" tabindex="0" aria-label="Clip length (beats)" aria-valuenow={q.length}
            aria-valuemin="1" aria-valuemax="64" onpointerdown={loopDown} onpointermove={loopMove}
            onpointerup={() => (sizing = null)}
            onkeydown={(e) => { if (e.key === 'ArrowRight') q.length = Math.min(64, q.length + 1); if (e.key === 'ArrowLeft') q.length = Math.max(1, q.length - 1); }}></span>
    </div>
  </div>
  <div class="scroll">
    <div class="pkeys">
      {#each pitches as p (p)}
        <button class="key" class:black={black(p)} class:c={p % 12 === 0} onpointerdown={() => sound(p)} tabindex="-1">
          {p % 12 === 0 || !black(p) ? noteName(p) : ''}</button>
      {/each}
    </div>
    <div class="area" bind:this={area} bind:clientWidth={width} style="height:{pitches.length * ROW}px"
         onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up}
         ondblclick={remove} oncontextmenu={remove} role="application" aria-label="Piano roll">
      {#each pitches as p, i (p)}
        <div class="lane" class:black={black(p)} class:out={!inScale(p, q.root, q.scale)} style="top:{i * ROW}px"></div>
      {/each}
      <div class="lines"></div>
      {#each q.notes as n (n.id)}
        {#if n.pitch <= top && n.pitch >= pitches[pitches.length - 1]}
          <div class="note" data-id={n.id} title="{noteName(n.pitch)} · {n.length} beat(s) · velocity {n.velocity}"
               style="left:{n.start * ppb}px; top:{(top - n.pitch) * ROW}px; width:{Math.max(3, Math.min(n.length, q.length - n.start) * ppb - 1)}px; --v:{0.35 + 0.65 * n.velocity / 127}">
            <span class="edge"></span>
          </div>
        {/if}
      {/each}
      {#if q.beat >= 0}<div class="playhead" style="left:{(q.beat % q.length) * ppb}px"></div>{/if}
    </div>
  </div>
</div>

<style>
  .roll { display: grid; grid-template-columns: 44px 1fr; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; background: var(--surface); user-select: none; }
  .corner { border-bottom: 1px solid var(--line); border-right: 1px solid var(--line); }
  .ruler { position: relative; height: 24px; border-bottom: 1px solid var(--line); }
  .ruler .bar { position: absolute; top: 3px; font: 10.5px var(--mono); color: var(--muted); padding-left: 4px; border-left: 1px solid var(--line); height: 18px; }
  .brace { position: absolute; left: 0; bottom: 0; height: 6px; background: var(--accent); border-radius: 0 3px 3px 0; }
  .ruler.off .brace { background: var(--muted); opacity: .5; }
  .handle { position: absolute; right: -5px; bottom: -3px; width: 10px; height: 14px; border-radius: 3px; background: var(--text); cursor: ew-resize; touch-action: none; }
  .scroll { grid-column: 1 / 3; display: grid; grid-template-columns: 44px 1fr; max-height: 420px; overflow-y: auto; }
  .pkeys { display: flex; flex-direction: column; border-right: 1px solid var(--line); }
  .key { flex: none; box-sizing: border-box; height: var(--row); min-height: 0; line-height: var(--row); font: 9.5px/var(--row) var(--mono); color: var(--muted);
    text-align: right; padding: 0 5px; margin: 0; border: 0; border-radius: 0; background: transparent; }
  .key.black { background: var(--faint); }
  .key.c { color: var(--text); }
  .key:hover { background: var(--accent-soft); }
  .area { position: relative; cursor: crosshair; touch-action: none; overflow: hidden; }
  .lane { position: absolute; left: 0; right: 0; height: var(--row); border-bottom: 1px solid color-mix(in srgb, var(--line) 50%, transparent); }
  .lane.black { background: color-mix(in srgb, var(--faint) 55%, transparent); }
  .lane.out { background: color-mix(in srgb, var(--faint) 90%, transparent); }
  .lines { position: absolute; inset: 0; pointer-events: none;
    background-image: linear-gradient(90deg, var(--line) 1px, transparent 1px), linear-gradient(90deg, color-mix(in srgb, var(--line) 60%, transparent) 1px, transparent 1px),
      linear-gradient(90deg, color-mix(in srgb, var(--line) 30%, transparent) 1px, transparent 1px);
    background-size: calc(var(--ppb) * 4) 100%, var(--ppb) 100%, var(--grid) 100%; }
  .note { position: absolute; height: calc(var(--row) - 2px); margin-top: 1px; border-radius: 3px; cursor: grab;
    background: color-mix(in srgb, var(--accent) calc(var(--v) * 100%), var(--surface)); box-shadow: inset 0 0 0 1px var(--accent); }
  .note .edge { position: absolute; right: 0; top: 0; bottom: 0; width: 7px; cursor: ew-resize; }
  .note:hover { filter: brightness(1.15); }
  .playhead { position: absolute; top: 0; bottom: 0; width: 1px; background: var(--text); pointer-events: none; }
</style>
