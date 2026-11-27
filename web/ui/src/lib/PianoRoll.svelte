<!-- PianoRoll.svelte — the clip's notes on a piano roll: pitches high on top (the key's notes lighter, C named),
     every MIDI pitch in a box that scrolls (it goes to the clip's notes when a clip arrives, to the octave chosen
     in the toolbar when that changes), time over the clip's length, which is also the loop (the brace on the ruler; drag its end), and a velocity lane
     below (drag on a note's bar). Two tools: Draw, where a click on an empty place draws a note and dragging
     stretches it, and Select, where dragging on an empty place selects with a rectangle (Cmd-drag in Draw) and a
     click sets the insertion point. In both, a click selects a note (Shift adds or removes), dragging moves the
     selected notes (time and pitch), their right edge stretches them, Alt-drag moves a copy; double-click or
     right-click deletes. A click on the ruler sets the insertion point, where Paste lands (then moves past what
     was pasted, so pastes follow each other); without one, a paste goes after the selection or at the start.
     The clipboard is kept across sounds and clips. The Edit menu: undo, the clipboard, selections; the Pitch, Time
     and Velocity menus: note operations (noteOps.js) on the selection, or on every note. Keys, whenever the Sequence view is shown and no text field
     has the focus: Delete, Cmd+A, Cmd+C / X / V, Cmd+D (duplicate after the selection), Cmd+L (legato), Cmd+U
     (quantize), Esc; with a selection, the arrows move it (a grid step; up / down a semitone, Shift an octave),
     Shift + left / right shortens / lengthens it by a grid step, Alt + left / right nudges it finer than the grid
     (without a selection the arrows stay the computer keyboard's octave). Everything snaps to the grid (Shift
     while dragging: finer). A drawn or moved note sounds. -->
<script>
  import { untrack } from 'svelte';
  import Seg from './Seg.svelte';
  const TOOLS = [{ id: 'draw', label: 'Draw', title: 'Draw notes' }, { id: 'select', label: 'Select', title: 'Select with a rectangle (or Cmd-drag in Draw)' }];
  import { session } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { history } from './history.svelte.js';
  import { noteName, inScale } from './seqExport.js';
  import * as op from './noteOps.js';
  import Menu from './Menu.svelte';

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
  // every MIDI pitch, high first, in a box that scrolls (wheel, trackpad, scrollbar); the view goes to the notes
  // when a clip arrives, and to the octave chosen in the clip settings when it changes
  const pitches = Array.from({ length: 128 }, (_, i) => 127 - i);
  const top = 127;
  let pending = null; // a pitch to centre once the roll is laid out (it may be hidden when a clip arrives)
  function centre(p) {
    if (!scroller || !scroller.clientHeight) { pending = p; return; }
    pending = null;
    scroller.scrollTop = Math.max(0, (top - p + 0.5) * ROW - scroller.clientHeight / 2);
  }
  const octaveMid = () => 12 * (q.octave + 1) + 6; // the middle of the chosen octave (C to the next C in view)
  function centreNotes() {
    const ps = q.notes.map((n) => n.pitch);
    centre(ps.length ? (Math.min(...ps) + Math.max(...ps)) / 2 : octaveMid());
  }
  let seen = { loads: -1, octave: -1 };
  $effect(() => {
    const loads = q.loads, octave = q.octave;
    untrack(() => {
      if (loads !== seen.loads) centreNotes(); // a new clip: its notes (it also sets the octave)
      else if (octave !== seen.octave) centre(octaveMid());
      seen = { loads, octave };
    });
  });
  // the roll's height: what the page shows below its top, less the velocity lane and the hint, so that only the
  // roll scrolls (not the page and the roll); at least a few octaves
  let boxH = $state(400);
  function fit() {
    const main = scroller?.closest('main');
    if (!main || !scroller.offsetParent) return;
    const m = main.getBoundingClientRect(), r = scroller.getBoundingClientRect();
    boxH = Math.round(Math.max(240, m.bottom - (r.top + main.scrollTop) - VEL_H - 64));
  }
  $effect(() => {
    if (!scroller) return;
    const main = scroller.closest('main');
    const ro = new ResizeObserver(() => { fit(); if (pending !== null && scroller.clientHeight) centre(pending); });
    ro.observe(scroller);
    if (main) ro.observe(main); // the window's height changes
    fit();
    return () => ro.disconnect();
  });
  const black = (p) => [1, 3, 6, 8, 10].includes(p % 12);
  // the notes being played (on-screen or computer keyboard, MIDI input): their key and lane light up, and the roll
  // scrolls to show the newest one when it is out of view
  const sounding = $derived(new Set([...session.played, ...session.held]));
  let lastSounding = new Set();
  $effect(() => {
    const now = sounding, fresh = [...now].filter((p) => !lastSounding.has(p));
    lastSounding = now;
    if (!fresh.length || !scroller) return;
    const y = (top - fresh[fresh.length - 1]) * ROW, h = scroller.clientHeight;
    if (y < scroller.scrollTop || y + ROW > scroller.scrollTop + h) scroller.scrollTo({ top: Math.max(0, y - h / 2), behavior: 'smooth' });
  });
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
  let drag = null; // {mode: 'move' | 'size' | 'box', beat0, pitch0, y0, orig: Map(id -> note), box}
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
      if (e.altKey) { // Alt-drag: a copy of the selection moves, the originals stay
        history.hold();
        sel = new Set(q.addMany(selected.map(({ id: _, ...n }) => n)));
        startDrag('move', e, pos);
        history.release();
        return;
      }
      startDrag(e.clientX > r.right - EDGE ? 'size' : 'move', e, pos);
      return;
    }
    if (tool === 'select' || e.metaKey || e.ctrlKey) { // a rectangle selects; a plain click sets the insertion point
      if (!e.shiftKey) sel = new Set();
      if (tool === 'select') q.cursor = Math.min(snapTo(pos.beat, false), q.length);
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

  // ---- editing commands (the toolbar's buttons and the keys) ----
  function copy() {
    if (!selected.length) return false;
    const first = Math.min(...selected.map((n) => n.start));
    q.clipboard = { notes: selected.map(({ id, ...n }) => ({ ...n, start: n.start - first })),
      span: Math.ceil((Math.max(...selected.map((n) => n.start + n.length)) - first) / q.grid - 1e-9) * q.grid };
    return true;
  }
  function pasteAt(at) {
    const c = q.clipboard;
    if (!c?.notes.length || at >= q.length) return;
    const ids = q.addMany(c.notes.map((n) => ({ ...n, start: at + n.start })).filter((n) => n.start < q.length));
    sel = new Set(ids);
    q.cursor = Math.min(q.length, at + c.span); // the next paste follows this one
  }
  const selEnd = () => Math.ceil(Math.max(...selected.map((n) => n.start + n.length)) / q.grid - 1e-9) * q.grid;
  const all = () => new Set(q.notes.map((n) => n.id));
  const target = () => (sel.size ? sel : all()); // the selection, else every note
  const clipOf = () => ({ length: q.length, grid: q.grid });
  const cmd = {
    cut: () => { if (copy()) { q.deleteMany(sel); sel = new Set(); } },
    copy,
    paste: () => pasteAt(q.cursor >= 0 ? q.cursor : sel.size ? selEnd() : 0),
    duplicate: () => { if (copy()) pasteAt(selEnd()); },
    remove: () => { q.deleteMany(sel); sel = new Set(); },
    legato: () => q.apply(op.legato, target(), clipOf()),
    quantize: () => q.apply(op.quantize, target(), clipOf(), 1),
  };
  const run = (name) => { history.step(); cmd[name](); };
  const doOp = (fn, ...args) => () => { history.step(); q.apply(fn, target(), ...args); };
  const has = $derived(q.notes.length > 0), any = $derived(sel.size > 0);
  const editItems = $derived([
    { label: 'Undo', kbd: '⌘Z', run: history.undo, disabled: !history.canUndo },
    { label: 'Redo', kbd: '⇧⌘Z', run: history.redo, disabled: !history.canRedo },
    '-',
    { label: 'Cut', kbd: '⌘X', run: () => run('cut'), disabled: !any },
    { label: 'Copy', kbd: '⌘C', run: () => run('copy'), disabled: !any },
    { label: 'Paste', kbd: '⌘V', run: () => run('paste'), disabled: !q.clipboard?.notes.length },
    { label: 'Duplicate', kbd: '⌘D', run: () => run('duplicate'), disabled: !any },
    { label: 'Delete', kbd: '⌫', run: () => run('remove'), disabled: !any },
    '-',
    { label: 'Select all', kbd: '⌘A', run: () => (sel = all()), disabled: !has },
    { label: 'Select none', kbd: 'Esc', run: () => (sel = new Set()), disabled: !any },
    { label: 'Invert selection', run: () => (sel = op.invertSelection(q.notes, sel)), disabled: !has },
    { label: 'Select the same pitches', run: () => (sel = op.samePitch(q.notes, sel)), disabled: !any },
  ]);
  const scope = $derived(any ? `${sel.size} selected` : 'every note'); // what the Pitch, Time and Velocity menus act on
  const pitchItems = $derived([
    { heading: scope },
    { label: 'Up a semitone', kbd: '↑', run: doOp(op.transpose, 1), disabled: !has },
    { label: 'Down a semitone', kbd: '↓', run: doOp(op.transpose, -1), disabled: !has },
    { label: 'Up an octave', kbd: '⇧↑', run: doOp(op.transpose, 12), disabled: !has },
    { label: 'Down an octave', kbd: '⇧↓', run: doOp(op.transpose, -12), disabled: !has },
    '-',
    { label: 'Upside down', run: doOp(op.invertPitch), disabled: !has },
  ]);
  const timeItems = $derived([
    { heading: scope },
    { label: 'Quantize starts', kbd: '⌘U', run: () => run('quantize'), disabled: !has },
    { label: 'Quantize starts, 50 %', run: doOp(op.quantize, clipOf(), 0.5), disabled: !has },
    { label: 'Quantize ends', run: doOp(op.quantizeEnds, clipOf()), disabled: !has },
    { label: 'Humanize', run: doOp(op.humanize, Math.random, q.grid / 8, 10), disabled: !has },
    '-',
    { label: 'Legato', kbd: '⌘L', run: () => run('legato'), disabled: !has },
    { label: 'Staccato (half length)', run: doOp(op.staccato, 0.5), disabled: !has },
    { label: 'Split at the grid', run: doOp(op.split, clipOf(), q.newId), disabled: !has },
    { label: 'Glue (same pitch, touching)', run: doOp(op.glue), disabled: !has },
    '-',
    { label: 'Reverse', run: doOp(op.reverse), disabled: !has },
    { label: 'Double speed', run: doOp(op.speed, 0.5, clipOf()), disabled: !has },
    { label: 'Half speed', run: doOp(op.speed, 2, clipOf()), disabled: !has },
  ]);
  const velocityItems = $derived([
    { heading: scope },
    { label: 'Louder', run: doOp(op.scaleVelocity, 1.15), disabled: !has },
    { label: 'Softer', run: doOp(op.scaleVelocity, 0.85), disabled: !has },
    { label: 'All at 100', run: doOp(op.setVelocity, 100), disabled: !has },
    '-',
    { label: 'Crescendo', run: doOp(op.rampVelocity, 50, 120), disabled: !has },
    { label: 'Decrescendo', run: doOp(op.rampVelocity, 120, 50), disabled: !has },
  ]);

  // ---- keys: whenever this view is shown (the roll is laid out) and no field types; arrows need a selection ----
  const typing = (t) => t?.closest?.('.cm-editor') || t?.isContentEditable || t?.tagName === 'TEXTAREA' || t?.tagName === 'SELECT' ||
    (t?.tagName === 'INPUT' && !['range', 'checkbox', 'radio', 'button'].includes(t.type));
  function key(e) {
    if (!area?.offsetParent || typing(e.target) || session.saving || session.shortcutsOpen || session.settingsOpen || session.clipSettingsOpen || session.shareOpen) return;
    const mod = e.metaKey || e.ctrlKey, k = e.key, arrow = k.startsWith('Arrow');
    if (arrow && !sel.size) return; // the computer keyboard's octave
    if (arrow && (e.target?.type === 'range' || e.target?.closest?.('.select'))) return; // a slider or a menu keeps its arrows
    const C = { KeyA: () => (sel = new Set(q.notes.map((n) => n.id))), KeyC: cmd.copy, KeyX: cmd.cut, KeyV: cmd.paste,
      KeyD: cmd.duplicate, KeyL: cmd.legato, KeyU: cmd.quantize };
    let act = null;
    const idle = (e.code === 'KeyC' || e.code === 'KeyX' || e.code === 'KeyD') && !sel.size || e.code === 'KeyV' && !q.clipboard?.notes.length;
    if (mod && C[e.code] && !idle) act = C[e.code]; // with nothing to copy or paste, the page keeps the key
    else if (!mod && (k === 'Delete' || k === 'Backspace') && sel.size) act = cmd.remove;
    else if (!mod && k === 'Escape' && (sel.size || q.cursor >= 0)) act = () => { sel = new Set(); q.cursor = -1; };
    else if (!mod && (k === 'ArrowUp' || k === 'ArrowDown')) {
      const d = (k === 'ArrowUp' ? 1 : -1) * (e.shiftKey ? 12 : 1);
      act = () => {
        q.apply(op.transpose, sel, d);
        sound(Math.min(127, Math.max(0, selected[0].pitch)));
      };
    } else if (!mod && (k === 'ArrowLeft' || k === 'ArrowRight')) {
      const dir = k === 'ArrowRight' ? 1 : -1;
      if (e.shiftKey) act = () => q.edit(sel, (n) => ({ length: Math.max(q.grid, n.length + dir * q.grid) })); // length
      else {
        const d = dir * (e.altKey ? q.grid / 4 : q.grid); // Alt: finer than the grid
        act = () => { if (selected.every((n) => n.start + d >= 0 && n.start + d < q.length)) q.edit(sel, (n) => ({ start: n.start + d })); };
      }
    }
    if (!act) return;
    e.preventDefault();
    e.stopImmediatePropagation(); // the computer keyboard (arrows: its octave) does not see it
    history.step();
    act();
  }

  // ---- the loop's end on the ruler ----
  let sizing = null;
  function rulerDown(e) { // a click on the ruler (not on the loop's handle): the insertion point
    if (e.button !== 0 || e.target.closest('.handle')) return;
    const r = e.currentTarget.getBoundingClientRect();
    q.cursor = Math.min(q.length, Math.max(0, snapTo((e.clientX - r.left) / ppb, e.shiftKey)));
  }
  function loopDown(e) {
    e.stopPropagation();
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

<svelte:window onkeydowncapture={key} />

<div class="toolbar" role="toolbar" aria-label="Piano roll tools">
  <Seg items={TOOLS} value={tool} onpick={(t) => (tool = t)} label="Tool" />
  <span class="vsep" aria-hidden="true"></span>
  <Menu label="Edit" items={editItems} title="Undo, clipboard, selection" />
  <Menu label="Pitch" items={pitchItems} title="Transpose: the selection, or every note" />
  <Menu label="Time" items={timeItems} title="Quantize, lengths, order and speed: the selection, or every note" />
  <Menu label="Velocity" items={velocityItems} title="Loudness of the notes: the selection, or every note" />
  <span class="vsep" aria-hidden="true"></span>
  <button class="ghost" onclick={() => run('cut')} disabled={!sel.size} title="Cut the selection (Cmd+X)">Cut</button>
  <button class="ghost" onclick={() => run('copy')} disabled={!sel.size} title="Copy the selection (Cmd+C)">Copy</button>
  <button class="ghost" onclick={() => run('paste')} disabled={!q.clipboard?.notes.length}
          title="Paste at the insertion point (click the ruler to set it), else after the selection (Cmd+V)">Paste</button>
  <button class="ghost" onclick={() => run('duplicate')} disabled={!sel.size} title="Duplicate after the selection (Cmd+D)">Duplicate</button>
  <span class="spacer"></span>
  <span class="muted">{sel.size ? `${sel.size} selected` : ''}{sel.size && q.cursor >= 0 ? ' · ' : ''}{q.cursor >= 0 ? `insert at ${Math.floor(q.cursor / 4) + 1}.${Math.floor(q.cursor % 4) + 1}` : ''}</span>
</div>

<div class="roll" style="--row:{ROW}px; --gutter:{gutter}px">
  <div class="corner"></div>
  <div class="ruler" class:off={!q.loop} onpointerdown={rulerDown} role="presentation" title="Click: the insertion point (where Paste lands)">
    {#each bars as b}
      <span class="bar" style="left:{b * 4 * ppb}px">{b + 1}</span>
    {/each}
    {#if q.cursor >= 0}<span class="mark" style="left:{q.cursor * ppb}px"></span>{/if}
    <div class="brace" style="width:{q.length * ppb}px" title={q.loop ? `Loop: ${q.length} beats` : `Plays once: ${q.length} beats`}>
      <span class="handle" role="slider" tabindex="0" aria-label="Clip length (beats)" aria-valuenow={q.length}
            aria-valuemin="1" aria-valuemax="64" onpointerdown={loopDown} onpointermove={loopMove} onpointerup={loopUp}
            onkeydown={(e) => { if (e.key === 'ArrowRight') q.length = Math.min(64, q.length + 1); if (e.key === 'ArrowLeft') q.length = Math.max(1, q.length - 1); }}></span>
    </div>
  </div>
  <div class="scroll" bind:this={scroller} style="height:{boxH}px">
    <div class="pkeys">
      {#each pitches as p (p)}
        <button class="key" class:black={black(p)} class:c={p % 12 === 0} class:lit={sounding.has(p)} onpointerdown={() => sound(p)} tabindex="-1">
          {p % 12 === 0 || !black(p) ? noteName(p) : ''}</button>
      {/each}
    </div>
    <div class="area" class:select={tool === 'select'} bind:this={area} bind:clientWidth={width} style="height:{pitches.length * ROW}px"
         onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up}
         ondblclick={dbl} oncontextmenu={context} role="application" aria-label="Piano roll" tabindex="-1">
      {#each pitches as p, i (p)}
        <div class="lane" class:black={black(p)} class:out={!inScale(p, q.root, q.scale)} class:lit={sounding.has(p)} style="top:{i * ROW}px"></div>
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
      {#if q.cursor >= 0}<div class="cursor" style="left:{q.cursor * ppb}px"></div>{/if}
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
  .scroll { grid-column: 1 / 3; display: grid; grid-template-columns: 44px 1fr; overflow-y: auto; scrollbar-gutter: stable; overscroll-behavior: contain; }
  .pkeys { display: flex; flex-direction: column; border-right: 1px solid var(--line); }
  .key { flex: none; box-sizing: border-box; height: var(--row); min-height: 0; line-height: var(--row); font: 9.5px/var(--row) var(--mono); color: var(--muted);
    text-align: right; padding: 0 5px; margin: 0; border: 0; border-radius: 0; background: transparent; }
  .key.black { background: var(--faint); }
  .key.c { color: var(--text); }
  .key:hover { background: var(--accent-soft); }
  .key.lit { background: var(--accent); color: var(--on-accent, #fff); }
  .area { position: relative; cursor: crosshair; touch-action: none; overflow: hidden; outline: none; }
  .area.select { cursor: default; }
  .lane { position: absolute; left: 0; right: 0; height: var(--row); border-bottom: 1px solid color-mix(in srgb, var(--line) 50%, transparent); }
  .lane.black { background: color-mix(in srgb, var(--faint) 55%, transparent); }
  .lane.out { background: color-mix(in srgb, var(--faint) 90%, transparent); }
  .lane.lit { background: color-mix(in srgb, var(--accent) 22%, transparent); }
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
  .cursor { position: absolute; top: 0; bottom: 0; width: 0; border-left: 1px dashed var(--accent); pointer-events: none; }
  .ruler { cursor: text; }
  .ruler .mark { position: absolute; top: 0; width: 0; height: 0; margin-left: -5px; border: 5px solid transparent; border-top-color: var(--accent); pointer-events: none; }
  .vsep { width: 1px; align-self: stretch; margin: 4px 2px; background: var(--line); }
  .playhead { position: absolute; top: 0; bottom: 0; width: 1px; background: var(--text); pointer-events: none; }
  .vlabel { border-top: 1px solid var(--line); border-right: 1px solid var(--line); font: 9.5px var(--mono); color: var(--muted); text-align: right; padding: 4px 5px; }
  .vlane { position: relative; border-top: 1px solid var(--line); cursor: ns-resize; touch-action: none; }
  .vbar { position: absolute; bottom: 0; width: 4px; margin-left: -1px; border-radius: 2px 2px 0 0; background: var(--accent); opacity: .7; }
  .vbar.sel { opacity: 1; background: var(--text); }
</style>
