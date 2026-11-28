<!-- GraphView.svelte — the Synth view's Graph, in levels: first the sound's four parts along the signal (Event, Engine,
     Transform, Output: graphmods.js), each a card that names its blocks and shows, as pictograms, the kinds of
     modulation acting in it; a click enters the part, drawn as its blocks in the order of the signal (ChainView),
     each with the pictograms of what moves it. A click on a block opens its settings in the side panel, a click on a
     pictogram opens that modulator; the strip of parts on top moves between them, Sound goes back. -->
<script>
  import ChainView from './ChainView.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { PARTS, partOf, modsByStage, MOD_ICON } from './graphmods.js';
  let { onpick } = $props();

  const isOn = (st) => !st.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
  const stages = $derived([...(s.engine?.stages ?? []), ...(s.fxStages ?? [])]);
  const badges = $derived(modsByStage(s));
  // each part: its stages (those on), and the kinds of modulation acting in it
  const parts = $derived(PARTS.map((p) => {
    const ids = stages.filter((st) => partOf(st) === p.id).map((st) => st.id);
    const on = stages.filter((st) => ids.includes(st.id) && isOn(st));
    const kinds = [...new Set(ids.flatMap((id) => (badges[id] ?? []).map((b) => b.kind)))];
    return { ...p, ids, on, kinds, count: ids.reduce((n, id) => n + (badges[id]?.length ?? 0), 0) };
  }));
  const current = $derived(parts.find((p) => p.id === s.graphPart) ?? null);
  const KIND = { env: 'envelopes', lfo: 'LFOs', macro: 'macros', ctl: 'controllers' };
</script>

<nav class="levels" aria-label="Parts of the sound">
  <button class="crumb" class:on={!current} onclick={() => (s.graphPart = '')}>Sound</button>
  {#each parts as p, i (p.id)}
    {#if i}<span class="arrow" aria-hidden="true">→</span>{/if}
    <button class="part" class:on={current?.id === p.id} onclick={() => (s.graphPart = p.id)} title="{p.label}: {p.about}">
      <span class="picon"><Icon name={p.icon} size={13} /></span>{p.label}
      {#each p.kinds as k}<span class="pk {k}" title={KIND[k]}><Icon name={MOD_ICON[k]} size={11} /></span>{/each}
    </button>
  {/each}
</nav>

{#if !current}
  <div class="cards">
    {#each parts as p, i (p.id)}
      {#if i}<span class="bigarrow" aria-hidden="true">→</span>{/if}
      <button class="card" onclick={() => (s.graphPart = p.id)} title="Enter {p.label}">
        <span class="chead"><span class="picon lg"><Icon name={p.icon} size={16} /></span><strong>{p.label}</strong></span>
        <span class="about">{p.about}</span>
        <span class="blocks">{p.on.length ? p.on.map((st) => st.label).join(' · ') : p.id === 'event' ? 'notes in' : 'nothing on'}</span>
        <span class="mods">
          {#if p.count}{#each p.kinds as k}<span class="pk {k}"><Icon name={MOD_ICON[k]} size={11} /> {KIND[k]}</span>{/each}
          {:else}<span class="muted">not modulated</span>{/if}
        </span>
      </button>
    {/each}
  </div>
  <p class="hint">Click a part to enter it; in a part, a block opens its settings, a pictogram the modulator acting on it.</p>
{:else}
  {#if current.on.length}<ChainView {onpick} only={current.ids} {badges} />
  {:else}<p class="empty muted">{current.id === 'event' ? 'This instrument has no voice stage: the notes go straight to the engine.' : 'Nothing is on in this part.'}</p>{/if}
  <p class="hint">{current.about}. A block opens its settings; a pictogram (envelope, LFO, macro) opens what moves it.</p>
{/if}

<style>
  .levels { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; margin: 4px 0 12px; }
  .crumb, .part { box-sizing: border-box; display: inline-flex; align-items: center; gap: 6px; height: 30px; padding: 0 10px; border: 1px solid var(--line);
    border-radius: 7px; background: var(--surface); font-size: 12px; color: var(--text); }
  .crumb.on, .part.on { border-color: var(--accent); box-shadow: 0 0 0 1px var(--accent); }
  .crumb:hover, .part:hover { border-color: var(--accent); }
  .arrow, .bigarrow { color: var(--muted); }
  .bigarrow { align-self: center; font-size: 16px; }
  .picon { display: inline-grid; place-items: center; width: 20px; height: 20px; border-radius: 5px; background: var(--accent-soft); color: var(--accent); }
  .picon.lg { width: 28px; height: 28px; border-radius: 7px; }
  .pk { display: inline-flex; align-items: center; gap: 4px; color: var(--accent); }
  .pk.macro { color: var(--warn); }
  .cards { display: flex; align-items: stretch; gap: 10px; flex-wrap: wrap; }
  .card { flex: 1 1 170px; display: grid; align-content: start; gap: 8px; padding: 14px; text-align: left; border: 1px solid var(--line); border-radius: var(--radius);
    background: var(--surface); color: var(--text); font-size: 12px; min-width: 0; }
  .card:hover { border-color: var(--accent); }
  .chead { display: flex; align-items: center; gap: 8px; font-size: 13px; }
  .about { color: var(--muted); }
  .blocks { line-height: 1.5; }
  .mods { display: flex; flex-wrap: wrap; gap: 4px 10px; font-size: 11.5px; }
  .empty { padding: 20px 0; }
</style>
