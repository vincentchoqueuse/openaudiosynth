<!-- Scrub.svelte — a setting in a table's summary that is set right there: its name, then its value, which a drag
     sideways moves (300 px over its whole travel, in its normalized space so exp curves feel right; Shift ten times
     finer; a choice steps one option every 24 px), the arrows step (1 %, Shift 0.1 %), a double-click types
     (parseValue: Enter sets, Escape leaves). The row does not open: the value is a control (DataTable). -->
<script>
  import { session as s } from './session.svelte.js';
  import { toNorm, fromNorm, format } from './synth.js';
  import { parseValue } from './slider.js';
  let { d, label } = $props();
  const value = $derived(s.values[d.id] ?? d.def);
  const choice = $derived(!!d.labels || d.curve === 'int');
  let typing = $state(false), dragging = $state(false);
  let x0 = 0, n0 = 0, v0 = 0, moved = false;

  const set = (v) => s.setValue(d, Math.min(d.max, Math.max(d.min, v)));
  function down(e) {
    if (e.button !== 0 || typing) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    x0 = e.clientX; n0 = toNorm(d, value); v0 = value; moved = false; dragging = true;
  }
  function move(e) {
    if (!dragging) return;
    const dx = e.clientX - x0;
    if (!moved && Math.abs(dx) < 3) return;
    moved = true;
    if (choice) set(Math.round(v0 + Math.trunc(dx / 24)));
    else set(fromNorm(d, n0 + dx / (e.shiftKey ? 3000 : 300)));
  }
  function up() { dragging = false; }
  function key(e) {
    const dir = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1 }[e.key];
    if (!dir) return;
    e.preventDefault();
    if (choice) set(Math.round(value) + dir);
    else set(fromNorm(d, toNorm(d, value) + dir * (e.shiftKey ? 0.001 : 0.01)));
  }
  function typed(e) {
    if (e.key === 'Escape') { e.stopPropagation(); typing = false; return; }
    if (e.key !== 'Enter') return;
    const v = parseValue(d, e.target.value);
    if (v !== null) set(v);
    typing = false;
  }
  const focus = (node) => { node.focus(); node.select(); };
</script>

<span class="sv"><span class="sl">{label}</span>
  {#if typing}<input class="st mono" value={format(d, value)} use:focus onkeydown={typed} onblur={() => (typing = false)} aria-label="{label}: type a value" />
  {:else}<button class="sb" class:dragging onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up} onkeydown={key}
                 ondblclick={() => (typing = true)} title="{label}: drag sideways to set (Shift: finer), double-click to type">{format(d, value)}</button>{/if}
</span>

<style>
  .sv { white-space: nowrap; }
  .sl { color: var(--muted); }
  .sb { padding: 0 2px; border: 0; border-bottom: 1px dotted transparent; border-radius: 0; background: none; font: inherit; color: var(--text);
    cursor: ew-resize; touch-action: none; }
  .sb:hover, .sb.dragging, .sb:focus-visible { border-bottom-color: var(--accent); color: var(--accent); outline: none; }
  .st { width: 7em; padding: 0 4px; font-size: inherit; border: 1px solid var(--accent); border-radius: 4px; background: var(--surface); color: var(--text); }
</style>
