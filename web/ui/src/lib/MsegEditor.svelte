<!-- MsegEditor.svelte — graphical editor of one multi-segment envelope (part MSEG 1/2 or the per-note MSEG):
     drag points (time and level), drag a segment's middle handle to bend its curve, double-click to add or
     remove a point, click a point then set it as sustain / loop start / loop end; loop mode, time unit,
     starting shapes as small drawn buttons; a line shows where the envelope runs while it plays. Writes the flat parameters (prefix + t<i>, l<i>, c<i>, points, sustain, loop_*). -->
<script>
  import { shape, curveThroughMid, readShape, writeShape, times, insertPoint, deletePoint, clone, PRESETS,
           fromPreset, PRESET_ICONS, MIN_TIME, MAX_TIME, MAX_POINTS } from './mseg.js';
  import Seg from './Seg.svelte';
  import Pills from './Pills.svelte';
  import { session } from './session.svelte.js';
  let { prefix } = $props(); // "mseg1_", "mseg2_" or "vmseg_"
  const get = session.getP, set = session.setP;
  // the playhead: where the envelope runs now (the part's, or the newest note's), from the engine
  const which = $derived({ mseg1_: 0, mseg2_: 1, vmseg_: 2 }[prefix] ?? -1);
  const head = $derived(which >= 0 ? session.msegPos[which] : -1);

  const s = $derived(readShape(get, prefix));
  const x = $derived(times(s.pts));
  let W = $state(600);
  const H = 220, PAD = 16;
  let sel = $state(-1);
  let drag = $state(null); // {kind: 'pt' | 'curve', i, span}
  let hover = $state(null);

  const total = $derived(x[x.length - 1]);
  const liveSpan = $derived(Math.max(total * 1.15, 0.5));
  const span = $derived(drag ? drag.span : liveSpan); // frozen while dragging so the view does not run away
  const unit = $derived(s.sync ? 'beat' : 's');
  const X = (t) => PAD + (t / span) * (W - 2 * PAD);
  const Y = (l) => PAD + (1 - l) * (H - 2 * PAD);
  const T = (px) => ((px - PAD) / (W - 2 * PAD)) * span;
  const L = (py) => Math.min(1, Math.max(0, 1 - (py - PAD) / (H - 2 * PAD)));

  const path = $derived.by(() => {
    let d = `M${X(0)},${Y(s.pts[0].l)}`;
    for (let i = 1; i < s.pts.length; i++) {
      const a = s.pts[i - 1].l, b = s.pts[i].l;
      for (let k = 1; k <= 24; k++) {
        const u = k / 24;
        d += ` L${X(x[i - 1] + u * s.pts[i].t)},${Y(a + (b - a) * shape(u, s.pts[i].c))}`;
      }
    }
    return d;
  });
  const fill = $derived(`${path} L${X(total)},${Y(0)} L${X(0)},${Y(0)} Z`);
  const handles = $derived(s.pts.slice(1).map((p, j) => {
    const i = j + 1, a = s.pts[i - 1].l, b = p.l;
    return { i, cx: X(x[i - 1] + 0.5 * p.t), cy: Y(a + (b - a) * shape(0.5, p.c)), flat: Math.abs(b - a) < 0.02 };
  }));
  const grid = $derived.by(() => {
    const steps = [0.05, 0.1, 0.25, 0.5, 1, 2, 4, 8];
    const step = steps.find((v) => span / v <= 12) ?? 8;
    const out = [];
    for (let t = 0; t <= span + 1e-9; t += step) out.push(t);
    return { step, ticks: out };
  });
  const looping = $derived(s.mode > 0 && s.loopStart >= 0 && s.loopEnd > s.loopStart && s.loopEnd < s.pts.length);

  function commit(next) { writeShape(set, prefix, next, s); }
  function svgPoint(e) {
    const r = e.currentTarget.closest('svg').getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top];
  }
  function snapT(t, e) { const g = s.sync ? 0.25 : 0.05; return e.shiftKey ? Math.round(t / g) * g : t; }
  function snapL(l, e) { return e.shiftKey ? Math.round(l * 20) / 20 : l; }

  function down(e, kind, i) {
    e.stopPropagation();
    e.currentTarget.setPointerCapture(e.pointerId);
    sel = kind === 'pt' ? i : sel;
    drag = { kind, i, span: liveSpan };
  }
  function move(e) {
    if (!drag) return;
    if (!e.buttons) { drag = null; return; } // the button was released elsewhere (capture lost)
    const [px, py] = svgPoint(e);
    const next = clone(s), i = drag.i;
    if (drag.kind === 'pt') {
      next.pts[i].l = snapL(L(py), e);
      if (i > 0) next.pts[i].t = Math.min(MAX_TIME, Math.max(MIN_TIME, snapT(T(px), e) - x[i - 1]));
    } else {
      const a = s.pts[i - 1].l, b = s.pts[i].l;
      if (Math.abs(b - a) < 0.02) return;
      next.pts[i].c = curveThroughMid((L(py) - a) / (b - a));
    }
    commit(next);
  }
  function up() { drag = null; }

  function dbl(e) {
    const [px, py] = svgPoint(e);
    const tx = Math.max(MIN_TIME, T(px));
    const next = insertPoint(s, tx, L(py));
    if (next.pts.length === s.pts.length) return; // already 16 points
    commit(next);
    const k = x.findIndex((v) => v >= tx);
    sel = k < 0 ? next.pts.length - 1 : Math.max(1, k); // the new point is selected
  }
  function remove(i) { if (i > 0) { commit(deletePoint(s, i)); sel = -1; } }
  function key(e) {
    if ((e.key === 'Delete' || e.key === 'Backspace') && sel > 0) { e.preventDefault(); remove(sel); }
  }
  function toggle(m) {
    if (sel < 0) return;
    const next = clone(s);
    if (m === 'sustain') next.sustain = s.sustain === sel ? -1 : sel;
    else {
      next[m] = s[m] === sel ? -1 : sel;
      if (next.loopStart >= 0 && next.loopEnd >= 0 && next.loopEnd <= next.loopStart) next[m === 'loopStart' ? 'loopEnd' : 'loopStart'] = -1;
      if (next.mode === 0 && next.loopStart >= 0 && next.loopEnd >= 0) next.mode = 2;
    }
    commit(next);
  }
  const fmtT = (t) => (t < 1 && !s.sync ? `${(t * 1000).toFixed(0)} ms` : `${+t.toFixed(2)} ${unit}`);
  const MODES = ['one shot', 'loop held', 'loop free'].map((label, id) => ({ id, label }));
  const UNITS = [{ id: 0, label: 'seconds' }, { id: 1, label: 'beats' }];
  const SHAPES = Object.keys(PRESETS).map((id) => ({ id, label: id, draw: PRESET_ICONS[id] }));
