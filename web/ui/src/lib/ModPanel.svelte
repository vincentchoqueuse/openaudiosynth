<!-- ModPanel.svelte — the Modulation pill. Its tabs are chosen in SynthTab: Assignments first (the matrix of the
     part or of each note: the routes in use, and Add route; ModMatrix), then one tab per modulator (lib/mods.js: the two
     envelopes and the per-note one, the two LFOs and the per-note one), each shown the same way, MsegEditor or
     LfoView: a bar of choices, its drawing, a bar of settings. -->
<script>
  import Card from './Card.svelte';
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import ModMatrix from './ModMatrix.svelte';
  import Param from './Param.svelte';
  import Seg from './Seg.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';
  import { MODS } from './mods.js';
  let { view } = $props(); // 'assign', or the index of a modulator in MODS
  let scope = $state('part'); // the routes of the part or of each note
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
{:else}
  <div class="picker"><Seg items={[{ id: 'part', label: 'Part' }, { id: 'voice', label: 'Each note' }]} value={scope} onpick={(v) => (scope = v)} label="Routes of" /></div>
  <Card>
    {#key scope}<ModMatrix kind={scope} />{/key}
  </Card>
{/if}

{#if rest.length}
  <Card title="Other">
    {#each rest as d (d.id)}
      <Param {d} label={human(d.name)} />
    {/each}
  </Card>
{/if}
