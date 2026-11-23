<!-- ModMatrix.svelte — the modulation matrix as a table: every slot of the part (8 routes) or of the notes (4),
     filled or empty, each row a source, a destination (a part route: the engine's parameters, or the master and
     the effects), an amount and (part) a "via" source that scales it. The
     amount slider is bipolar and quadratic (amount = position x |position|): fine near zero, where most useful
     amounts are, and a long way to the extremes; its value reads in the destination's units. A new route starts
     at 0 (nothing moves until the slider does); double-click on the slider: 0. -->
<script>
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import Select from './Select.svelte';
  let { kind = 'part' } = $props(); // the part's 8 routes, or each note's 4 ('voice')
  const get = s.getP, set = s.setP;
  const N = $derived(kind === 'part' ? 8 : 4);
  const pre = $derived(kind === 'part' ? 'route' : 'vroute');
  const srcLabels = $derived(s.byName[pre + '1_source']?.labels ?? []);
  const vdstLabels = $derived(s.byName['vroute1_destination']?.labels ?? []);
  const dests = $derived(s.macroTargets.engine);
  const fxDests = $derived(s.macroTargets.fx); // part routes may move the master and the effects too
  const engineName = $derived(s.engine?.name ?? 'engine');
  const srcOf = (r) => Math.round(get(`${pre}${r}_source`));
  const slots = $derived(Array.from({ length: N }, (_, k) => k + 1));

  // slider position <-> amount (quadratic)
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;

  const LABEL = { mlfo1: 'LFO 1', mlfo2: 'LFO 2', mseg1: 'Env 1', mseg2: 'Env 2', vmseg: 'Note env', vlfo: 'Note LFO',
    mod_wheel: 'Mod wheel', pitch_bend: 'Pitch bend' };
  const srcName = (l) => LABEL[l] ?? l.replace(/^macro(\d)$/, 'Macro $1').replace(/^./, (c) => c.toUpperCase());

  const srcOptions = $derived(srcLabels.map((l, i) => ({ value: i, label: i ? srcName(l) : '—' })));
  const dstOptions = $derived([{ heading: engineName }, ...dests.map((d) => ({ value: d.id + 1, label: human(d.name) })),
                               { heading: 'master and effects' }, ...fxDests.map((d) => ({ value: d.id + 1, label: human(d.name) }))]);

  function setSource(r, v) {
    set(`${pre}${r}_source`, v);
    if (!v) { clear(r); return; }
    if (kind === 'part' && !Math.round(get(`${pre}${r}_destination`)) && dests.length) set(`${pre}${r}_destination`, dests[0].id + 1);
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
      if (dst === 0) return Math.abs(a * 12) < 1 ? `${sign(a)}${(a * 1200).toFixed(0)} ct` : `${sign(a)}${(a * 12).toFixed(2)} st`;
      if (dst === 1) return `${sign(a)}${(a * 24).toFixed(1)} dB`;
      if (dst === 3) return `${sign(a)}${(a * 4).toFixed(2)} oct`;
      return `${sign(a)}${(a * 100).toFixed(0)} %`;
    }
    const d = s.paramById(Math.round(get(`${pre}${r}_destination`)) - 1);
    if (!d) return '';
    const v = d.curve === 'exp' ? a * Math.log2(d.max / d.min) : a * (d.max - d.min);
    const u = d.curve === 'exp' ? 'oct' : d.unit || '';
    return `${sign(v)}${+v.toPrecision(3)} ${u}`.trim();
  }
</script>

<table class="matrix {kind}">
  <thead><tr><th>#</th><th>Source</th><th>Destination</th><th>Amount</th><th></th>{#if kind === 'part'}<th>Via</th>{/if}<th></th></tr></thead>
  <tbody>
    {#each slots as r (r)}
      {@const s = srcOf(r)}
      {@const a = get(`${pre}${r}_amount`)}
      <tr class:empty={!s}>
        <td class="num mono">{r}</td>
          <td><Select value={s} onchange={(v) => setSource(r, v)} label="Source of slot {r}" options={srcOptions} /></td>
        <td>
          {#if kind === 'part'}
            <Select value={Math.round(get(`${pre}${r}_destination`))} disabled={!s} onchange={(v) => set(`${pre}${r}_destination`, v)} label="Destination of slot {r}"
                    options={s ? dstOptions : [{ value: 0, label: '—' }, ...dstOptions]} />
          {:else}
            <Select value={Math.round(get(`${pre}${r}_destination`))} disabled={!s} onchange={(v) => set(`${pre}${r}_destination`, v)} label="Destination of slot {r}"
                    options={vdstLabels.map((l, i) => ({ value: i, label: l }))} />
          {/if}
        </td>
        <td class="amt">
          <input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(a)} disabled={!s}
                 style="--lo:{50 + Math.min(0, toPos(a)) * 50}%; --hi:{50 + Math.max(0, toPos(a)) * 50}%"
                 oninput={(e) => set(`${pre}${r}_amount`, toAmount(+e.target.value))} ondblclick={() => set(`${pre}${r}_amount`, 0)}
                 aria-label="Amount of slot {r}" title="double-click: 0" />
        </td>
        <td class="val mono" title="amount {a.toFixed(3)} of the destination's range">{s ? effect(r) : ''}</td>
        {#if kind === 'part'}
          <td><Select class="via" value={Math.round(get(`${pre}${r}_via`))} disabled={!s} onchange={(v) => set(`${pre}${r}_via`, v)} label="Scaled by"
                  options={srcLabels.map((l, i) => ({ value: i, label: i ? `× ${srcName(l)}` : '—' }))} /></td>
        {/if}
        <td>{#if s}<button class="x" onclick={() => clear(r)} aria-label="Clear slot {r}" title="clear">×</button>{/if}</td>
      </tr>
    {/each}
  </tbody>
</table>
