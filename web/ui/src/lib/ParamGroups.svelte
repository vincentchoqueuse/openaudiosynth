<!-- ParamGroups.svelte — the parameters of one editor tab as cards: the engine's (Sound), the effect bus (Effects)
     or the part's (Part), one card per stage of the signal chain when the engine describes its stages, with the
     stage's on / off switch (off remembers the value it had, to come back to it), else by name prefix. -->
<script>
  import Param from './Param.svelte';
  import { session } from './session.svelte.js';
  import { MOD_PARAM, PART_PARAM } from './synth.js';
  import { grouped, byStage } from './naming.js';
  let { tab } = $props(); // 'sound' | 'fx' | 'part'

  const s = session;
  const params = $derived(
    !s.engine ? [] :
    tab === 'sound' ? s.engine.params :
    tab === 'fx' ? s.shared.filter((d) => d.id < PART_PARAM) :
    s.shared.filter((d) => d.id >= PART_PARAM && d.id < MOD_PARAM));
  const groups = $derived(
    tab === 'sound' && s.engine?.stages?.length ? byStage(s.engine.stages, s.engine.params) :
    tab === 'fx' && s.catalog?.fx ? byStage(s.catalog.fx, params) :
    grouped(params).map(([g, ps]) => [g, ps, null]));

  let lastOn = {}; // value of a stage's switch before it was turned off (this sound only)
  $effect(() => { s.patchGen; lastOn = {}; });
  const stageOn = (st) => !st?.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
  function toggle(st) {
    if (stageOn(st)) { lastOn[st.enable] = s.getP(st.enable); s.setP(st.enable, st.off); }
    else s.setP(st.enable, lastOn[st.enable] ?? st.on);
  }
</script>

<div class="groups">
  {#each groups as [g, ps, st] (g)}
    <section class="card" class:off={!stageOn(st)}>
      <div class="stage-head">
        <h2>{g}</h2>
        {#if st?.enable}
          <button class="onoff" class:on={stageOn(st)} onclick={() => toggle(st)} role="switch"
                  aria-checked={stageOn(st)} aria-label="{g} on/off" title={stageOn(st) ? 'on' : 'off'}></button>
        {/if}
      </div>
      {#each ps as { d, label } (d.id)}
        <Param {d} {label} value={s.values[d.id] ?? d.def} onchange={(v) => s.setValue(d, v)} />
      {/each}
    </section>
  {/each}
</div>
