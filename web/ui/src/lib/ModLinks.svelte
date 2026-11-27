<!-- ModLinks.svelte — the Modulation section of a stage's panel: everything that moves one of its settings, as a table.
     A route of the whole sound (lib/routes.js): its source, the setting, its amount (a bipolar, quadratic slider read in
     the setting's units; double-click: 0), × to remove it; all set here. Edit (the cog) opens the route's source or
     the macro in the panel, and Back returns here. A macro: its name (a click also opens it, where its range is set), the setting, its travel. `add()` (for the panel's footer) makes a route to the stage's first
     setting a route can move, from the first global LFO, at a tenth of its travel. -->
<script>
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { fromNorm, format } from './synth.js';
  import * as R from './routes.js';
  import { modRows } from './mods.js';
  let { params } = $props(); // the stage's settings: [{d, label}]

  const movable = $derived(params.filter(({ d }) => d.realtime && (s.macroTargets.engine.some((x) => x.id === d.id) || s.macroTargets.fx.some((x) => x.id === d.id))));
  const ids = $derived(new Set(params.map(({ d }) => d.id)));
  const routes = $derived(R.keys('part').map((k) => R.get(s, k)).filter((x) => x.src && ids.has(x.dst - 1)));
  const maps = $derived(s.macroMaps.filter((m) => m.macro >= 0 && m.dst && ids.has(m.dst.id)));
  const srcOptions = $derived(R.srcLabels(s, 'part').map((l, i) => ({ value: i, label: R.srcName(l) })).slice(1));
  const dstOptions = $derived(movable.map(({ d, label }) => ({ value: d.id + 1, label })));
  const nameOf = (id) => params.find(({ d }) => d.id === id)?.label ?? '';
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;
  const P = (x, n) => `${x.pre}${x.r}_${n}`;
  // a route's source, to edit it: a macro's panel, or the modulator's (envelope, LFO, Wander, controller)
  function editSource(x) {
    const label = R.srcLabels(s, 'part')[x.src] ?? '', mac = /^macro(\d)$/.exec(label);
    if (mac) return () => s.editPanel('macro', 'm' + (+mac[1] - 1));
    const row = modRows().find((r) => r.src === label);
    return row ? () => s.editPanel('mod', row.key) : null;
  }
  export const canAdd = () => !!R.free(s, 'part') && movable.length > 0;
  export function add() {
    const k = R.free(s, 'part');
    if (!k || !movable.length) return;
    const { r, pre } = R.parse(k);
    s.setP(`${pre}${r}_source`, Math.max(1, R.srcLabels(s, 'part').indexOf('mlfo1')));
    s.setP(`${pre}${r}_destination`, movable[0].d.id + 1);
    s.setP(`${pre}${r}_amount`, 0.1);
  }
</script>

{#if routes.length || maps.length}
  <table class="dt routes">
    <colgroup><col style="width:31%" /><col style="width:25%" /><col /><col style="width:6.8em" /><col style="width:30px" /><col style="width:30px" /></colgroup>
    <thead><tr><th>Source</th><th>Setting</th><th>Amount</th><th></th><th></th><th></th></tr></thead>
    <tbody>
      {#each routes as x (x.key)}
        {@const edit = editSource(x)}
        <tr>
          <td><Select value={x.src} options={srcOptions} label="Source" onchange={(v) => s.setP(P(x, 'source'), v)} /></td>
          <td><Select value={x.dst} options={dstOptions} label="Setting moved" onchange={(v) => s.setP(P(x, 'destination'), v)} /></td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(x.amount)}
                     style="--lo:{50 + Math.min(0, toPos(x.amount)) * 50}%; --hi:{50 + Math.max(0, toPos(x.amount)) * 50}%"
                     oninput={(e) => s.setP(P(x, 'amount'), toAmount(+e.target.value))} ondblclick={() => s.setP(P(x, 'amount'), 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{R.inUnits(s, x)}</td>
          <td><button class="iconbtn" onclick={edit} disabled={!edit} aria-label="Edit {R.srcName(R.srcLabels(s, 'part')[x.src])}" title="Edit the source (Back returns here)"><Icon name="settings" size={13} /></button></td>
          <td><button class="iconbtn" onclick={() => R.clear(s, x.key)} aria-label="Remove this modulation" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
      {#each maps as m (m.slot)}
        <tr class="openable" onclick={() => s.editPanel('macro', 'm' + m.macro)} title="Edit the macro (Back returns here)">
          <td><span class="strong">{s.macroName(m.macro)}</span> <span class="muted">macro {m.macro + 1}</span></td>
          <td>{nameOf(m.dst.id)}</td>
          <td class="mono muted" colspan="2">{format(m.dst, fromNorm(m.dst, m.from))} → {format(m.dst, fromNorm(m.dst, m.to))}</td>
          <td><button class="iconbtn" aria-label="Edit {s.macroName(m.macro)}" title="Edit the macro (Back returns here)"><Icon name="settings" size={13} /></button></td>
          <td></td>
        </tr>
      {/each}
    </tbody>
  </table>
{:else}<p class="hint">Nothing moves its settings yet.</p>{/if}
