<!-- ModPanel.svelte — the Modulation tab: every modulator of the part in one card, chosen in one row (the two
     envelopes and the per-note one, the two LFOs and the per-note one; a dot on those that drive something), each
     shown the same way: its editor (MsegEditor or LfoView: a bar of choices, its drawing, a bar of settings), then
     what it drives (its routes, and a button to add one). Below, every route of the part and of the notes. -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import Routes from './Routes.svelte';
  import Param from './Param.svelte';
  let { engine, shared, get, set, byName, human, values, setValue } = $props();
  // [label, prefix, route source name, kind, editor]
  const MODS = [
    ['Env 1', 'mseg1_', 'mseg1', 'part', 'env'], ['Env 2', 'mseg2_', 'mseg2', 'part', 'env'], ['Note env', 'vmseg_', 'vmseg', 'voice', 'env'],
    ['LFO 1', 'mlfo1_', 'mlfo1', 'part', 'lfo'], ['LFO 2', 'mlfo2_', 'mlfo2', 'part', 'lfo'], ['Note LFO', 'vlfo_', 'vlfo', 'voice', 'lfo'],
  ];
  let sel = $state(0);
  const m = $derived(MODS[sel]);
  // does a modulator drive anything? (a route from it)
  function drives(src, kind) {
    const pre = kind === 'part' ? 'route' : 'vroute', n = kind === 'part' ? 8 : 4;
    const labels = byName[pre + '1_source']?.labels ?? [];
    for (let r = 1; r <= n; r++) if (labels[Math.round(get(`${pre}${r}_source`))] === src && get(`${pre}${r}_amount`) !== 0) return true;
    return false;
  }
  // part modulation parameters no editor shows (none today; kept so a new one is never hidden)
  const rest = $derived(shared.filter((d) => d.id >= 1024 && !/^(mseg\d|vmseg|route\d|vroute\d|map\d|macro\d|mlfo\d|vlfo)/.test(d.name)));
</script>

<section class="card">
  <div class="card-head">
    <h2>Modulators</h2>
    <div class="seg small">
      {#each MODS as [label, , src, kind], i}
        <button class:on={sel === i} onclick={() => (sel = i)}>{label}{#if drives(src, kind)}<span class="dot" title="drives something"></span>{/if}</button>
      {/each}
    </div>
  </div>
  {#key sel}
    {#if m[4] === 'env'}
      <MsegEditor prefix={m[1]} {get} {set} />
    {:else}
      <LfoView prefix={m[1]} {get} {set} {byName} />
    {/if}
    <h3>{m[3] === 'part' ? 'Drives' : 'Drives (each note)'}</h3>
    <Routes {engine} {get} {set} {byName} {human} kind={m[3]} only={m[2]} />
  {/key}
</section>

<section class="card">
  <h2>All routes</h2>
  <Routes {engine} {get} {set} {byName} {human} kind="part" />
  <h3>Each note</h3>
  <Routes {engine} {get} {set} {byName} {human} kind="voice" />
</section>

{#if rest.length}
  <section class="card">
    <h2>Other</h2>
    {#each rest as d (d.id)}
      <Param {d} label={human(d.name)} value={values[d.id] ?? d.def} onchange={(v) => setValue(d, v)} />
    {/each}
  </section>
{/if}

<style>
  .dot { display: inline-block; width: 5px; height: 5px; margin-left: 6px; border-radius: 50%; background: var(--accent); vertical-align: 2px; }
</style>
