<!-- RouteList.svelte — the Moves section of a source of modulation, edited where the source is set (its side panel):
     a table of what it moves, one row per link, the same columns for every kind of link: the setting, its amount (a
     bipolar slider; read in the setting's units; double-click: 0), a "via" source that scales it when the link has
     one, and × to free it. The links are the routes of the whole sound and of each note (lib/routes.js) and the slots
     of the instrument's own matrix (lib/matrix.js, `msrc`); "+ Add" under the table adds one of each kind the source
     feeds. -->
<script>
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import AddButton from './AddButton.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import * as R from './routes.js';
  import * as M from './matrix.js';
  let { src = null, kinds = [], msrc = null } = $props(); // the source's name in the route lists ("mlfo1", "velocity"...), 'part' / 'voice'; its name in the matrix

  const rows = $derived(src ? kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k)).filter((x) => x.src && x.srcLabel === src)) : []);
  const slots = $derived(msrc ? M.from(s, msrc) : []);
  const engineDst = $derived(s.macroTargets.engine.map((d) => ({ value: d.id + 1, label: human(d.name) })));
  const fxDst = $derived(s.macroTargets.fx.map((d) => ({ value: d.id + 1, label: human(d.name) })));
  const dstOptions = (x) => (x.kind === 'voice'
    ? (s.byName.vroute1_destination?.labels ?? []).map((l, i) => ({ value: i, label: human(l) }))
    : [{ heading: s.engine?.name ?? 'instrument' }, ...engineDst, { heading: 'master and effects' }, ...fxDst]);
  const viaOptions = $derived(R.srcLabels(s, 'part').map((l, i) => ({ value: i, label: i ? `× ${R.srcName(l)}` : '—' })));
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;
  const P = (x, n) => `${x.pre}${x.r}_${n}`;
  const where = $derived((src ? kinds.length : 0) + (msrc ? 1 : 0) > 1); // links of several kinds: each says which
  const KIND = { part: 'whole sound', voice: 'each note' };
  const adders = $derived([
    ...(src ? kinds.map((kind) => ({ label: kind === 'part' ? 'On the whole sound' : 'On each note', run: () => R.addFrom(s, kind, src), disabled: !R.free(s, kind) })) : []),
    ...(msrc ? [{ label: `In the ${s.engine?.name ?? 'instrument'} (its matrix)`, run: () => M.add(s, msrc), disabled: !M.free(s) }] : []),
  ]);
</script>

{#if rows.length || slots.length}
  <table class="dt routes">
    <colgroup><col style="width:38%" /><col /><col style="width:5.5em" /><col style="width:22%" /><col style="width:30px" /></colgroup>
    <thead><tr><th>Setting</th><th>Amount</th><th></th><th>Via</th><th></th></tr></thead>
    <tbody>
      {#each rows as x (x.key)}
        <tr>
          <td><Select value={x.dst} options={dstOptions(x)} label="Setting moved" onchange={(v) => s.setP(P(x, 'destination'), v)} />
            {#if where}<small class="muted">{KIND[x.kind]}</small>{/if}</td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(x.amount)}
                     style="--lo:{50 + Math.min(0, toPos(x.amount)) * 50}%; --hi:{50 + Math.max(0, toPos(x.amount)) * 50}%"
                     oninput={(e) => s.setP(P(x, 'amount'), toAmount(+e.target.value))} ondblclick={() => s.setP(P(x, 'amount'), 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{R.inUnits(s, x)}</td>
          <td>{#if x.kind === 'part'}<Select value={x.via} options={viaOptions} label="Scaled by" onchange={(v) => s.setP(P(x, 'via'), v)} />{/if}</td>
          <td><button class="iconbtn" onclick={() => R.clear(s, x.key)} aria-label="Remove this link" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
      {#each slots as x (x.k)}
        <tr>
          <td><Select value={Math.round(s.getP(`mod${x.k}_destination`))} options={M.dstOptions(s)} label="Setting moved" onchange={(v) => M.set(s, x, 'destination', v)} />
            {#if where}<small class="muted">{s.engine?.name ?? 'instrument'}</small>{/if}</td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={x.amount}
                     style="--lo:{50 + Math.min(0, x.amount) * 50}%; --hi:{50 + Math.max(0, x.amount) * 50}%"
                     oninput={(e) => M.set(s, x, 'amount', +e.target.value)} ondblclick={() => M.set(s, x, 'amount', 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{M.inUnits(x)}</td>
          <td><Select value={x.via} options={M.viaOptions(s)} label="Scaled by" onchange={(v) => M.set(s, x, 'via', v)} /></td>
          <td><button class="iconbtn" onclick={() => M.clear(s, x)} aria-label="Remove this link" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
    </tbody>
  </table>
{:else}<p class="hint">It moves nothing yet.</p>{/if}
{#if adders.length}<AddButton items={adders} />{/if}
