<!-- RoutePanel.svelte — the side panel's body for one route (lib/routes.js): its source, the setting it moves (of
     the whole sound: any live setting of the instrument, the master or the effects; of each note: pitch, level, pan,
     brightness, timbre), its amount and, for the whole sound, a "via" source that scales it; Remove frees it. The
     amount slider is bipolar and quadratic (amount = position x |position|): fine near zero, where most useful
     amounts are; its value reads in the setting's units; double-click: 0. -->
<script>
  import Select from './Select.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import * as R from './routes.js';
  let { id } = $props();

  const x = $derived(R.get(s, id));
  const labels = $derived(R.srcLabels(s, x.kind));
  const srcOptions = $derived(labels.map((l, i) => ({ value: i, label: R.srcName(l) })));
  const dstOptions = $derived(x.kind === 'voice'
    ? (s.byName.vroute1_destination?.labels ?? []).map((l, i) => ({ value: i, label: human(l) }))
    : [{ heading: s.engine?.name ?? 'instrument' }, ...s.macroTargets.engine.map((d) => ({ value: d.id + 1, label: human(d.name) })),
       { heading: 'master and effects' }, ...s.macroTargets.fx.map((d) => ({ value: d.id + 1, label: human(d.name) }))]);
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;
  const P = (n) => `${x.pre}${x.r}_${n}`;
  function remove() { R.clear(s, id); s.closePanel(); }
</script>

<div class="sp-form">
  <span class="sp-label">Source</span>
  <Select value={x.src} options={srcOptions} label="Source" onchange={(v) => R.setSource(s, id, v)} />
  <span class="sp-label">Setting</span>
  <Select value={x.dst} options={dstOptions} disabled={!x.src} label="Setting moved" onchange={(v) => s.setP(P('destination'), v)} />
  <span class="sp-label">Amount</span>
  <span class="sp-amt">
    <input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(x.amount)} disabled={!x.src}
           style="--lo:{50 + Math.min(0, toPos(x.amount)) * 50}%; --hi:{50 + Math.max(0, toPos(x.amount)) * 50}%"
           oninput={(e) => s.setP(P('amount'), toAmount(+e.target.value))} ondblclick={() => s.setP(P('amount'), 0)}
           aria-label="Amount" title="double-click: 0" />
    <span class="mono muted">{x.src ? R.inUnits(s, x) : ''}</span>
  </span>
  {#if x.kind === 'part'}
    <span class="sp-label">Via</span>
    <Select value={x.via} disabled={!x.src} label="Scaled by" onchange={(v) => s.setP(P('via'), v)}
            options={labels.map((l, i) => ({ value: i, label: i ? `× ${R.srcName(l)}` : '—' }))} />
  {/if}
</div>
<p class="hint">{x.kind === 'part' ? 'On the whole sound: one modulation for every note.' : 'On each note: every note has its own.'}
  A new route moves nothing until its amount does.</p>
{#if x.src}<button class="btn ghost danger" onclick={remove}>Remove this route</button>{/if}
