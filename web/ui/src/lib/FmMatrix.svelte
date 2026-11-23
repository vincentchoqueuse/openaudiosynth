<!-- FmMatrix.svelte — the modulation matrix of an FM engine (a stage of kind "matrix", parameters "opS_to_opD",
     "opK_feedback", "opK_out"), three views of the same cells. The algorithms: the classic routings as small
     drawings; a click wires the matrix that way (links it already has keep their amount, the others start at 0.4;
     feedback is left alone); the one the matrix matches is lit. The diagram: the routing as it is, carriers at the
     bottom, each modulator above what it modulates, a line as thick as its amount, links going up curved, feedback as
     a loop. The grid: one row per source operator, one column per destination and the output; drag a cell up or down,
     arrows step it, double-click switches it off or on. -->
<script>
  import { session as s } from './session.svelte.js';
  import { history } from './history.svelte.js';
  import { format } from './synth.js';
  let { params } = $props(); // the stage's parameter descriptors

  const ops = $derived.by(() => {
    let n = 0;
    for (const d of params) { const m = /^op(\d+)_out$/.exec(d.name); if (m) n = Math.max(n, +m[1]); }
    return Array.from({ length: n }, (_, i) => i + 1);
  });
  const cellName = (src, dst) => (src === dst ? `op${src}_feedback` : `op${src}_to_op${dst}`);
  const get = (name) => s.getP(name) ?? 0;

  // the classic routings of 4 operators: [modulator, modulated] links, and the carriers
  const ALGOS = [
    { links: [[4, 3], [3, 2], [2, 1]], out: [1], name: 'stack' },
    { links: [[4, 2], [3, 2], [2, 1]], out: [1], name: 'two into one, into the carrier' },
    { links: [[3, 2], [2, 1], [4, 1]], out: [1], name: 'a stack of three and one more modulator' },
    { links: [[4, 3], [3, 1], [2, 1]], out: [1], name: 'a pair and one more modulator' },
    { links: [[4, 1], [3, 1], [2, 1]], out: [1], name: 'three modulators, one carrier' },
    { links: [[4, 3], [4, 2], [3, 1], [2, 1]], out: [1], name: 'diamond' },
    { links: [[4, 3], [2, 1]], out: [1, 3], name: 'two pairs' },
    { links: [[4, 3], [3, 2]], out: [1, 2], name: 'a stack of three and a lone carrier' },
    { links: [[4, 3], [4, 2], [4, 1]], out: [1, 2, 3], name: 'one modulator, three carriers' },
    { links: [[4, 3]], out: [1, 2, 3], name: 'a pair and two carriers' },
    { links: [], out: [1, 2, 3, 4], name: 'additive: four carriers' },
  ];

  // the matrix as it is: links (amount > 0) between different operators, feedback, outputs
  const cur = $derived.by(() => {
    const links = [], fb = {}, out = {};
    for (const a of ops) {
      out[a] = get(`op${a}_out`);
      fb[a] = get(`op${a}_feedback`);
      for (const b of ops) if (a !== b && get(cellName(a, b)) > 0) links.push([a, b, get(cellName(a, b))]);
    }
    return { links, fb, out };
  });
  const key = (links, out) => links.map(([a, b]) => `${a}>${b}`).sort().join(',') + '|' + [...out].sort().join(',');
  const curKey = $derived(key(cur.links, ops.filter((k) => cur.out[k] > 0)));
  const algoKey = (al) => key(al.links, al.out);

  function apply(al) {
    history.step();
    const isLink = (a, b) => al.links.some(([x, y]) => x === a && y === b);
    for (const a of ops) {
      for (const b of ops) {
        if (a === b) continue;
        const n = cellName(a, b), v = get(n);
        s.setP(n, isLink(a, b) ? (v > 0 ? v : 0.4) : 0);
      }
      const o = get(`op${a}_out`);
      s.setP(`op${a}_out`, al.out.includes(a) ? (o > 0 ? o : 0.8) : 0);
    }
    history.step();
  }

  // layout: every operator one row above the highest operator below it (a lower number) that it modulates, so
  // carriers that modulate nothing sit on the bottom row; links the other way (to a higher number, computed a sample
  // late) are drawn curved and do not move anything; operators neither heard nor modulating one that is are dimmed
  function layout(links, out, w, h, pad) {
    const heard = new Set(out);
    for (let pass = 0; pass < ops.length; pass++) for (const [a, b] of links) if (a !== b && heard.has(b)) heard.add(a);
    const lvl = {};
    for (const k of ops) {
      lvl[k] = 0;
      for (const [a, b] of links) if (a === k && b < k) lvl[k] = Math.max(lvl[k], lvl[b] + 1);
    }
    const idle = ops.filter((k) => !heard.has(k));
    const rows = Math.max(1, ...ops.map((k) => lvl[k] + 1));
    const pos = {};
    for (let r = 0; r < rows; r++) {
      const row = ops.filter((k) => lvl[k] === r);
      row.forEach((k, i) => {
        pos[k] = { x: pad + ((w - 2 * pad) * (i + 0.5)) / row.length, y: h - pad - ((h - 2 * pad) * (r + 0.5)) / rows };
      });
    }
    return { pos, idle };
  }
  // a link: straight down, or curved when it goes up or sideways
  function edge(p, q, r) {
    if (q.y > p.y + 1) return `M${p.x},${p.y + r} L${q.x},${q.y - r}`;
    const mx = (p.x + q.x) / 2 + (Math.abs(q.x - p.x) < 1 ? r * 2.4 : 0);
    const my = Math.min(p.y, q.y) - r * 2.2;
    return `M${p.x + (q.x >= p.x ? r * 0.7 : -r * 0.7)},${p.y - r * 0.7} Q${mx},${my} ${q.x + (q.x >= p.x ? -r * 0.7 : r * 0.7)},${q.y - r * 0.7}`;
  }
  const loop = (p, r) => `M${p.x + r * 0.5},${p.y - r * 0.85} C${p.x + r * 2.2},${p.y - r * 2.6} ${p.x + r * 2.8},${p.y - r * 0.2} ${p.x + r},${p.y}`;

  const big = $derived(layout(cur.links, ops.filter((k) => cur.out[k] > 0), 260, 150, 22));

  // dragging a cell: 120 px for the whole range
  let drag = null;
  function down(e, name) {
    e.currentTarget.setPointerCapture(e.pointerId);
    drag = { name, y: e.clientY, v: get(name), moved: false };
    history.hold();
  }
  function move(e) {
    if (!drag) return;
    const v = Math.min(1, Math.max(0, drag.v + (drag.y - e.clientY) / 120));
    if (Math.abs(drag.y - e.clientY) > 2) drag.moved = true;
    if (drag.moved) s.setP(drag.name, Math.round(v * 1000) / 1000);
  }
  function up() {
    if (drag) history.release();
    drag = null;
  }
  function flip(name, on) {
    history.step();
    s.setP(name, get(name) > 0 ? 0 : on);
  }
  function key_(e, name) {
    const step = e.shiftKey ? 0.01 : 0.05, v = get(name);
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') s.setP(name, Math.min(1, Math.round((v + step) * 1000) / 1000));
    else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') s.setP(name, Math.max(0, Math.round((v - step) * 1000) / 1000));
    else if (e.key === 'Delete' || e.key === 'Backspace') s.setP(name, 0);
    else if (e.key === 'Enter' || e.key === ' ') flip(name, name.endsWith('_out') ? 0.8 : 0.4);
    else return;
    e.preventDefault();
  }
  const desc = (name) => s.byName[name];
