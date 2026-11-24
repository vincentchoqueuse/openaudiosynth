<!-- ParamGroups.svelte — the parameters of one editor as cards: the engine's (tab "engine"), the effect bus ("fx")
     or the part's settings ("settings": MIDI channel, keys, transposition, MPE, plus the global tempo and tuning no
     effect claims). One card per stage of the signal chain when the engine describes its stages, else by name
     prefix. The engine's cards all one width, in the order its signal flows (sources, filters, amp, then modulators
     and voice); the effects in the order of the bus; the cards fill a grid of equal columns that wraps; a
     stage that can be switched off has a switch, and folds to its title while off (switched off, it
     remembers its value to come back to). The effects show only those in use as cards: the others wait in a row
     of small buttons under them (Add effect), and one switched off by hand stays a folded card until another sound
     is loaded. The five effects of the bus form a rack: each shows its place and two arrows that move it earlier or
     later in the signal (session.setRack); one added from the row goes after those already in use. A stage that is an oscillator, a filter, an envelope or an LFO shows a small
     drawing of its settings (StageViz); an engine may name its controls itself and keep only the main ones in view,
     the others under a More button (engineui.js). The card the signal path asks for (a click on a block in Summary) is scrolled to and lit. -->
<script>
  import Card from './Card.svelte';
  import Switch from './Switch.svelte';
  import Param from './Param.svelte';
  import StageViz from './StageViz.svelte';
  import { session as s } from './session.svelte.js';
  import { MOD_PARAM, PART_PARAM } from './synth.js';
  import { grouped, byStage, unclaimed, human } from './naming.js';
  import { ENGINE_UI } from './engineui.js';
  let { tab } = $props(); // 'engine' | 'fx' | 'settings'

  const shared = $derived(s.shared.filter((d) => d.id < PART_PARAM));
  const groups = $derived.by(() => {
    if (!s.engine) return [];
    if (tab === 'engine')
      return s.engine.stages?.length ? byStage(s.engine.stages, s.engine.params) : grouped(s.engine.params).map(([g, ps]) => [g, ps, null]);
    if (tab === 'fx') return s.catalog?.fx ? byStage(s.fxStages, shared, false) : grouped(shared).map(([g, ps]) => [g, ps, null]);
    // settings: the part, the tempo, the tuning (the 12 custom offsets only with the custom temperament)
    const part = s.shared.filter((d) => d.id >= PART_PARAM && d.id < MOD_PARAM);
    const globals = s.catalog?.fx ? unclaimed(s.catalog.fx, shared) : [];
    const row = (d, label) => ({ d, label: label ?? human(d.name) });
    const custom = s.byName.tuning?.labels?.[Math.round(s.getP('tuning'))] === 'custom';
    const tuning = globals.filter((d) => /^(tuning|a4_hz|tune_\d+)/.test(d.name) && (custom || !/^tune_\d+$/.test(d.name)));
    // the tempo itself is set in the top bar (the clip's): here only how it is followed
    const clock = globals.filter((d) => !tuning.includes(d) && !/^(tune_\d+|fx_slot\d+)$/.test(d.name) && d.name !== 'tempo'); // the rack's places: set in Effects
    return [
      ['Part', part.map((d) => row(d, human(d.name.replace(/^part_/, '')))), null],
      ['Clock', clock.map((d) => row(d)), null],
      ['Tuning', tuning.map((d) => row(d, d.name === 'a4_hz' ? 'A4' : /^tune_\d+$/.test(d.name) ? `Degree ${+d.name.slice(5)}` : human(d.name.replace(/^tuning_?/, '') || 'temperament'))), null],
    ].filter(([, ps]) => ps.length);
  });

  // the engine's own presentation (engineui.js): names that depend on another setting, main controls and "More"
  const ui = $derived(tab === 'engine' ? ENGINE_UI[s.engine?.name] : undefined);
  const place = (d) => ui?.place?.(d, s) ?? 'main';
  let open = $state({}); // card title -> its "More" is unfolded
  $effect(() => { s.patchGen; open = {}; });

  let lastOn = {}; // value of a stage's switch before it was turned off (this sound only)
  $effect(() => { s.patchGen; lastOn = {}; });
  const stageOn = (st) => !st?.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
  // effects that have been on, or that a macro moves, since this sound was loaded: they stay cards (folded when
  // switched off, and never leaving while a slider is dragged down to 0); the others wait under Add effect
  let kept = $state({});
  $effect(() => { s.patchGen; kept = {}; });
  $effect(() => {
    if (tab !== 'fx') return;
    for (const [, ps, st] of groups)
      if (st?.enable && !kept[st.id] && (stageOn(st) || ps.some(({ d }) => s.macrosOf[d.id]))) kept[st.id] = true;
  });
  const idle = (st) => tab === 'fx' && !!st?.enable && !kept[st.id]; // an effect not in use
  // the rack: the bus effects whose order is free
  const inRack = (st) => tab === 'fx' && !!st && s.rack.includes(st.id);
  const shownRack = () => s.rack.filter((id) => !idle(s.fxStages.find((x) => x.id === id)));
  function rackPlace(id, other, after) { // id just before or after another effect of the rack
    const order = s.rack.filter((x) => x !== id);
    order.splice(order.indexOf(other) + (after ? 1 : 0), 0, id);
    s.setRack(order);
  }
  function move(st, dir) {
    const shown = shownRack(), other = shown[shown.indexOf(st.id) + dir];
    if (other) rackPlace(st.id, other, dir > 0);
  }
  function toggle(st) {
    if (inRack(st) && idle(st)) { const shown = shownRack(); if (shown.length) rackPlace(st.id, shown[shown.length - 1], true); }
    if (stageOn(st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
    else s.setP(st.enable, lastOn[st.enable] ?? st.on);
  }
  // the engine's cards in the order its signal flows: sources, filters, amp, modulators, voice, the rest
  const ordered = $derived.by(() => {
    if (tab !== 'engine' || !s.engine?.stages?.length) return groups;
    const rank = { source: 0, filter: 1, amp: 2, mod: 3, voice: 4 };
    return groups.map((g, i) => [g, i]).sort(([a, i], [b, j]) => (rank[a[2]?.kind] ?? 5) - (rank[b[2]?.kind] ?? 5) || i - j).map(([g]) => g);
  });
  // the stage the diagram asked for: scrolled into view, lit for a moment
  let host, lit = $state('');
  $effect(() => {
    const f = s.focus;
    if (!f || !host) return;
    const card = host.querySelector(`[data-stage="${CSS.escape(f.id)}"]`);
    if (!card) return;
    card.scrollIntoView({ behavior: 'smooth', block: 'center' });
    lit = f.id;
    const t = setTimeout(() => (lit = ''), 1200);
    return () => clearTimeout(t);
  });
</script>

{#snippet card(g, ps, st)}
  {@const on = stageOn(st)}
  <Card class="{on ? '' : 'off'} {st && lit === st.id ? 'lit' : ''}" data-stage={st?.id}>
    <div class="stage-head">
      <h2>{#if inRack(st)}<span class="place mono" title="place in the rack: the signal goes through the effects in this order">{shownRack().indexOf(st.id) + 1}</span>{/if}{g}</h2>
      {#if inRack(st)}
        {@const shown = shownRack()}
        <span class="rackmove">
          <button onclick={() => move(st, -1)} disabled={shown[0] === st.id} aria-label="{g}: earlier in the signal" title="earlier in the signal">‹</button>
          <button onclick={() => move(st, 1)} disabled={shown[shown.length - 1] === st.id} aria-label="{g}: later in the signal" title="later in the signal">›</button>
        </span>
      {/if}
      {#if st?.enable}
        <Switch {on} onchange={() => toggle(st)} name="{g} on/off" />
      {/if}
    </div>
    {#if on}
      {#if st}<StageViz {st} params={ps.map((p) => p.d)} get={s.getP} />{/if}
      {@const more = ps.filter(({ d }) => place(d) === 'more')}
      {#each ps.filter(({ d }) => place(d) === 'main') as { d, label } (d.id)}
        <Param {d} label={ui?.label?.(d, s) ?? label} />
      {/each}
      {#if more.length}
        <button class="more" aria-expanded={!!open[g]} onclick={() => (open[g] = !open[g])}>{open[g] ? 'Less' : `More (${more.length})`}</button>
        {#if open[g]}
          {#each more as { d, label } (d.id)}
            <Param {d} label={ui?.label?.(d, s) ?? label} />
          {/each}
        {/if}
      {/if}
    {/if}
  </Card>
{/snippet}

<div bind:this={host}>
  <div class="groups">
    {#each ordered.filter(([, , st]) => !idle(st)) as [g, ps, st] (g)}{@render card(g, ps, st)}{/each}
  </div>
  {#if ordered.some(([, , st]) => idle(st))}
    <div class="addrow"><span class="tag">Add effect</span>
      {#each ordered.filter(([, , st]) => idle(st)) as [g, , st] (g)}<button class="add" onclick={() => toggle(st)}>{g}</button>{/each}
    </div>
  {/if}
</div>
