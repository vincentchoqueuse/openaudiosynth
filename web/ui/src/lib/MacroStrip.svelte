<!-- MacroStrip.svelte — the macros to play, above the keyboard: the part's 8 macros on one line, each its name, its value and
     a slider and Edit (the cog: the macro in the Synth view's side panel) (double-click: back to the sound's value; Shift and the arrows as on every slider; two lines of 4 on a narrow
     screen); what it moves and its MIDI CC are in the tooltip. They are set up (names, CC, targets) in the Synth view (Macros: MacroTable, MacroPanel); a macro's
     CC turns it in the synth. -->
<script>
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import { precise } from './slider.js';
  import Icon from './Icon.svelte';
  let { onedit = null } = $props(); // Edit: the macro in the Synth view's side panel
  // what each macro moves, by name
  const moves = $derived(Array.from({ length: 8 }, (_, k) => s.macroMaps.filter((m) => m.macro === k && m.dst).map((m) => human(m.dst.name))));
  const base = (k) => s.baseline[s.byName[`macro${k + 1}`]?.id] ?? 0;
  const cc = (k) => Math.round(s.getP(`macro${k + 1}_cc`));
</script>

<div class="macrogrid">
  {#each moves as targets, k}
    {@const m = s.getP(`macro${k + 1}`)}
    <label class="mcell" class:empty={!s.macroNames[k] && !targets.length} class:changed={Math.abs(m - base(k)) > 1e-3}
           title="{s.macroName(k)} · {Math.round(m * 100)} % · {cc(k) ? `CC ${cc(k)}` : 'no CC'} · {targets.length ? targets.join(', ') : 'nothing assigned'}">
      <span class="mname"><span>{s.macroName(k)}</span><span class="mright"><span class="mval mono">{Math.round(m * 100)}</span>{#if onedit}<button class="medit" onclick={(e) => { e.preventDefault(); onedit(k); }}
        aria-label="Edit {s.macroName(k)}" title="Edit the macro: its name, CC and what it moves"><Icon name="settings" size={11} /></button>{/if}</span></span>
      <input type="range" min="0" max="1" step="0.001" value={m} style="--p:{m * 100}%" aria-label={s.macroName(k)}
             use:precise={{ value: () => s.getP(`macro${k + 1}`), set: (v) => s.setP(`macro${k + 1}`, v) }} ondblclick={() => s.setP(`macro${k + 1}`, base(k))} />
    </label>
  {/each}
</div>

<style>
  .macrogrid { display: grid; grid-template-columns: repeat(8, minmax(0, 1fr)); gap: 0 18px; }
  .mcell { display: grid; gap: 2px; min-width: 0; margin: 0; }
  .mcell.empty { opacity: .45; }
  .mname { display: flex; justify-content: space-between; gap: 6px; font-size: 11.5px; color: var(--text); }
  .mname span:first-child { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .mcell.changed .mname span:first-child::after { content: ' •'; color: var(--accent); }
  .mval { color: var(--muted); font-size: 10.5px; }
  .mright { display: inline-flex; align-items: center; gap: 4px; }
  .medit { display: inline-grid; place-items: center; padding: 0; margin: 0; width: 15px; height: 15px; border: 0; border-radius: 4px; background: transparent; color: var(--muted); cursor: pointer; }
  .medit:hover { color: var(--text); background: var(--accent-soft); }
  .mcell input { width: 100%; }
  @media (max-width: 900px) { .macrogrid { grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 6px 14px; } }
</style>
