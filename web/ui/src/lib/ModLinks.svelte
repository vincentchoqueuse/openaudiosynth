<!-- ModLinks.svelte — the Moved by section of a stage's panel: everything that moves one of its settings, as one
     table, the same columns for every kind of link: the source, the setting, the amount (a bipolar slider, read in the
     setting's units; double-click: 0), Edit (the arrow: opens the source in the panel, Back returns here) and ×.
     The links are the routes of the whole sound (lib/routes.js), the slots of the instrument's own matrix that end on
     this stage (lib/matrix.js) and the macros (their travel, set in the macro's panel). "+ Add" under the table makes
     a route to the stage's first setting a route can move, from the first global LFO, at a tenth of its travel. -->
<script>
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import AddButton from './AddButton.svelte';
  import { session as s } from './session.svelte.js';
  import { fromNorm, format } from './synth.js';
  import * as R from './routes.js';
  import * as M from './matrix.js';
  import { modRows } from './mods.js';
  let { params, stage = '' } = $props(); // the stage's settings: [{d, label}], and its key

  const movable = $derived(params.filter(({ d }) => d.realtime && (s.macroTargets.engine.some((x) => x.id === d.id) || s.macroTargets.fx.some((x) => x.id === d.id))));
  const ids = $derived(new Set(params.map(({ d }) => d.id)));
  const routes = $derived(R.keys('part').map((k) => R.get(s, k)).filter((x) => x.src && ids.has(x.dst - 1)));
  const slots = $derived(M.slots(s).filter((x) => { const d = M.dstParam(s, x.dst); return d ? ids.has(d.id) : M.dstStage(x.dst) === stage; }));
  const maps = $derived(s.macroMaps.filter((m) => m.macro >= 0 && m.dst && ids.has(m.dst.id)));
  const srcOptions = $derived(R.srcLabels(s, 'part').map((l, i) => ({ value: i, label: R.srcName(l) })).slice(1));
  const msrcOptions = $derived((s.byName.mod1_source?.labels ?? []).map((l, i) => ({ value: i, label: M.srcName(l) })).slice(1));
  const dstOptions = $derived(movable.map(({ d, label }) => ({ value: d.id + 1, label })));
  const nameOf = (id) => params.find(({ d }) => d.id === id)?.label ?? '';
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;
  const P = (x, n) => `${x.pre}${x.r}_${n}`;
  // a link's source, to edit it: a macro's panel, the modulator's (envelope, LFO, Wander, controller), or the
  // instrument's stage (its envelope, its LFO)
  function editSource(label) {
    const mac = /^macro(\d)$/.exec(label);
    if (mac) return () => s.editPanel('macro', 'm' + (+mac[1] - 1));
    const row = modRows().find((r) => r.src === label);
    return row ? () => s.editPanel('mod', row.key) : null;
  }
  const STAGE_OF = { env2: 'env2', lfo1: 'lfo1', lfo2: 'lfo2', amp_env: 'amp' };
  const editSlot = (x) => (STAGE_OF[x.src] ? () => s.editPanel('stage', STAGE_OF[x.src]) : editSource(x.src));
  function add() {
    const k = R.free(s, 'part');
    if (!k || !movable.length) return;
    const { r, pre } = R.parse(k);
    s.setP(`${pre}${r}_source`, Math.max(1, R.srcLabels(s, 'part').indexOf('mlfo1')));
    s.setP(`${pre}${r}_destination`, movable[0].d.id + 1);
    s.setP(`${pre}${r}_amount`, 0.1);
  }
</script>

{#if routes.length || slots.length || maps.length}
  <table class="dt routes">
    <colgroup><col style="width:31%" /><col style="width:25%" /><col /><col style="width:6.8em" /><col style="width:30px" /><col style="width:30px" /></colgroup>
    <thead><tr><th>Source</th><th>Setting</th><th>Amount</th><th></th><th></th><th></th></tr></thead>
    <tbody>
      {#each routes as x (x.key)}
        {@const edit = editSource(R.srcLabels(s, 'part')[x.src] ?? '')}
        <tr>
          <td><Select value={x.src} options={srcOptions} label="Source" onchange={(v) => s.setP(P(x, 'source'), v)} /></td>
          <td><Select value={x.dst} options={dstOptions} label="Setting moved" onchange={(v) => s.setP(P(x, 'destination'), v)} /></td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(x.amount)}
                     style="--lo:{50 + Math.min(0, toPos(x.amount)) * 50}%; --hi:{50 + Math.max(0, toPos(x.amount)) * 50}%"
                     oninput={(e) => s.setP(P(x, 'amount'), toAmount(+e.target.value))} ondblclick={() => s.setP(P(x, 'amount'), 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{R.inUnits(s, x)}</td>
          <td><button class="iconbtn" onclick={edit} disabled={!edit} aria-label="Open the source" title="Open the source (Back returns here)"><Icon name="next" size={13} /></button></td>
          <td><button class="iconbtn" onclick={() => R.clear(s, x.key)} aria-label="Remove this link" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
      {#each slots as x (x.k)}
        {@const edit = editSlot(x)}
        <tr>
          <td><Select value={Math.round(s.getP(`mod${x.k}_source`))} options={msrcOptions} label="Source" onchange={(v) => M.set(s, x, 'source', v)} /></td>
          <td>{M.dstName(x.dst)}</td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={x.amount}
                     style="--lo:{50 + Math.min(0, x.amount) * 50}%; --hi:{50 + Math.max(0, x.amount) * 50}%"
                     oninput={(e) => M.set(s, x, 'amount', +e.target.value)} ondblclick={() => M.set(s, x, 'amount', 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{M.inUnits(x)}</td>
          <td><button class="iconbtn" onclick={edit} disabled={!edit} aria-label="Open the source" title="Open the source (Back returns here)"><Icon name="next" size={13} /></button></td>
          <td><button class="iconbtn" onclick={() => M.clear(s, x)} aria-label="Remove this link" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
      {#each maps as m (m.slot)}
        <tr>
          <td><span class="strong">{s.macroName(m.macro)}</span> <span class="muted">macro {m.macro + 1}</span></td>
          <td>{nameOf(m.dst.id)}</td>
          <td class="mono muted" colspan="2">{format(m.dst, fromNorm(m.dst, m.from))} → {format(m.dst, fromNorm(m.dst, m.to))}</td>
          <td><button class="iconbtn" onclick={() => s.editPanel('macro', 'm' + m.macro)} aria-label="Open {s.macroName(m.macro)}" title="Open the macro (Back returns here)"><Icon name="next" size={13} /></button></td>
          <td><button class="iconbtn" onclick={() => { s.setP(`map${m.slot}_macro`, 0); s.setP(`map${m.slot}_destination`, 0); }} aria-label="Remove this link" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
    </tbody>
  </table>
{:else}<p class="hint">Nothing moves it yet.</p>{/if}
{#if movable.length}<AddButton items={[{ label: 'A route from a global LFO', run: add, disabled: !R.free(s, 'part') }]} />{/if}
