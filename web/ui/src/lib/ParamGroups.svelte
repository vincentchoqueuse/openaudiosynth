<!-- ParamGroups.svelte — the parameters of one editor as cards: the engine's (tab "engine"), the effect bus ("fx")
     or the part's settings ("settings": MIDI channel, keys, transposition, MPE, plus the global tempo and tuning no
     effect claims). One card per stage of the signal chain when the engine describes its stages, else by name
     prefix; a stage that can be switched off has a switch, and folds to its title while off (switched off, it
     remembers its value to come back to). The card the Structure diagram asks for is scrolled to and lit. -->
<script>
  import Param from './Param.svelte';
  import { session as s } from './session.svelte.js';
  import { MOD_PARAM, PART_PARAM } from './synth.js';
  import { grouped, byStage, unclaimed, human } from './naming.js';
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
    const tempo = globals.filter((d) => !tuning.includes(d) && !/^tune_\d+$/.test(d.name));
    return [
      ['Part', part.map((d) => row(d, human(d.name.replace(/^part_/, '')))), null],
      ['Tempo', tempo.map((d) => row(d)), null],
      ['Tuning', tuning.map((d) => row(d, d.name === 'a4_hz' ? 'A4' : /^tune_\d+$/.test(d.name) ? `Degree ${+d.name.slice(5)}` : human(d.name.replace(/^tuning_?/, '') || 'temperament'))), null],
    ].filter(([, ps]) => ps.length);
  });

  let lastOn = {}; // value of a stage's switch before it was turned off (this sound only)
  $effect(() => { s.patchGen; lastOn = {}; });
  const stageOn = (st) => !st?.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
  function toggle(st) {
    if (stageOn(st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
    else s.setP(st.enable, lastOn[st.enable] ?? st.on);
  }
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

<div class="groups" bind:this={host}>
  {#each groups as [g, ps, st] (g)}
    {@const on = stageOn(st)}
    <section class="card" class:off={!on} class:lit={st && lit === st.id} data-stage={st?.id}>
      <div class="stage-head">
        <h2>{g}</h2>
        {#if st?.enable}
          <button class="onoff" class:on onclick={() => toggle(st)} role="switch" aria-checked={on} aria-label="{g} on/off" title={on ? 'on' : 'off'}></button>
        {/if}
      </div>
      {#if on}
        {#each ps as { d, label } (d.id)}
          <Param {d} {label} value={s.values[d.id] ?? d.def} base={s.baseline[d.id] ?? d.def} onchange={(v) => s.setValue(d, v)} />
        {/each}
      {/if}
    </section>
  {/each}
</div>
