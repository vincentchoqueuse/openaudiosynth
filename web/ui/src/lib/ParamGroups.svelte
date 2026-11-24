<!-- ParamGroups.svelte — the parameters of one editor as cards: the engine's (tab "engine"), the effect bus ("fx")
     or the part's settings ("settings": MIDI channel, keys, transposition, MPE, plus the global tempo and tuning no
     effect claims). One card per stage of the signal chain when the engine describes its stages, else by name
     prefix. The engine's cards all one width, in the order its signal flows (sources, filters, amp, then modulators
     and voice); the effects in the order of the bus; every card one width, on one row that scrolls sideways; a
     stage that can be switched off has a switch, and folds to its title while off (switched off, it
     remembers its value to come back to). A stage that is an oscillator, a filter, an envelope or an LFO shows a small
     drawing of its settings (StageViz); an engine may name its controls itself and keep only the main ones in view,
     the others under a More button (engineui.js); an FM matrix (a stage of kind "matrix") is drawn as one (FmMatrix). The card the Structure diagram asks for is scrolled to and lit. -->
<script>
  import Card from './Card.svelte';
  import Switch from './Switch.svelte';
  import Param from './Param.svelte';
  import StageViz from './StageViz.svelte';
  import FmMatrix from './FmMatrix.svelte';
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
    if (tab === 'fx') return s.catalog?.fx ? byStage(s.catalog.fx, shared, false) : grouped(shared).map(([g, ps]) => [g, ps, null]);
    // settings: the part, the tempo, the tuning (the 12 custom offsets only with the custom temperament)
    const part = s.shared.filter((d) => d.id >= PART_PARAM && d.id < MOD_PARAM);
    const globals = s.catalog?.fx ? unclaimed(s.catalog.fx, shared) : [];
    const row = (d, label) => ({ d, label: label ?? human(d.name) });
    const custom = s.byName.tuning?.labels?.[Math.round(s.getP('tuning'))] === 'custom';
    const tuning = globals.filter((d) => /^(tuning|a4_hz|tune_\d+)/.test(d.name) && (custom || !/^tune_\d+$/.test(d.name)));
    // the tempo itself is set in the top bar (the clip's): here only how it is followed
    const clock = globals.filter((d) => !tuning.includes(d) && !/^tune_\d+$/.test(d.name) && d.name !== 'tempo');
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
  function toggle(st) {
    if (stageOn(st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
    else s.setP(st.enable, lastOn[st.enable] ?? st.on);
  }
  // the engine with stages, laid out as its signal: one column per kind of audio stage (sources, filters, amps),
  // the engine's cards in the order its signal flows: sources, filters, amp, modulators, voice, the rest
  const ordered = $derived.by(() => {
    if (tab !== 'engine' || !s.engine?.stages?.length) return groups;
    const rank = { matrix: -1, source: 0, filter: 1, amp: 2, mod: 3, voice: 4 };
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
  <Card class="{on ? '' : 'off'} {st && lit === st.id ? 'lit' : ''} {st?.kind === 'matrix' ? 'wide' : ''}" data-stage={st?.id}>
    <div class="stage-head">
      <h2>{g}</h2>
      {#if st?.enable}
        <Switch {on} onchange={() => toggle(st)} name="{g} on/off" />
      {/if}
    </div>
    {#if on && st?.kind === 'matrix'}
      <FmMatrix params={ps.map((p) => p.d)} />
    {:else if on}
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
    {#each ordered as [g, ps, st] (g)}{@render card(g, ps, st)}{/each}
  </div>
</div>
