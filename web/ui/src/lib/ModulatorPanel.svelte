<!-- ModulatorPanel.svelte — the side panel's body for a modulator: its editor (MsegEditor for an envelope, LfoView
     for an LFO, the two settings of Wander), then what it moves: its routes (a click opens one), and Add a route
     from it, which opens the new route in the panel. -->
<script>
  import MsegEditor from './MsegEditor.svelte';
  import LfoView from './LfoView.svelte';
  import Param from './Param.svelte';
  import { session as s } from './session.svelte.js';
  import * as R from './routes.js';
  let { m } = $props(); // {key, label, prefix, src, kind, type}

  const routes = $derived(R.keys(m.kind).map((k) => R.get(s, k)).filter((x) => x.src && x.srcLabel === m.src));
  function add() { const k = R.addFrom(s, m.kind, m.src); if (k) s.openPanel('route', k); }
</script>

{#if m.key === 'wander'}
  <Param d={s.byName.wander} label="Amount" />
  <Param d={s.byName.wander_rate} label="Rate" />
  <p class="hint">Wander moves every macro slowly on its own random walk: the sound drifts through what its macros reach.</p>
{:else}
  {#key m.key}{#if m.type === 'Envelope'}<MsegEditor prefix={m.prefix} />{:else}<LfoView prefix={m.prefix} />{/if}{/key}
  <h3 class="sub">What it moves</h3>
  {#each routes as x (x.key)}
    <button class="sp-link" onclick={() => s.openPanel('route', x.key)}>
      <span>{R.dstName(s, x)}</span><span class="mono muted">{R.inUnits(s, x)}</span>
    </button>
  {:else}<p class="hint">Nothing yet.</p>{/each}
  <button class="btn ghost" onclick={add} disabled={!R.free(s, m.kind)}>+ Route from {m.label}</button>
{/if}
