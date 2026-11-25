<!-- SettingsDialog.svelte — the part's settings, apart from the sound's design: MIDI channel, key range,
     transposition, level and pan in the mix, MPE, and the global clock (the tempo itself is in the top bar) and tuning
     (temperament, concert pitch; the twelve custom offsets only with the custom temperament). Opened from the gear in
     the sidebar; Escape or a click outside closes it. -->
<script>
  import Param from './Param.svelte';
  import { session as s } from './session.svelte.js';
  import { MOD_PARAM, PART_PARAM } from './synth.js';
  import { unclaimed, human } from './naming.js';

  const groups = $derived.by(() => {
    const part = s.shared.filter((d) => d.id >= PART_PARAM && d.id < MOD_PARAM);
    const globals = s.catalog?.fx ? unclaimed(s.catalog.fx, s.shared.filter((d) => d.id < PART_PARAM)) : [];
    const custom = s.byName.tuning?.labels?.[Math.round(s.getP('tuning'))] === 'custom';
    const tuning = globals.filter((d) => /^(tuning|a4_hz|tune_\d+)/.test(d.name) && (custom || !/^tune_\d+$/.test(d.name)));
    const clock = globals.filter((d) => !tuning.includes(d) && !/^(tune_\d+|fx_slot\d+)$/.test(d.name) && d.name !== 'tempo');
    return [
      ['Part', part.map((d) => [d, human(d.name.replace(/^part_/, ''))])],
      ['Clock', clock.map((d) => [d, human(d.name)])],
      ['Tuning', tuning.map((d) => [d, d.name === 'a4_hz' ? 'A4' : /^tune_\d+$/.test(d.name) ? `Degree ${+d.name.slice(5)}` : human(d.name.replace(/^tuning_?/, '') || 'temperament')])],
    ].filter(([, ps]) => ps.length);
  });
</script>

<div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.settingsOpen = false)}>
  <div class="dialog wide" role="dialog" aria-modal="true" aria-label="Settings">
    <div class="dhead"><h2>Settings</h2><button class="btn ghost" onclick={() => (s.settingsOpen = false)}>Close</button></div>
    <div class="cols">
      {#each groups as [title, ps] (title)}
        <section><h3 class="sub">{title}</h3>{#each ps as [d, label] (d.id)}<Param {d} {label} />{/each}</section>
      {/each}
    </div>
  </div>
</div>

<style>
  .wide { width: min(960px, 94vw); max-height: 86vh; overflow-y: auto; }
  .dhead { display: flex; align-items: center; justify-content: space-between; }
  .dhead h2 { margin: 0; }
  .cols { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 8px 28px; align-items: start; }
</style>
