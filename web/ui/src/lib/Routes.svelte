<!-- Routes.svelte — modulation routes as readable rows: part routes (source -> engine parameter by name, amount,
     optional "via" source) or per-note routes (source -> pitch / level / pan / brightness / timbre). `only`
     restricts the list to one source (e.g. the envelope being edited) and "Add" creates a route from it. -->
<script>
  let { engine, get, set, byName, kind = 'part', only = null, human } = $props();
  const N = $derived(kind === 'part' ? 8 : 4);
  const pre = $derived(kind === 'part' ? 'route' : 'vroute');
  const srcLabels = $derived(byName[pre + '1_source']?.labels ?? []);
  const vdstLabels = $derived(byName['vroute1_destination']?.labels ?? []);
  const dests = $derived(engine.params.filter((d) => d.realtime));
  const srcOf = (r) => Math.round(get(`${pre}${r}_source`));
  const rows = $derived(Array.from({ length: N }, (_, k) => k + 1)
    .filter((r) => srcOf(r) > 0 && (!only || srcLabels[srcOf(r)] === only)));
  const free = $derived(Array.from({ length: N }, (_, k) => k + 1).find((r) => srcOf(r) === 0));

  function add() {
    if (!free) return;
    set(`${pre}${free}_source`, only ? srcLabels.indexOf(only) : 1);
    if (kind === 'part') set(`${pre}${free}_destination`, dests.length ? dests[0].id + 1 : 0);
    set(`${pre}${free}_amount`, 0.2);
  }
  function clear(r) {
    set(`${pre}${r}_amount`, 0);
    set(`${pre}${r}_source`, 0);
    if (kind === 'part') { set(`${pre}${r}_destination`, 0); set(`${pre}${r}_via`, 0); }
  }
  // what the amount means in the destination's units
  function effect(r) {
    const a = get(`${pre}${r}_amount`);
    const sign = (v) => (v >= 0 ? '+' : '');
    if (kind !== 'part') { // per-note destinations: pitch +/-12 st, level +/-24 dB, pan, brightness +/-4 oct, timbre
      const dst = Math.round(get(`${pre}${r}_destination`));
      if (dst === 0) return Math.abs(a * 12) < 1 ? `${sign(a)}${(a * 1200).toFixed(1)} cent` : `${sign(a)}${(a * 12).toFixed(2)} st`;
      if (dst === 1) return `${sign(a)}${(a * 24).toFixed(1)} dB`;
      if (dst === 3) return `${sign(a)}${(a * 4).toFixed(2)} oct`;
      return `${sign(a)}${a.toFixed(2)}`;
    }
    const d = engine.params[Math.round(get(`${pre}${r}_destination`)) - 1];
    if (!d) return '';
    const v = d.curve === 'exp' ? a * Math.log2(d.max / d.min) : a * (d.max - d.min);
    const u = d.curve === 'exp' ? 'oct' : d.unit;
    return `${v >= 0 ? '+' : ''}${+v.toPrecision(3)} ${u}`;
  }
</script>

<div class="routes">
  {#each rows as r (r)}
    {@const a = get(`${pre}${r}_amount`)}
    <div class="route {kind}" class:only={!!only}>
      {#if !only}
        <select value={srcOf(r)} onchange={(e) => set(`${pre}${r}_source`, +e.target.value)} aria-label="Source">
          {#each srcLabels as l, i}{#if i}<option value={i}>{l}</option>{/if}{/each}
        </select>
        <span class="arrow">→</span>
      {/if}
      {#if kind === 'part'}
        <select value={Math.round(get(`${pre}${r}_destination`))} onchange={(e) => set(`${pre}${r}_destination`, +e.target.value)} aria-label="Destination">
          {#each dests as d}<option value={d.id + 1}>{human(d.name)}</option>{/each}
        </select>
      {:else}
        <select value={Math.round(get(`${pre}${r}_destination`))} onchange={(e) => set(`${pre}${r}_destination`, +e.target.value)} aria-label="Destination">
          {#each vdstLabels as l, i}<option value={i}>{l}</option>{/each}
        </select>
      {/if}
      <input type="range" class="bipolar" min="-1" max="1" step="0.005" value={a}
             style="--lo:{50 + Math.min(0, a) * 50}%; --hi:{50 + Math.max(0, a) * 50}%"
             oninput={(e) => set(`${pre}${r}_amount`, +e.target.value)} ondblclick={() => set(`${pre}${r}_amount`, 0)} aria-label="Amount" />
      <span class="val" title="amount {a.toFixed(3)} of the destination's range">{effect(r)}</span>
      {#if kind === 'part'}
        <select class="via" value={Math.round(get(`${pre}${r}_via`))} onchange={(e) => set(`${pre}${r}_via`, +e.target.value)} aria-label="Scaled by">
          <option value={0}>no via</option>
          {#each srcLabels as l, i}{#if i}<option value={i}>× {l}</option>{/if}{/each}
        </select>
      {/if}
      <button class="x" onclick={() => clear(r)} aria-label="Remove route">×</button>
    </div>
  {/each}
  {#if free}
    <button class="ghost add" onclick={add}>+ {only ? `Route ${only} to…` : 'Add route'}</button>
  {:else if !rows.length}
    <span class="muted">All {N} routes are used by other sources.</span>
  {/if}
</div>
