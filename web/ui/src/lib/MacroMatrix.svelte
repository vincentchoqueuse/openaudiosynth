<!-- MacroMatrix.svelte — the macros set up in the Synth view, as tables like the modulation matrix, in two tabs:
     Names (the eight macros' names and the MIDI CC that turns them, not on a phone), then Assignments (16 fixed rows, filled or
     none, like the modulation matrix: which macro, which parameter (any that moves live: the engine's, the master or an effect), its value with
     the macro at 0 and at 1, sliders over the parameter's travel, read in its units). They are played above the
     keyboard (MacroStrip). The slots are decoded once, in the session (macroMaps). -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import Select from './Select.svelte';
  let { view } = $props(); // 'names' | 'assign'
  const ROWS = 16; // the table shows the first 16 of the part's 32 slots
  const table = $derived(s.macroMaps.slice(0, ROWS));
  const count = (k) => s.macroMaps.filter((m) => m.macro === k && m.dst).length;

  function setMacro(r, k) { // k: 1..8, or 0 = none (clears the row)
    if (!k) return remove(r);
    const { engine } = s.macroTargets;
    if (!r.dst && engine.length) retarget(r, (engine.find((x) => !s.macroMaps.some((o) => o.dst?.id === x.id)) ?? engine[0]).id);
    s.setP(`map${r.slot}_macro`, k);
  }
  function retarget(r, id) { // a new parameter starts where it is now, going a third of its travel up
    if (id < 0) return remove(r);
    const now = toNorm(s.paramById(id), s.values[id] ?? s.paramById(id).def);
    s.setP(`map${r.slot}_destination`, id + 1);
    s.setP(`map${r.slot}_from`, now);
    s.setP(`map${r.slot}_to`, Math.min(1, now + 0.3));
  }
  function remove(r) { s.setP(`map${r.slot}_macro`, 0); s.setP(`map${r.slot}_destination`, 0); }
  const shown = (d, n) => format(d, fromNorm(d, n));
</script>

{#if view === 'names'}
<table class="matrix">
  <thead><tr><th>#</th><th>Name</th>{#if !s.phone}<th>CC</th>{/if}<th>Targets</th></tr></thead>
  <tbody>
    {#each s.macroNames as name, k}
      {@const n = count(k)}
      <tr class:empty={!name && !n}>
        <td class="num mono">{k + 1}</td>
        <td><input class="name" placeholder="Macro {k + 1}" value={name} oninput={(e) => (s.macroNames[k] = e.target.value)} aria-label="Name of macro {k + 1}" /></td>
        {#if !s.phone}<td class="cc"><input type="number" min="0" max="119" value={Math.round(s.getP(`macro${k + 1}_cc`))} aria-label="MIDI CC of macro {k + 1}"
                   onchange={(e) => s.setP(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))} title="MIDI CC that turns it (0 = none)" /></td>{/if}
        <td class="muted">{n ? `${n} target${n > 1 ? 's' : ''}` : '—'}</td>
      </tr>
    {/each}
  </tbody>
</table>
{:else}
<table class="matrix">
  <thead><tr><th>#</th><th>Macro</th><th>Parameter</th><th>At 0</th><th></th><th>At 1</th><th></th></tr></thead>
  <tbody>
    {#each table as r (r.slot)}
      {@const on = r.macro >= 0 && r.dst}
      <tr class:empty={!on}>
        <td class="num mono">{r.slot}</td>
        <td><Select value={r.macro + 1} onchange={(v) => setMacro(r, v)} label="Macro of row {r.slot}"
          options={[{ value: 0, label: 'none' }, ...s.macroNames.map((_, k) => ({ value: k + 1, label: `${k + 1} · ${s.macroName(k)}` }))]} /></td>
        <td><Select value={on ? r.dst.id : -1} disabled={!on} onchange={(v) => retarget(r, v)} label="Parameter of row {r.slot}"
          options={[{ value: -1, label: 'none' }, { heading: s.engine.name }, ...s.macroTargets.engine.map((d) => ({ value: d.id, label: human(d.name) })),
                    { heading: 'master and effects' }, ...s.macroTargets.fx.map((d) => ({ value: d.id, label: human(d.name) }))]} /></td>
        <td class="amt"><input type="range" min="0" max="1" step="0.001" value={r.from} style="--p:{r.from * 100}%" disabled={!on}
                   oninput={(e) => s.setP(`map${r.slot}_from`, +e.target.value)} aria-label="Value at macro 0" /></td>
        <td class="val mono">{on ? shown(r.dst, r.from) : ''}</td>
        <td class="amt"><input type="range" min="0" max="1" step="0.001" value={r.to} style="--p:{r.to * 100}%" disabled={!on}
                   oninput={(e) => s.setP(`map${r.slot}_to`, +e.target.value)} aria-label="Value at macro 1" /></td>
        <td class="val mono">{on ? shown(r.dst, r.to) : ''}</td>
      </tr>
    {/each}
  </tbody>
</table>
{/if}
<p class="hint">Each macro moves its targets together, each from its value at 0 to its value at 1. They are played above the keyboard.</p>
