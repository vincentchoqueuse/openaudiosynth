<!-- MacroStrip.svelte — the macros to play, above the keyboard: the part's 8 macros as rows shaped like every other
     parameter (name, slider, value in percent; double-click: back to the sound's value), 2 rows of 4 on a wide
     screen; the MIDI CC and the parameters each moves are in its tooltip. They are set up (names, CC, targets) in the Synth tab (MacroMatrix); a macro's
     CC turns it in the synth. -->
<script>
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
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
        </span>
        <input type="range" min="0" max="1" step="0.001" value={m} style="--p:{m * 100}%" aria-label={s.macroName(k)}
               oninput={(e) => s.setP(`macro${k + 1}`, +e.target.value)} ondblclick={() => s.setP(`macro${k + 1}`, base(k))} />
        <span class="val">{Math.round(m * 100)} %</span>
      </div>
    </div>
  {/each}
</div>

<style>
  .macrogrid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 0 26px; margin-bottom: 8px; }
  .macrogrid .param { grid-template-columns: 7.5em 1fr 3.2em; min-height: 26px; gap: 8px; }
  .mrow { min-width: 0; }
  .mrow.empty { opacity: .5; }
  @media (max-width: 1100px) { .macrogrid { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
  @media (max-width: 760px) { .macrogrid { gap: 0 14px; } .macrogrid .param { grid-template-columns: 5.5em 1fr; } .macrogrid .val { display: none; } }
</style>