</script>

<div class="mseg">
  <div class="mseg-bar">
    <Seg items={MODES} value={s.mode} onpick={(mode) => commit({ ...clone(s), mode })} label="Mode" />
    <span class="vsep" aria-hidden="true"></span>
    <Pills items={SHAPES} value={null} onpick={(p) => { commit(fromPreset(p, s)); sel = -1; }} size="icon" label="Start from a shape" />
    <span class="vsep" aria-hidden="true"></span>
    <Seg items={UNITS} value={s.sync} onpick={(sync) => commit({ ...clone(s), sync })} label="Time unit" />
    <span class="muted info">{s.pts.length}/{MAX_POINTS} points · {fmtT(total)}</span>
  </div>

  <div class="mseg-canvas" bind:clientWidth={W}>
    <!-- a drawing surface: focusable for the Delete key, driven by the pointer; no ARIA role fits better -->
    <!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_noninteractive_element_interactions -->
    <svg width={W} height={H} role="application" tabindex="0" aria-label="Envelope editor"
         onkeydown={key} onpointermove={move} onpointerup={up} onpointercancel={up} ondblclick={dbl}>
      {#each grid.ticks as t}
        <line class="grid" x1={X(t)} x2={X(t)} y1={PAD} y2={H - PAD} />
        <text class="tick" x={X(t) + 3} y={H - 3}>{+t.toFixed(2)}{unit === 's' ? 's' : 'b'}</text>
      {/each}
      {#each [0.25, 0.5, 0.75] as l}<line class="grid" x1={PAD} x2={W - PAD} y1={Y(l)} y2={Y(l)} />{/each}
      {#if looping}
        <rect class="loop" x={X(x[s.loopStart])} y={PAD} width={X(x[s.loopEnd]) - X(x[s.loopStart])} height={H - 2 * PAD} />
        <text class="mark-label" x={X(x[s.loopStart]) + 4} y={PAD + 11}>loop</text>
      {/if}
      {#if head >= 0}<line class="playhead" x1={X(head * total)} x2={X(head * total)} y1={PAD} y2={H - PAD} />{/if}
      {#if s.sustain >= 0 && s.sustain < s.pts.length}
        <line class="sus" x1={X(x[s.sustain])} x2={X(x[s.sustain])} y1={PAD} y2={H - PAD} />
        <text class="mark-label" x={X(x[s.sustain]) + 4} y={H - PAD - 4}>sustain</text>
      {/if}
      <path class="area" d={fill} />
      <path class="curve" d={path} />
      {#each handles as h (h.i)}
        {#if !h.flat}
          <circle class="handle" cx={h.cx} cy={h.cy} r="4.5" role="button" tabindex="-1" aria-label="Curve of segment {h.i}" onpointerdown={(e) => down(e, 'curve', h.i)}
                  ondblclick={(e) => { e.stopPropagation(); commit({ ...clone(s), pts: s.pts.map((p, k) => (k === h.i ? { ...p, c: 0 } : p)) }); }}>
            <title>drag to bend · double-click: straight</title>
          </circle>
        {/if}
      {/each}
      {#each s.pts as p, i}
        <circle class="pt" class:sel={sel === i} cx={X(x[i])} cy={Y(p.l)} r={sel === i ? 7 : 6} role="button" tabindex="-1" aria-label="Point {i + 1}"
                onpointerdown={(e) => down(e, 'pt', i)} onpointerenter={() => (hover = i)} onpointerleave={() => (hover = null)}
                ondblclick={(e) => { e.stopPropagation(); remove(i); }}>
          <title>point {i + 1}: {p.l.toFixed(2)} at {fmtT(x[i])} · double-click: remove</title>
        </circle>
      {/each}
    </svg>
  </div>

  <div class="mseg-bar">
    {#if sel >= 0 && sel < s.pts.length}
      <span class="info">Point {sel + 1}: level {s.pts[sel].l.toFixed(2)}{sel ? ` · ${fmtT(s.pts[sel].t)} after point ${sel}` : ''}</span>
      <button class="chip" class:on={s.sustain === sel} onclick={() => toggle('sustain')}>Sustain</button>
      <button class="chip" class:on={s.loopStart === sel} onclick={() => toggle('loopStart')}>Loop start</button>
      <button class="chip" class:on={s.loopEnd === sel} onclick={() => toggle('loopEnd')}>Loop end</button>
      {#if sel > 0}<button class="chip" onclick={() => remove(sel)}>Remove</button>{/if}
    {:else}
      <span class="muted info">Drag points and curve handles (Shift snaps) · double-click to add or remove · click a point to set sustain or loop</span>
    {/if}
  </div>
</div>
