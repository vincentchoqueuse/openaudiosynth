<!-- PianoRoll.svelte — the clip's notes on a piano roll: pitches high on top (the key's notes lighter, C named),
     time over the clip's length, which is also the loop (the brace on the ruler; drag its end), and a velocity lane
     below. Two tools: Draw, where a click on an empty place draws a note and dragging stretches it, and Select,
     where dragging on an empty place selects with a rectangle (Cmd-drag in Draw). In both, a click selects a note
     (Shift adds or removes), dragging moves the selected notes (time and pitch), their right edge stretches them,
     Alt-drag sets their velocity; double-click deletes. Keys, once the roll has the focus (the computer keyboard
     keeps its letters): Delete, Cmd+A, Cmd+C / X / V, Cmd+D (duplicate after the selection), arrows (move by a
     grid step, up / down a semitone, Shift an octave), Cmd+U (quantize), Esc.
     Everything snaps to the grid (Shift while dragging: finer). A drawn or moved note sounds. -->
<script>
  import Seg from './Seg.svelte';
  const TOOLS = [{ id: 'draw', label: 'Draw', title: 'Draw notes' }, { id: 'select', label: 'Select', title: 'Select with a rectangle (or Cmd-drag in Draw)' }];
  import { session } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { history } from './history.svelte.js';
  import { noteName, inScale } from './seqExport.js';
  let { settings } = $props(); // the clip's settings, shown at the start of the roll's toolbar

  const ROW = 15, EDGE = 7, VEL_H = 56; // row height, grab width of a note's right edge, velocity lane (px)
  let width = $state(600); // of the time area
  let scroller, gutter = $state(0); // width of the scroll area's scrollbar: the ruler leaves it out too
  $effect(() => { width; gutter = scroller ? scroller.offsetWidth - scroller.clientWidth : 0; });
  const ppb = $derived(width / q.length); // pixels per beat
  // the vertical lines, one per grid step at a whole pixel, strongest kind wins: bar, beat, then step
  const vlines = $derived.by(() => {
    const out = [], steps = Math.round(q.length / q.grid);
    for (let i = 1; i < steps; i++) {
      const b = i * q.grid, kind = Math.abs(b / 4 - Math.round(b / 4)) < 1e-6 ? 'bar' : Math.abs(b - Math.round(b)) < 1e-6 ? 'beat' : 'step';
      out.push({ x: Math.round(b * ppb), kind });
    }
    return out;
  });
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

  let tool = $state('draw'); // 'draw' | 'select'
  let sel = $state(new Set()); // ids of the selected notes
  $effect(() => { // a note that no longer exists (undo, JSON, clear) leaves the selection
    const ids = new Set(q.notes.map((n) => n.id));
    untrackSel(ids);
  });
  function untrackSel(ids) { if ([...sel].some((id) => !ids.has(id))) sel = new Set([...sel].filter((id) => ids.has(id))); }
  const selected = $derived(q.notes.filter((n) => sel.has(n.id)));

  // ---- pointer gestures on the notes ----
  let area;
  let drag = null; // {mode: 'move' | 'size' | 'vel' | 'box', beat0, pitch0, y0, orig: Map(id -> note), box}
  let box = $state(null); // the selection rectangle {x, y, w, h} in px
  const snapTo = (b, fine) => { const g = fine ? q.grid / 4 : q.grid; return Math.round(b / g) * g; };
  function at(e) {
    const r = area.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top, beat: Math.max(0, (e.clientX - r.left) / ppb), pitch: top - Math.floor((e.clientY - r.top) / ROW) };
  }
  function sound(p, v = 100) {
    session.synth.noteOn(p, v);
    setTimeout(() => session.synth.noteOff(p), 180);
  }
  function startDrag(mode, e, pos) {
    drag = { mode, beat0: pos.beat, pitch0: pos.pitch, y0: e.clientY, x0: pos.x, ys: pos.y, orig: new Map(selected.map((n) => [n.id, { ...n }])) };
    if (mode !== 'box') history.hold();
  }
  function down(e) {
    if (e.button !== 0) return;
    e.preventDefault();
    area.focus({ preventScroll: true });
    area.setPointerCapture(e.pointerId);
    const pos = at(e);
    const hit = e.target.closest?.('.note');
    if (hit) {
      const id = +hit.dataset.id;
      if (e.shiftKey) { // add to / remove from the selection, no drag
        const s = new Set(sel);
        s.has(id) ? s.delete(id) : s.add(id);
        sel = s;
        return;
      }
      if (!sel.has(id)) sel = new Set([id]);
      const r = hit.getBoundingClientRect();
      startDrag(e.altKey ? 'vel' : e.clientX > r.right - EDGE ? 'size' : 'move', e, pos);
      return;
    }
    if (tool === 'select' || e.metaKey || e.ctrlKey) { // a rectangle selects
      if (!e.shiftKey) sel = new Set();
      startDrag('box', e, pos);
      return;
    }
    sel = new Set();
    if (pos.pitch < 0 || pos.pitch > 127 || pos.beat >= q.length) return;
    const start = Math.max(0, Math.min(Math.floor(pos.beat / q.grid) * q.grid, q.length - q.grid));
    const id = q.add(pos.pitch, start, q.grid);
    sound(pos.pitch);
    sel = new Set([id]);
    startDrag('size', e, { ...pos, beat: start + q.grid });
  }
  function move(e) {
    if (!drag) return;
    const pos = at(e), fine = e.shiftKey, g = fine ? q.grid / 4 : q.grid;
    if (drag.mode === 'box') {
      const x = Math.min(drag.x0, pos.x), y = Math.min(drag.ys, pos.y);
      box = { x, y, w: Math.abs(pos.x - drag.x0), h: Math.abs(pos.y - drag.ys) };
      const b0 = x / ppb, b1 = (x + box.w) / ppb, p0 = top - Math.floor((y + box.h) / ROW), p1 = top - Math.floor(y / ROW);
      const inBox = q.notes.filter((n) => n.start < b1 && n.start + n.length > b0 && n.pitch >= p0 && n.pitch <= p1).map((n) => n.id);
      sel = new Set(e.shiftKey ? [...sel, ...inBox] : inBox);
      return;
    }
    const o = drag.orig, ids = new Set(o.keys());
    if (drag.mode === 'size') {
      const d = snapTo(pos.beat - drag.beat0, fine);
      q.edit(ids, (n) => ({ length: Math.max(g, o.get(n.id).length + d) }));
    } else if (drag.mode === 'move') {
      const first = Math.min(...[...o.values()].map((n) => n.start));
      const dBeat = Math.max(-first, snapTo(pos.beat - drag.beat0, fine)), dPitch = pos.pitch - drag.pitch0;
      const lead = o.values().next().value, before = q.notes.find((n) => n.id === lead.id)?.pitch;
      q.edit(ids, (n) => {
        const m = o.get(n.id);
        return { start: Math.min(m.start + dBeat, q.length - g), pitch: Math.min(127, Math.max(0, m.pitch + dPitch)) };
      });
      const after = Math.min(127, Math.max(0, lead.pitch + dPitch));
      if (after !== before) sound(after);
    } else {
      const dv = Math.round((drag.y0 - e.clientY) / 2);
      q.edit(ids, (n) => ({ velocity: Math.min(127, Math.max(1, o.get(n.id).velocity + dv)) }));
    }
  }
  function up() {
    if (drag && drag.mode !== 'box') history.release();
    drag = null;
    box = null;
  }
  function dbl(e) {
    const hit = e.target.closest?.('.note');
    if (!hit) return;
    history.step();
    const id = +hit.dataset.id;
    q.deleteMany(sel.has(id) ? sel : new Set([id]));
    sel = new Set();
  }
  function context(e) { e.preventDefault(); dbl(e); } // right-click deletes too

  // ---- the velocity lane: drag up / down on a note's bar (all the selected ones if it is selected) ----
  let velDrag = null;
  function velAt(e, lane) {
    const r = lane.getBoundingClientRect();
    return { beat: (e.clientX - r.left) / ppb, v: Math.round(Math.min(127, Math.max(1, 127 * (1 - (e.clientY - r.top) / VEL_H)))) };
  }
  function velDown(e) {
    if (e.button !== 0) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    velDrag = e.currentTarget;
    history.hold();
    velMove(e);
  }
  function velMove(e) {
    if (!velDrag) return;
    const { beat, v } = velAt(e, velDrag), tol = 6 / ppb;
    const hit = q.notes.filter((n) => Math.abs(n.start - beat) <= tol);
    if (!hit.length) return;
    const ids = new Set(hit.some((n) => sel.has(n.id)) ? [...sel, ...hit.map((n) => n.id)] : hit.map((n) => n.id));
    q.edit(ids, () => ({ velocity: v }));
  }
  function velUp() { if (velDrag) history.release(); velDrag = null; }

  // ---- keys (while this tab is shown and the focus is not in a field) ----
  let clip = []; // copied notes, starts relative to the first one: {pitch, start, length, velocity}
  let clipSpan = 0;
  function copy() {
    if (!selected.length) return false;
    const first = Math.min(...selected.map((n) => n.start));
    clip = selected.map(({ id, ...n }) => ({ ...n, start: n.start - first }));
    clipSpan = Math.max(...selected.map((n) => n.start + n.length)) - first;
    return true;
  }
  function pasteAt(at) {
    if (!clip.length) return;
    const ids = q.addMany(clip.map((n) => ({ ...n, start: at + n.start })).filter((n) => n.start < q.length));
    sel = new Set(ids);
  }
  const selEnd = () => Math.ceil(Math.max(...selected.map((n) => n.start + n.length)) / q.grid - 1e-9) * q.grid;
  function quantize() {
    history.step();
    const ids = sel.size ? sel : new Set(q.notes.map((n) => n.id));
    q.edit(ids, (n) => ({ start: Math.min(Math.round(n.start / q.grid) * q.grid, Math.max(0, q.length - q.grid)) }));
  }
  function key(e) {
    const t = e.target;
    if (!t?.closest?.('.roll')) return; // the roll has the focus (a click in it gives it)
    const mod = e.metaKey || e.ctrlKey, k = e.key;
    history.step();
    let done = true;
    if (k === 'Delete' || k === 'Backspace') { if (sel.size) { q.deleteMany(sel); sel = new Set(); } }
    else if (mod && e.code === 'KeyA') sel = new Set(q.notes.map((n) => n.id));
    else if (mod && e.code === 'KeyC') copy();
    else if (mod && e.code === 'KeyX') { if (copy()) { q.deleteMany(sel); sel = new Set(); } }
    else if (mod && e.code === 'KeyV') pasteAt(sel.size ? selEnd() : 0);
    else if (mod && e.code === 'KeyD') { if (copy()) pasteAt(selEnd()); }
    else if (mod && e.code === 'KeyU') quantize();
    else if (k === 'Escape') sel = new Set();
    else if (sel.size && (k === 'ArrowUp' || k === 'ArrowDown')) {
      const d = (k === 'ArrowUp' ? 1 : -1) * (e.shiftKey ? 12 : 1);
      q.edit(sel, (n) => ({ pitch: Math.min(127, Math.max(0, n.pitch + d)) }));
      sound(Math.min(127, Math.max(0, selected[0].pitch + d)));
    } else if (sel.size && (k === 'ArrowLeft' || k === 'ArrowRight')) {
      const d = k === 'ArrowRight' ? q.grid : -q.grid;
      if (selected.every((n) => n.start + d >= 0 && n.start + d < q.length)) q.edit(sel, (n) => ({ start: n.start + d }));
    } else done = false;
    if (done) e.preventDefault();
  }

  // ---- the loop's end on the ruler ----
  let sizing = null;
  function loopDown(e) {
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    sizing = { x0: e.clientX, len0: q.length, ppb0: ppb };
    history.hold();
  }
  function loopMove(e) {
    if (!sizing) return;
    const beats = Math.round(sizing.len0 + (e.clientX - sizing.x0) / sizing.ppb0);
    q.length = Math.min(64, Math.max(1, beats));
  }
  function loopUp() { if (sizing) history.release(); sizing = null; }
