<!-- ModPanel.svelte — the Modulation pill. Its tabs are chosen in SynthTab: All assignments first (the two tables,
     of the whole sound and of each note; ModMatrix), then one tab per modulator (lib/mods.js: the two global
     envelopes and the per-note one, the two global LFOs and the per-note one), each shown the same way, MsegEditor
     or LfoView: a bar of choices, its drawing, a bar of settings; and under it what it moves: its own assignments,
     and Add. -->
<script>
  import Card from './Card.svelte';
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import ModMatrix from './ModMatrix.svelte';
  import Param from './Param.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import { MODS } from './mods.js';
  let { view } = $props(); // 'assign', or the index of a modulator in MODS
  const m = $derived(MODS[view] ?? MODS[0]);
  // part modulation parameters no editor shows (none today; kept so a new one is never hidden)
  const rest = $derived(s.shared.filter((d) => d.id >= 1024 && !/^(mseg\d|vmseg|route\d|vroute\d|map\d|macro\d|mlfo\d|vlfo|wander)/.test(d.name)));
</script>

{#if view !== 'assign'}
  <Card>
    {#key view}
      {#if m[4] === 'env'}
        <MsegEditor prefix={m[1]} />
      {:else}
        <LfoView prefix={m[1]} />
      {/if}
    {/key}
  </Card>
  <h3 class="sub">What {m[0]} moves</h3>
  <Card>
    {#key view}<ModMatrix kind={m[3]} only={m[2]} />{/key}
  </Card>
{:else}
  <h3 class="sub">On the whole sound</h3>
  <Card><ModMatrix kind="part" /></Card>
  <h3 class="sub">On each note</h3>
  <Card><ModMatrix kind="voice" /></Card>
{/if}

{#if rest.length}
  <Card title="Other">
    {#each rest as d (d.id)}
      <Param {d} label={human(d.name)} />
    {/each}
  </Card>
{/if}
