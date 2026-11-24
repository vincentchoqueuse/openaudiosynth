<!-- ModMatrix.svelte — modulation assignments as a table: those in use among the slots of the whole sound (8) or of
     each note (4), and Add assignment under them for a free one; each row a source, a setting (for the whole
     sound: the instrument's, or the master and the effects), an amount and (whole sound) a "via" source that scales
     it. With `only` (a source's name) it shows that source's assignments alone, without the source column, and Add
     gives it one more. The amount slider is bipolar and quadratic (amount = position x |position|): fine near
     zero, where most useful amounts are, and a long way to the extremes; its value reads in the setting's units.
     A new assignment starts at 0 (nothing moves until the slider does); double-click on the slider: 0. -->
<script>
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import Select from './Select.svelte';
  let { kind = 'part', only = null } = $props(); // the whole sound's 8 slots, or each note's 4 ('voice'); one source only
  const get = s.getP, set = s.setP;
  const N = $derived(kind === 'part' ? 8 : 4);
  const pre = $derived(kind === 'part' ? 'route' : 'vroute');
  const srcLabels = $derived(s.byName[pre + '1_source']?.labels ?? []);
  const vdstLabels = $derived(s.byName['vroute1_destination']?.labels ?? []);
  const dests = $derived(s.macroTargets.engine);
  const fxDests = $derived(s.macroTargets.fx); // part routes may move the master and the effects too
  const engineName = $derived(s.engine?.name ?? 'engine');
  const srcOf = (r) => Math.round(get(`${pre}${r}_source`));
  let added = $state({}); // empty slots opened with Add route (this sound only)
  $effect(() => { s.patchGen; added = {}; });
  const all = $derived(Array.from({ length: N }, (_, k) => k + 1));
  const onlyIdx = $derived(only ? srcLabels.indexOf(only) : -1);
  const slots = $derived(all.filter((r) => (only ? srcOf(r) === onlyIdx : srcOf(r) || added[r])));
  const free = $derived(all.find((r) => !srcOf(r) && !added[r]));
  const add = () => { if (only) setSource(free, onlyIdx); else added[free] = true; };

  // slider position <-> amount (quadratic)
  const toPos = (a) => Math.sign(a) * Math.sqrt(Math.abs(a));
  const toAmount = (p) => Math.round(p * Math.abs(p) * 1000) / 1000;

  const LABEL = { mlfo1: 'Global LFO 1', mlfo2: 'Global LFO 2', mseg1: 'Global env 1', mseg2: 'Global env 2', vmseg: 'Note env', vlfo: 'Note LFO',
    mod_wheel: 'Mod wheel', pitch_bend: 'Pitch bend', wander: 'Wander' };
  const srcName = (l) => LABEL[l] ?? l.replace(/^macro(\d)$/, 'Macro $1').replace(/^./, (c) => c.toUpperCase());

  const srcOptions = $derived(srcLabels.map((l, i) => ({ value: i, label: i ? srcName(l) : '—' })));
  const dstOptions = $derived([{ heading: engineName }, ...dests.map((d) => ({ value: d.id + 1, label: human(d.name) })),
                               { heading: 'master and effects' }, ...fxDests.map((d) => ({ value: d.id + 1, label: human(d.name) }))]);

  function setSource(r, v) {
    set(`${pre}${r}_source`, v);
    if (!v) { clear(r); added[r] = true; return; }
    if (kind === 'part' && !Math.round(get(`${pre}${r}_destination`)) && dests.length) set(`${pre}${r}_destination`, dests[0].id + 1);
  }
  function clear(r) {
    delete added[r];
    set(`${pre}${r}_amount`, 0);
    set(`${pre}${r}_source`, 0);
    if (kind === 'part') { set(`${pre}${r}_destination`, 0); set(`${pre}${r}_via`, 0); }
  }
  // what the amount means in the destination's units
  function inUnits(r) {
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

{#if slots.length}
<table class="matrix">
  <colgroup><col class="c-num" />{#if !only}<col class="c-src" />{/if}<col class="c-dst" /><col /><col class="c-val" />{#if kind === 'part'}<col class="c-via" />{/if}<col class="c-x" /></colgroup>
  <thead><tr><th>#</th>{#if !only}<th>Source</th>{/if}<th>Setting</th><th>Amount</th><th></th>{#if kind === 'part'}<th>Via</th>{/if}<th></th></tr></thead>
  <tbody>
    {#each slots as r (r)}
      {@const src = srcOf(r)}
      {@const a = get(`${pre}${r}_amount`)}
      <tr class:empty={!src}>
        <td class="num mono">{r}</td>
        {#if !only}<td><Select value={src} onchange={(v) => setSource(r, v)} label="Source of assignment {r}" options={srcOptions} /></td>{/if}
        <td>
          {#if kind === 'part'}
            <Select value={Math.round(get(`${pre}${r}_destination`))} disabled={!src} onchange={(v) => set(`${pre}${r}_destination`, v)} label="Destination of assignment {r}"
                    options={src ? dstOptions : [{ value: 0, label: '—' }, ...dstOptions]} />
          {:else}
            <Select value={Math.round(get(`${pre}${r}_destination`))} disabled={!src} onchange={(v) => set(`${pre}${r}_destination`, v)} label="Destination of assignment {r}"
                    options={vdstLabels.map((l, i) => ({ value: i, label: l }))} />
          {/if}
        </td>
        <td class="amt">
          <input type="range" class="bipolar" min="-1" max="1" step="0.002" value={toPos(a)} disabled={!src}
                 style="--lo:{50 + Math.min(0, toPos(a)) * 50}%; --hi:{50 + Math.max(0, toPos(a)) * 50}%"
                 oninput={(e) => set(`${pre}${r}_amount`, toAmount(+e.target.value))} ondblclick={() => set(`${pre}${r}_amount`, 0)}
                 aria-label="Amount of assignment {r}" title="double-click: 0" />
        </td>
        <td class="val mono" title="amount {a.toFixed(3)} of the destination's range">{src ? inUnits(r) : ''}</td>
        {#if kind === 'part'}
          <td><Select value={Math.round(get(`${pre}${r}_via`))} disabled={!src} onchange={(v) => set(`${pre}${r}_via`, v)} label="Scaled by"
                  options={srcLabels.map((l, i) => ({ value: i, label: i ? `× ${srcName(l)}` : '—' }))} /></td>
        {/if}
        <td><button class="x" onclick={() => clear(r)} aria-label="Remove assignment {r}" title="remove">×</button></td>
      </tr>
    {/each}
  </tbody>
</table>
{/if}
<div class="addrow">
  <button class="add" disabled={!free} onclick={add}>Add assignment</button>
  <span>{#if only}{slots.length ? '' : 'It moves nothing yet.'}{:else}{slots.length ? `${all.filter(srcOf).length} of ${N} in use` : `Nothing is modulated yet: an assignment sends a source (an LFO, an envelope, the velocity…) to a setting.`}{/if}</span>
</div>
