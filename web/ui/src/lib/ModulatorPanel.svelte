<!-- ModulatorPanel.svelte — the Settings tab of the side panel for a source of modulation: its editor (MsegEditor
     for an envelope, LfoView for an LFO, the two settings of Wander; a controller has none), then Moves: what it
     moves, set here (RouteList: its routes on the whole sound and / or on each note, and for a controller the slots
     of the instrument's matrix it feeds), with "+ Add" under it. -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import Param from './Param.svelte';
  import RouteList from './RouteList.svelte';
  import Section from './Section.svelte';
  import { session as s } from './session.svelte.js';
  import * as M from './matrix.js';
  let { m } = $props(); // a row of mods.js modRows()
  const msrc = $derived(m.ctl ? M.srcOfCtl(s, m.src) : null);
</script>

{#if m.key === 'wander'}
  <Section icon="sliders" title="Settings">
    <div class="ptable"><Param d={s.byName.wander} label="Amount" /><Param d={s.byName.wander_rate} label="Rate" /></div>
  </Section>
{:else}
  {#if !m.ctl}<Section icon="shape" title="Shape">{#key m.key}{#if m.type === 'Envelope'}<MsegEditor prefix={m.prefix} />{:else}<LfoView prefix={m.prefix} />{/if}{/key}</Section>{/if}
  <Section icon="link" title="Moves"><RouteList src={m.src} kinds={m.kinds} {msrc} /></Section>
{/if}
