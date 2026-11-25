<!-- ParamGroups.svelte — the parameters of one editor as cards: the engine's (tab "engine"), the effect bus ("fx")
     or the part's settings ("settings": MIDI channel, keys, transposition, MPE, plus the global tempo and tuning no
     effect claims). One card per stage of the signal chain when the engine describes its stages, else by name
     prefix. The engine's cards all one width, in the order its signal flows (sources, filters, amp, then modulators
     and voice); the effects in the order of the bus; the cards fill a grid of equal columns that wraps; a
     stage that can be switched off has a switch, and folds to its title while off (switched off, it
     remembers its value to come back to). The effects page reads as the signal: Input (output filter, crush), six
     blocks, Output (master, saturation, limiter). Each block holds one effect type, picked from the list in its head
     (Empty takes it out and switches it off; an effect another block holds swaps places with this one), and shows its
     settings; the blocks' order is the rack's (session.setRack). A stage that is an oscillator, a filter, an envelope or an LFO shows a small
     drawing of its settings (StageViz); an engine may name its controls itself and keep only the main ones in view,
     the others under a More button (engineui.js). The card the signal path asks for (a click on a block in Summary) is scrolled to and lit. -->
<script>
  import Card from './Card.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
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
  function toggle(st) {
    if (stageOn(st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
    else s.setP(st.enable, lastOn[st.enable] ?? st.on);
  }

  // ---- the rack: six blocks in the order of the signal, each holding one effect type or none. An effect is in a
  // block when it is on, or a macro moves it, or it was loaded since this sound came (so a mix dragged to 0 does not
  // empty its block); "Empty" switches it off and takes it out. Loading an effect another block holds swaps the two.
  const inRack = (st) => tab === 'fx' && !!st && s.rack.includes(st.id);
  let loaded = $state({}), emptied = $state({});
  $effect(() => { s.patchGen; loaded = {}; emptied = {}; });
  $effect(() => {
    if (tab !== 'fx') return;
    for (const [, ps, st] of groups) {
      if (!inRack(st) || loaded[st.id]) continue;
      if (stageOn(st)) { loaded[st.id] = true; delete emptied[st.id]; }
      else if (!emptied[st.id] && ps.some(({ d }) => s.macrosOf[d.id])) loaded[st.id] = true;
    }
  });
  const slots = $derived.by(() => {
    const used = s.rack.filter((id) => loaded[id]);
    return Array.from({ length: s.rackUnits.length }, (_, k) => used[k] ?? null);
  });
  const fxStage = (id) => s.fxStages.find((x) => x.id === id);
  // the list of block k: Empty, then each effect (one another block holds says which)
  const slotOptions = (k) => [{ value: '', label: 'Empty' },
    ...s.rack.map((id) => { const at = slots.indexOf(id); return { value: id, label: fxStage(id)?.label + (at >= 0 && at !== k ? ` (block ${at + 1})` : '') }; })];
  function load(k, id) {
    const A = [...slots], old = A[k];
    if (old === id) return;
    const j = id ? A.indexOf(id) : -1;
    if (j >= 0) A[j] = old; // the effect leaves its block for this one: the two swap
    A[k] = id || null;
    const used = A.filter(Boolean);
    s.setRack([...used, ...s.rack.filter((x) => !used.includes(x))]);
    if (old && !used.includes(old)) { // the effect that was here goes out: switched off
      const st = fxStage(old);
      if (stageOn(st)) toggle(st);
      delete loaded[old];
      emptied[old] = true;
    }
    if (id) {
      const st = fxStage(id);
      loaded[id] = true;
      delete emptied[id];
      if (!stageOn(st)) toggle(st);
    }
  }
  // the fixed stages before the rack (output filter, crush) and after it (master, saturation, limiter)
  const firstRack = $derived(groups.findIndex(([, , st]) => inRack(st)));
  const before = $derived(tab === 'fx' && firstRack >= 0 ? groups.filter(([, , st], i) => i < firstRack && !inRack(st)) : []);
  const after = $derived(tab === 'fx' && firstRack >= 0 ? groups.filter(([, , st], i) => i > firstRack && !inRack(st)) : []);
  const rackGroup = (id) => groups.find(([, , st]) => st?.id === id);
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
  <Card class="{on || inRack(st) ? '' : 'off'} {st && lit === st.id ? 'lit' : ''}" data-stage={st?.id}>
    <div class="stage-head">
      {#if inRack(st)}
        <span class="place mono" title="place in the signal: the effects go one after the other, from block 1">{slots.indexOf(st.id) + 1}</span>
        <Select class="fxpick" value={st.id} options={slotOptions(slots.indexOf(st.id))} label="Block {slots.indexOf(st.id) + 1}" onchange={(v) => load(slots.indexOf(st.id), v)} />
      {:else}
        <h2>{g}</h2>
        {#if st?.enable}<Switch {on} onchange={() => toggle(st)} name="{g} on/off" />{/if}
      {/if}
    </div>
    {#if on || inRack(st)}
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
  {#if tab === 'fx' && firstRack >= 0}
    {#if before.length}<h3 class="fxsec">Input</h3><div class="groups">{#each before as [g, ps, st] (g)}{@render card(g, ps, st)}{/each}</div>{/if}
    <h3 class="fxsec">Effects</h3>
    <div class="groups">
      {#each slots as id, k (id ?? 'empty' + k)}
        {#if id}
          {@const [g, ps, st] = rackGroup(id)}
          {@render card(g, ps, st)}
        {:else}
          <Card class="emptyblock">
            <div class="stage-head">
              <span class="place mono">{k + 1}</span>
              <Select class="fxpick" value="" options={slotOptions(k)} label="Block {k + 1}" onchange={(v) => load(k, v)} />
            </div>
          </Card>
        {/if}
      {/each}
    </div>
    {#if after.length}<h3 class="fxsec">Output</h3><div class="groups">{#each after as [g, ps, st] (g)}{@render card(g, ps, st)}{/each}</div>{/if}
  {:else}
    <div class="groups">
      {#each ordered as [g, ps, st] (g)}{@render card(g, ps, st)}{/each}
    </div>
  {/if}
</div>
