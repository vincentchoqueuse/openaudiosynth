<!-- ModPanel.svelte — the Modulation pill, its two tabs (chosen in SynthTab) as the Macros': Modulators (the part's modulators chosen in
     one row: the two envelopes and the per-note one, the two LFOs and the per-note one, a dot on those assigned;
     each shown the same way, MsegEditor or LfoView: a bar of choices, its drawing, a bar of settings) and
     Assignments (the matrix of the part or of each note, chosen the same way: every slot, filled or empty; ModMatrix). -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import ModMatrix from './ModMatrix.svelte';
  import Param from './Param.svelte';
  import Seg from './Seg.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  let { view } = $props(); // 'mods' | 'assign'
  const get = s.getP;
  // [label, prefix, route source name, kind, editor]
  const MODS = [
    ['Env 1', 'mseg1_', 'mseg1', 'part', 'env'], ['Env 2', 'mseg2_', 'mseg2', 'part', 'env'], ['Note env', 'vmseg_', 'vmseg', 'voice', 'env'],
    ['LFO 1', 'mlfo1_', 'mlfo1', 'part', 'lfo'], ['LFO 2', 'mlfo2_', 'mlfo2', 'part', 'lfo'], ['Note LFO', 'vlfo_', 'vlfo', 'voice', 'lfo'],
  ];
  let sel = $state(0), scope = $state('part'); // which modulator; the routes of the part or of each note
  const m = $derived(MODS[sel]);
  // does a modulator drive anything? (a route from it)
  function drives(src, kind) {
    const pre = kind === 'part' ? 'route' : 'vroute', n = kind === 'part' ? 8 : 4;
    const labels = s.byName[pre + '1_source']?.labels ?? [];
    for (let r = 1; r <= n; r++) if (labels[Math.round(get(`${pre}${r}_source`))] === src && get(`${pre}${r}_amount`) !== 0) return true;
    return false;
  }
  // part modulation parameters no editor shows (none today; kept so a new one is never hidden)
  const rest = $derived(s.shared.filter((d) => d.id >= 1024 && !/^(mseg\d|vmseg|route\d|vroute\d|map\d|macro\d|mlfo\d|vlfo)/.test(d.name)));
</script>

{#if view === 'mods'}
  <section class="card">
    <div class="card-head">
      <Seg items={MODS.map(([label, , src, kind], i) => ({ id: i, label, dot: drives(src, kind) }))} value={sel} onpick={(i) => (sel = i)} label="Modulator" />
    </div>
    {#key sel}
      {#if m[4] === 'env'}
        <MsegEditor prefix={m[1]} />
      {:else}
        <LfoView prefix={m[1]} />
      {/if}
    {/key}
  </section>
{:else}
  <section class="card">
    <div class="card-head">
      <Seg items={[{ id: 'part', label: 'Part' }, { id: 'voice', label: 'Each note' }]} value={scope} onpick={(v) => (scope = v)} label="Routes of" />
    </div>
    {#key scope}<ModMatrix kind={scope} />{/key}
  </section>
{/if}

{#if rest.length}
  <section class="card">
    <h2>Other</h2>
    {#each rest as d (d.id)}
      <Param {d} label={human(d.name)} />
    {/each}
  </section>
{/if}
