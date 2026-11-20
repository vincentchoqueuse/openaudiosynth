<!-- ModPanel.svelte — the Modulation tab: a graphical editor for each multi-segment envelope (part MSEG 1 and 2,
     per-note MSEG) with the routes it drives, the full route list, then LFOs and macros as plain parameters. -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import Routes from './Routes.svelte';
  import Param from './Param.svelte';
  let { engine, shared, get, set, byName, human, values, setValue } = $props();
  const ENVS = [['mseg1_', 'MSEG 1', 'mseg1', 'part'], ['mseg2_', 'MSEG 2', 'mseg2', 'part'], ['vmseg_', 'Per-note MSEG', 'vmseg', 'voice']];
  let env = $state(0);
  const rest = $derived(shared.filter((d) => d.id >= 1024 && !/^(mseg\d|vmseg|route\d|vroute\d|map\d|macro\d)/.test(d.name)));
  const restGroups = $derived.by(() => {
    const out = new Map();
    for (const d of rest) {
      const g = d.name.startsWith('macro') ? 'Macros' : d.name.startsWith('mlfo1') ? 'LFO 1' : d.name.startsWith('mlfo2') ? 'LFO 2' : d.name.startsWith('vlfo') ? 'Per-note LFO' : 'Other';
      if (!out.has(g)) out.set(g, []);
      out.get(g).push({ d, label: human(d.name.replace(/^(mlfo\d|vlfo)_/, '')) });
    }
    return [...out];
  });
</script>

<section class="card">
  <div class="card-head">
    <h2>Envelopes</h2>
    <div class="seg small">
      {#each ENVS as [, name], i}<button class:on={env === i} onclick={() => (env = i)}>{name}</button>{/each}
    </div>
  </div>
  {#key env}
    <MsegEditor prefix={ENVS[env][0]} {get} {set} />
    <h3>{ENVS[env][3] === 'part' ? 'Drives' : 'Drives (per note)'}</h3>
    <Routes {engine} {get} {set} {byName} {human} kind={ENVS[env][3]} only={ENVS[env][2]} />
  {/key}
</section>

<section class="card">
  <h2>All routes</h2>
  <Routes {engine} {get} {set} {byName} {human} kind="part" />
  <h3>Per note</h3>
  <Routes {engine} {get} {set} {byName} {human} kind="voice" />
</section>

<div class="groups">
  {#each restGroups as [g, ps] (g)}
    <section>
      <h2>{g}</h2>
      {#each ps as { d, label } (d.id)}
        <Param {d} {label} value={values[d.id] ?? d.def} onchange={(v) => setValue(d, v)} />
      {/each}
    </section>
  {/each}
</div>