</script>

{#snippet diagram(links, out, fb, w, h, pad, r, thin)}
  {@const L = layout(links, out, w, h, pad)}
  <svg viewBox="0 0 {w} {h}" width={w} height={h} aria-hidden="true">
    <line class="bus" x1={pad * 0.6} x2={w - pad * 0.6} y1={h - 2} y2={h - 2} />
    {#each out as k}
      {@const p = L.pos[k]}
      {#if p}<line class="outl" x1={p.x} y1={p.y + r} x2={p.x} y2={h - 2} />{/if}
    {/each}
    {#each links as [a, b, v]}
      {@const p = L.pos[a]}{@const q = L.pos[b]}
      {#if p && q && a !== b}
        <path class="link" class:up={q.y <= p.y + 1} d={edge(p, q, r)} stroke-width={thin ? 1.4 : 1 + 4 * (v ?? 0.5)} />
      {/if}
    {/each}
    {#each ops as k}
      {@const p = L.pos[k]}
      {#if fb && fb[k] > 0}<path class="link fb" d={loop(p, r)} stroke-width={thin ? 1.2 : 1 + 3 * fb[k]} />{/if}
      <circle class="node" class:carrier={out.includes(k)} class:idle={L.idle.includes(k)} cx={p.x} cy={p.y} {r} />
      {#if !thin}<text x={p.x} y={p.y + 4}>{k}</text>{/if}
    {/each}
  </svg>
{/snippet}

<div class="fm">
  <div class="algos" role="group" aria-label="Algorithms">
    {#each ALGOS as al, i}
      <button class="algo" class:on={algoKey(al) === curKey} title="Algorithm {i + 1}: {al.name}" aria-label="Algorithm {i + 1}: {al.name}"
              onclick={() => apply(al)}>
        {@render diagram(al.links, al.out, null, 40, 44, 7, 4, true)}
        <span>{i + 1}</span>
      </button>
    {/each}
  </div>

  <div class="big">
    {@render diagram(cur.links, ops.filter((k) => cur.out[k] > 0), cur.fb, 260, 150, 22, 11, false)}
    {#if big.idle.length}<p class="note">Not heard: op {big.idle.join(', ')}</p>{/if}
  </div>

  <table class="grid">
    <thead>
      <tr><th></th>{#each ops as b}<th>→ {b}</th>{/each}<th class="outh">Out</th></tr>
    </thead>
    <tbody>
      {#each ops as a}
        <tr>
          <th>Op {a}</th>
          {#each [...ops.map((b) => cellName(a, b)), `op${a}_out`] as name, j}
            {@const v = get(name)}
            {@const d = desc(name)}
            <td>
              <div class="cell" class:fb={j === a - 1} class:out={j === ops.length} class:set={v > 0}
                   role="slider" tabindex="0" aria-label={d?.name ?? name} aria-valuemin="0" aria-valuemax="1" aria-valuenow={v}
                   aria-valuetext={d ? format(d, v) : v} title="{d?.help ?? name}: {v.toFixed(2)}"
                   style="--v:{v}" onpointerdown={(e) => down(e, name)} onpointermove={move} onpointerup={up} onpointercancel={up}
                   ondblclick={() => flip(name, j === ops.length ? 0.8 : 0.4)} onkeydown={(e) => key_(e, name)}>
                <span>{v > 0 ? v.toFixed(2).replace(/^0/, '') : ''}</span>
              </div>
            </td>
          {/each}
        </tr>
      {/each}
    </tbody>
  </table>
  <p class="hint">Row: the modulator; column: what it modulates. Diagonal: feedback. Drag, arrows, double-click.</p>
</div>

<style>
  .fm { display: flex; flex-direction: column; gap: 10px; }
  .algos { display: grid; grid-template-columns: repeat(6, 1fr); gap: 4px; }
  .algo { display: flex; flex-direction: column; align-items: center; padding: 2px 0 1px; border-radius: 6px; background: var(--faint);
          color: var(--muted); border: 1px solid transparent; cursor: pointer; }
  .algo span { font-size: 10px; line-height: 1; }
  .algo:hover { color: var(--text); }
  .algo.on { border-color: var(--accent); color: var(--accent); background: var(--accent-soft); }
  .big { display: flex; flex-direction: column; align-items: center; }
  .big svg { max-width: 100%; height: auto; }
  .note, .hint { margin: 0; font-size: 11px; color: var(--muted); }
  svg :global(.bus) { stroke: var(--line); stroke-width: 2; }
  svg :global(.outl) { stroke: var(--line); stroke-width: 1.5; stroke-dasharray: 2 2; }
  svg :global(.link) { fill: none; stroke: var(--accent); stroke-linecap: round; opacity: .85; }
  svg :global(.link.up) { stroke-dasharray: 4 3; }
  svg :global(.node) { fill: var(--surface); stroke: var(--muted); stroke-width: 1.5; }
  svg :global(.node.carrier) { fill: var(--accent-soft); stroke: var(--accent); }
  svg :global(.node.idle) { opacity: .35; }
  svg :global(text) { font-size: 11px; text-anchor: middle; fill: var(--text); font-weight: 600; }
  .grid { border-collapse: separate; border-spacing: 3px; width: 100%; }
  .grid th { font-size: 11px; font-weight: 500; color: var(--muted); text-align: center; white-space: nowrap; }
  .grid tbody th { text-align: left; padding-right: 2px; }
  .grid .outh { color: var(--text); }
  .cell { position: relative; height: 30px; border-radius: 5px; background: var(--faint); overflow: hidden; cursor: ns-resize;
          touch-action: none; user-select: none; outline-offset: 1px; }
  .cell::before { content: ''; position: absolute; inset: auto 0 0 0; height: calc(var(--v) * 100%); background: var(--accent); opacity: .55; }
  .cell.fb { background: repeating-linear-gradient(135deg, var(--faint) 0 4px, transparent 4px 8px); }
  .cell.out::before { opacity: .8; }
  .cell span { position: relative; display: block; text-align: center; line-height: 30px; font-size: 11px; font-variant-numeric: tabular-nums;
               color: var(--text); }
  .cell:focus-visible { outline: 2px solid var(--accent); }
</style>
