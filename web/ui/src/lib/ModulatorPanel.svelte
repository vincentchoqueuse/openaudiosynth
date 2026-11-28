<!-- ModulatorPanel.svelte — the Settings tab of the side panel for a source of modulation: its editor (MsegEditor
     for an envelope, LfoView for an LFO, the two settings of Wander; a controller has none), then what it moves, set
     here (RouteList), and a footer to add a route from it, to the whole sound and / or to each note. -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import Param from './Param.svelte';
  import RouteList from './RouteList.svelte';
  import Icon from './Icon.svelte';
  import Section from './Section.svelte';
  import { session as s } from './session.svelte.js';
  import * as R from './routes.js';
  let { m } = $props(); // a row of mods.js modRows()
  const add = (kind) => R.addFrom(s, kind, m.src);
</script>

{#if m.key === 'wander'}
  <Section icon="sliders" title="Settings">
    <div class="ptable"><Param d={s.byName.wander} label="Amount" /><Param d={s.byName.wander_rate} label="Rate" /></div>
  </Section>
{:else}
  {#if !m.ctl}<Section icon="shape" title="Shape">{#key m.key}{#if m.type === 'Envelope'}<MsegEditor prefix={m.prefix} />{:else}<LfoView prefix={m.prefix} />{/if}{/key}</Section>{/if}
  <Section icon="link" title="What it moves"><RouteList src={m.src} kinds={m.kinds} /></Section>
  <div class="sp-foot">
    {#each m.kinds as kind}
      <button class="btn ghost" onclick={() => add(kind)} disabled={!R.free(s, kind)}>
        <Icon name="plus" size={12} /> {m.kinds.length > 1 ? (kind === 'part' ? 'Route on the whole sound' : 'Route on each note') : 'Add a route'}
      </button>
    {/each}
  </div>
{/if}