</script>

<svelte:window onkeydown={key} />

<div class="toolbar" role="toolbar" aria-label="Piano roll tools">
  {#if settings}{@render settings()}{/if}
  <span class="spacer"></span>
  {#if sel.size}<span class="muted">{sel.size} selected</span>{/if}
  <Seg items={TOOLS} value={tool} onpick={(t) => (tool = t)} label="Tool" />
  <button class="ghost" onclick={() => { history.step(); if (copy()) pasteAt(selEnd()); }} disabled={!sel.size} title="Duplicate after the selection (Cmd+D)">Duplicate</button>
  <button class="ghost" onclick={quantize} disabled={!q.notes.length} title="Starts to the grid, the selection or every note (Cmd+U)">Quantize</button>
  <button class="ghost" onclick={() => { history.step(); q.deleteMany(sel); sel = new Set(); }} disabled={!sel.size} title="Delete">Delete</button>
</div>

<div class="roll" style="--row:{ROW}px; --gutter:{gutter}px">
  <div class="corner"></div>
  <div class="ruler" class:off={!q.loop}>
    {#each bars as b}
      <span class="bar" style="left:{b * 4 * ppb}px">{b + 1}</span>
    {/each}
    <div class="brace" style="width:{q.length * ppb}px" title={q.loop ? `Loop: ${q.length} beats` : `Plays once: ${q.length} beats`}>
      <span class="handle" role="slider" tabindex="0" aria-label="Clip length (beats)" aria-valuenow={q.length}
            aria-valuemin="1" aria-valuemax="64" onpointerdown={loopDown} onpointermove={loopMove} onpointerup={loopUp}
            onkeydown={(e) => { if (e.key === 'ArrowRight') q.length = Math.min(64, q.length + 1); if (e.key === 'ArrowLeft') q.length = Math.max(1, q.length - 1); }}></span>
    </div>
  </div>
  <div class="scroll" bind:this={scroller}>
    <div class="pkeys">
      {#each pitches as p (p)}
        <button class="key" class:black={black(p)} class:c={p % 12 === 0} onpointerdown={() => sound(p)} tabindex="-1">
          {p % 12 === 0 || !black(p) ? noteName(p) : ''}</button>
      {/each}
    </div>
    <div class="area" class:select={tool === 'select'} bind:this={area} bind:clientWidth={width} style="height:{pitches.length * ROW}px"
         onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up}
         ondblclick={dbl} oncontextmenu={context} role="application" aria-label="Piano roll" tabindex="-1">
      {#each pitches as p, i (p)}
        <div class="lane" class:black={black(p)} class:out={!inScale(p, q.root, q.scale)} style="top:{i * ROW}px"></div>
      {/each}
      <div class="lines">{#each vlines as l}<span class="vl {l.kind}" style="left:{l.x}px"></span>{/each}</div>
      {#each q.notes as n (n.id)}
        {#if n.pitch <= top && n.pitch >= pitches[pitches.length - 1]}
          <div class="note" class:sel={sel.has(n.id)} data-id={n.id} title="{noteName(n.pitch)} · {n.length} beat(s) · velocity {n.velocity}"
               style="left:{n.start * ppb}px; top:{(top - n.pitch) * ROW}px; width:{Math.max(3, Math.min(n.length, q.length - n.start) * ppb - 1)}px; --v:{0.35 + 0.65 * n.velocity / 127}">
            <span class="edge"></span>
          </div>
        {/if}
      {/each}
      {#if box}<div class="box" style="left:{box.x}px; top:{box.y}px; width:{box.w}px; height:{box.h}px"></div>{/if}
      {#if q.beat >= 0}<div class="playhead" style="left:{(q.beat % q.length) * ppb}px"></div>{/if}
    </div>
  </div>
  <div class="vlabel">vel</div>
  <div class="vlane" style="height:{VEL_H}px; margin-right:{gutter}px" onpointerdown={velDown} onpointermove={velMove} onpointerup={velUp}
       onpointercancel={velUp} role="application" aria-label="Velocity lane">
    {#each q.notes as n (n.id)}
      <div class="vbar" class:sel={sel.has(n.id)} style="left:{n.start * ppb}px; height:{(n.velocity / 127) * 100}%" title="velocity {n.velocity}"></div>
    {/each}
  </div>
</div>

<style>
  .roll { display: grid; grid-template-columns: 44px 1fr; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; background: var(--surface); user-select: none; }
  .corner { border-bottom: 1px solid var(--line); border-right: 1px solid var(--line); }
  .ruler { position: relative; height: 24px; border-bottom: 1px solid var(--line); margin-right: var(--gutter, 0px); }
  .ruler .bar { position: absolute; top: 3px; font: 10.5px var(--mono); color: var(--muted); padding-left: 4px; border-left: 1px solid var(--line); height: 18px; }
  .brace { position: absolute; left: 0; bottom: 0; height: 6px; background: var(--accent); border-radius: 0 3px 3px 0; }
  .ruler.off .brace { background: var(--muted); opacity: .5; }
  .handle { position: absolute; right: -5px; bottom: -3px; width: 10px; height: 14px; border-radius: 3px; background: var(--text); cursor: ew-resize; touch-action: none; }
  .scroll { grid-column: 1 / 3; display: grid; grid-template-columns: 44px 1fr; max-height: 420px; overflow-y: auto; scrollbar-gutter: stable; }
  .pkeys { display: flex; flex-direction: column; border-right: 1px solid var(--line); }
  .key { flex: none; box-sizing: border-box; height: var(--row); min-height: 0; line-height: var(--row); font: 9.5px/var(--row) var(--mono); color: var(--muted);
    text-align: right; padding: 0 5px; margin: 0; border: 0; border-radius: 0; background: transparent; }
  .key.black { background: var(--faint); }
  .key.c { color: var(--text); }
  .key:hover { background: var(--accent-soft); }
  .area { position: relative; cursor: crosshair; touch-action: none; overflow: hidden; outline: none; }
  .area.select { cursor: default; }
  .lane { position: absolute; left: 0; right: 0; height: var(--row); border-bottom: 1px solid color-mix(in srgb, var(--line) 50%, transparent); }
  .lane.black { background: color-mix(in srgb, var(--faint) 55%, transparent); }
  .lane.out { background: color-mix(in srgb, var(--faint) 90%, transparent); }
  .lines { position: absolute; inset: 0; pointer-events: none; }
  .vl { position: absolute; top: 0; bottom: 0; width: 1px; background: color-mix(in srgb, var(--line) 30%, transparent); }
  .vl.beat { background: color-mix(in srgb, var(--line) 60%, transparent); }
  .vl.bar { background: var(--line); }
  .note { position: absolute; height: calc(var(--row) - 2px); margin-top: 1px; border-radius: 3px; cursor: grab;
    background: color-mix(in srgb, var(--accent) calc(var(--v) * 100%), var(--surface)); box-shadow: inset 0 0 0 1px var(--accent); }
  .note.sel { box-shadow: inset 0 0 0 1.5px var(--text); }
  .note .edge { position: absolute; right: 0; top: 0; bottom: 0; width: 7px; cursor: ew-resize; }
  .note:hover { filter: brightness(1.15); }
  .box { position: absolute; border: 1px solid var(--accent); background: color-mix(in srgb, var(--accent) 12%, transparent); pointer-events: none; }
  .playhead { position: absolute; top: 0; bottom: 0; width: 1px; background: var(--text); pointer-events: none; }
  .vlabel { border-top: 1px solid var(--line); border-right: 1px solid var(--line); font: 9.5px var(--mono); color: var(--muted); text-align: right; padding: 4px 5px; }
  .vlane { position: relative; border-top: 1px solid var(--line); cursor: ns-resize; touch-action: none; }
  .vbar { position: absolute; bottom: 0; width: 4px; margin-left: -1px; border-radius: 2px 2px 0 0; background: var(--accent); opacity: .7; }
  .vbar.sel { opacity: 1; background: var(--text); }
</style>
