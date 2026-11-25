<!-- RouteList.svelte — what a source of modulation moves, edited where the source is set (its side panel): a table of
     its routes (lib/routes.js), for the whole sound and / or for each note, each the setting it moves, its amount
     (a bipolar, quadratic slider: amount = position x |position|, fine near zero; read in the setting's units;
     double-click: 0) and, for the whole sound, a "via" source that scales it; × frees it. `adders` gives the panel's
     footer a button per kind to add one. -->
<script>
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import * as R from './routes.js';
  let { src, kinds } = $props(); // the source's name in the route lists ("mlfo1", "velocity"...), 'part' / 'voice'

  const rows = $derived(kinds.flatMap((kind) => R.keys(kind).map((k) => R.get(s, k)).filter((x) => x.src && x.srcLabel === src)));
  const engineDst = $derived(s.macroTargets.engine.map((d) => ({ value: d.id + 1, label: human(d.name) })));
  const fxDst = $derived(s.macroTargets.fx.map((d) => ({ value: d.id + 1, label: human(d.name) })));
  const dstOptions = (x) => (x.kind === 'voice'
    ? (s.byName.vroute1_destination?.labels ?? []).map((l, i) => ({ value: i, label: human(l) }))
    : [{ heading: s.engine?.name ?? 'instrument' }, ...engineDst, { heading: 'master and effects' }, ...fxDst]);
  const viaOptions = $derived(R.srcLabels(s, 'part').map((l, i) => ({ value: i, label: i ? `× ${R.srcName(l)}` : '—' })));
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;
  const P = (x, n) => `${x.pre}${x.r}_${n}`;
  const both = $derived(kinds.length > 1);
</script>

{#if rows.length}
  <table class="dt routes">
    <colgroup><col style="width:38%" /><col /><col style="width:5.5em" />{#if kinds.includes('part')}<col style="width:22%" />{/if}<col style="width:30px" /></colgroup>
    <thead><tr><th>Setting</th><th>Amount</th><th></th>{#if kinds.includes('part')}<th>Via</th>{/if}<th></th></tr></thead>
    <tbody>
      {#each rows as x (x.key)}
        <tr>
          <td><Select value={x.dst} options={dstOptions(x)} label="Setting moved" onchange={(v) => s.setP(P(x, 'destination'), v)} />
            {#if both}<small class="muted">{x.kind === 'part' ? 'whole sound' : 'each note'}</small>{/if}</td>
          <td><input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(x.amount)}
                     style="--lo:{50 + Math.min(0, toPos(x.amount)) * 50}%; --hi:{50 + Math.max(0, toPos(x.amount)) * 50}%"
                     oninput={(e) => s.setP(P(x, 'amount'), toAmount(+e.target.value))} ondblclick={() => s.setP(P(x, 'amount'), 0)}
                     aria-label="Amount" title="double-click: 0" /></td>
          <td class="mono muted num">{R.inUnits(s, x)}</td>
          {#if kinds.includes('part')}<td>{#if x.kind === 'part'}<Select value={x.via} options={viaOptions} label="Scaled by" onchange={(v) => s.setP(P(x, 'via'), v)} />{/if}</td>{/if}
          <td><button class="iconbtn" onclick={() => R.clear(s, x.key)} aria-label="Remove this route" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
    </tbody>
  </table>
{:else}<p class="hint">It moves nothing yet.</p>{/if}
