<!-- StepGrid.svelte — the sequence as a grid: scale notes (high on top) x steps, beats marked, the playhead lit.
     Click cycles note -> accent -> soft -> off, Shift-click holds the note from the previous step, right-click
     clears, dragging paints the value of the first cell. -->
<script>
  import { sequence as q } from './sequence.svelte.js';
  import { noteName } from './seqExport.js';
  let paint = null; // value applied while dragging across cells
  const cycle = (c) => (!c ? 'on' : c === 'on' ? 'acc' : c === 'acc' ? 'soft' : null);
  function down(e, n, s) {
    e.preventDefault();
    const c = q.get(n, s);
    paint = e.shiftKey ? (c === 'tie' ? null : 'tie') : e.button === 2 ? null : cycle(c);
    q.set(n, s, paint);
  }
  function enter(e, n, s) { if (paint !== null && e.buttons) q.set(n, s, paint); else if (e.buttons === 0) paint = null; }
</script>

<div class="grid" style="--steps:{q.steps}" oncontextmenu={(e) => e.preventDefault()} role="grid" tabindex="0" aria-label="Step grid">
  {#each q.rows as n (n)}
    <div class="rowlabel" class:root={n % 12 === q.root}>{noteName(n)}</div>
    {#each Array(q.steps) as _, s}
      {@const c = q.get(n, s)}
      <div class="cell {c ?? ''}" class:beat={s % 4 === 0} class:now={s === q.playStep}
           onpointerdown={(e) => down(e, n, s)} onpointerenter={(e) => enter(e, n, s)} role="gridcell" tabindex="-1" aria-selected={!!c}></div>
    {/each}
  {/each}
</div>
