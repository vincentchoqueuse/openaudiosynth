<!-- MacroStrip.svelte — the macros to play, above the keyboard: the part's 8 macros as rows shaped like every other
     parameter (name, slider, value in percent; double-click: back to the sound's value; Shift and the arrows as on every slider), 2 rows of 4 on a wide
     screen; under each name, what it moves (its MIDI CC is in the tooltip). They are set up (names, CC, targets) in the Synth view (Macros: MacroTable, MacroPanel); a macro's
     CC turns it in the synth. -->
<script>
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import { precise } from './slider.js';
  // what each macro moves, by name
  const moves = $derived(Array.from({ length: 8 }, (_, k) => s.macroMaps.filter((m) => m.macro === k && m.dst).map((m) => human(m.dst.name))));
  const base = (k) => s.baseline[s.byName[`macro${k + 1}`]?.id] ?? 0;
  const cc = (k) => Math.round(s.getP(`macro${k + 1}_cc`));
</script>

<div class="macrogrid">
  {#each moves as targets, k}
    {@const m = s.getP(`macro${k + 1}`)}
    <div class="mrow" class:empty={!s.macroNames[k] && !targets.length}
         title="{cc(k) ? `CC ${cc(k)}` : 'no CC'} · {targets.length ? targets.join(', ') : 'nothing assigned'}">
      <div class="param" class:changed={Math.abs(m - base(k)) > 1e-3}>
        <span class="label" title="double-click: back to the sound's value" ondblclick={() => s.setP(`macro${k + 1}`, base(k))} role="presentation">
          {s.macroName(k)}
          {#if targets.length && !(targets.length === 1 && targets[0].toLowerCase() === s.macroName(k).toLowerCase())}<small>{targets.length > 2 ? `${targets.length} settings` : targets.join(', ')}</small>{/if}
        </span>
        <input type="range" min="0" max="1" step="0.001" value={m} style="--p:{m * 100}%" aria-label={s.macroName(k)}
               use:precise={{ value: () => s.getP(`macro${k + 1}`), set: (v) => s.setP(`macro${k + 1}`, v) }} ondblclick={() => s.setP(`macro${k + 1}`, base(k))} />
        <span class="val">{Math.round(m * 100)} %</span>
      </div>
    </div>
  {/each}
</div>

<style>
  .macrogrid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 0 26px; }
  .macrogrid .param { grid-template-columns: 7.5em 1fr 3.2em; min-height: 26px; gap: 8px; }
  .mrow { min-width: 0; }
  .mrow.empty { opacity: .5; }
  .macrogrid .label { display: flex; flex-direction: column; line-height: 1.2; }
  .macrogrid .label small { font-size: 10px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; }
  .macrogrid .param { min-height: 32px; }
  /* a tablet keeps the four columns (the percentages go); a phone has two */
  @media (max-width: 1180px) { .macrogrid { gap: 0 16px; } .macrogrid .param { grid-template-columns: 6em 1fr; } .macrogrid .val { display: none; } }
  @media (max-width: 700px) { .macrogrid { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 0 14px; } .macrogrid .param { grid-template-columns: 5.5em 1fr; } }
</style>
