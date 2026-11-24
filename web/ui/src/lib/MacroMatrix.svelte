<!-- MacroMatrix.svelte — the macros set up in the Synth view, on one page: Wander first (how far and how fast the
     macros wander by themselves: the part's `wander` and `wander_rate`), then the eight macros, each a group: its
     name, the MIDI CC that turns it (not on a phone) and Add setting; under it the settings it moves (any that
     moves live: the instrument's, the master or an effect), each with its value with the macro at 0 and at 1,
     sliders over the setting's travel, read in its units. They are played above the keyboard (MacroStrip). The
     32 slots are decoded once, in the session (macroMaps). -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import Select from './Select.svelte';
  import { precise } from './slider.js';
  import Param from './Param.svelte';

  const used = (r) => r.macro >= 0 && !!r.dst;
  const rowsOf = (k) => s.macroMaps.filter((r) => r.macro === k && r.dst);
  const free = $derived(s.macroMaps.find((r) => !used(r)));
  const settings = $derived([{ heading: s.engine.name }, ...s.macroTargets.engine.map((d) => ({ value: d.id, label: human(d.name) })),
                             { heading: 'master and effects' }, ...s.macroTargets.fx.map((d) => ({ value: d.id, label: human(d.name) }))]);

  function retarget(r, id) { // a new setting starts where it is now, going a third of its travel up
    const now = toNorm(s.paramById(id), s.values[id] ?? s.paramById(id).def);
    s.setP(`map${r.slot}_destination`, id + 1);
    s.setP(`map${r.slot}_from`, now);
    s.setP(`map${r.slot}_to`, Math.min(1, now + 0.3));
  }
  function add(k) { // one more setting for macro k: the first of the instrument's that no macro moves yet
    const { engine } = s.macroTargets, r = free;
    if (!r || !engine.length) return;
    retarget(r, (engine.find((x) => !s.macroMaps.some((o) => o.dst?.id === x.id)) ?? engine[0]).id);
    s.setP(`map${r.slot}_macro`, k + 1);
  }
  function remove(r) { s.setP(`map${r.slot}_macro`, 0); s.setP(`map${r.slot}_destination`, 0); }
  const shown = (d, n) => format(d, fromNorm(d, n));
</script>

{#if s.byName.wander}
  <div class="drift">
    <Param d={s.byName.wander} label="Wander" />
    <Param d={s.byName.wander_rate} label="Wander rate" />
    <span class="hint">Wander moves every macro slowly on its own random walk: the sound drifts through what its macros reach.</span>
  </div>
{/if}
<table class="matrix macros">
  <colgroup><col class="c-num" /><col class="c-dst" /><col /><col class="c-val" /><col /><col class="c-val" /><col class="c-x" /></colgroup>
  <thead><tr><th>#</th><th>Macro, then its settings</th><th>At 0</th><th></th><th>At 1</th><th></th><th></th></tr></thead>
  <tbody>
    {#each s.macroNames as name, k}
      {@const rows = rowsOf(k)}
      <tr class="group" class:empty={!name && !rows.length}>
        <td class="num mono">{k + 1}</td>
        <td><input class="name" placeholder="Macro {k + 1}" value={name} oninput={(e) => (s.macroNames[k] = e.target.value)} aria-label="Name of macro {k + 1}" /></td>
        <td colspan="4">
          <span class="ghead">
            {#if !s.phone}<label class="cc" title="MIDI CC that turns it (0 = none)">CC <input type="number" min="0" max="119" value={Math.round(s.getP(`macro${k + 1}_cc`))}
                   aria-label="MIDI CC of macro {k + 1}" onchange={(e) => s.setP(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))} /></label>{/if}
            <button class="add" disabled={!free} onclick={() => add(k)}>Add setting</button>
          </span>
        </td>
        <td></td>
      </tr>
      {#each rows as r (r.slot)}
        <tr>
          <td></td>
          <td><Select value={r.dst.id} onchange={(v) => retarget(r, v)} label="Setting moved by macro {k + 1}" options={settings} /></td>
          <td><input type="range" min="0" max="1" step="0.001" value={r.from} style="--p:{r.from * 100}%"
                     use:precise={{ value: () => r.from, set: (v) => s.setP(`map${r.slot}_from`, v) }} aria-label="Value at macro 0" /></td>
          <td class="val mono">{shown(r.dst, r.from)}</td>
          <td><input type="range" min="0" max="1" step="0.001" value={r.to} style="--p:{r.to * 100}%"
                     use:precise={{ value: () => r.to, set: (v) => s.setP(`map${r.slot}_to`, v) }} aria-label="Value at macro 1" /></td>
          <td class="val mono">{shown(r.dst, r.to)}</td>
          <td><button class="x" onclick={() => remove(r)} aria-label="Remove this setting from macro {k + 1}" title="remove">×</button></td>
        </tr>
      {/each}
    {/each}
  </tbody>
</table>
<p class="hint">Each macro moves its settings together, each from its value at 0 to its value at 1. {s.macroMaps.filter(used).length} of {s.macroMaps.length} settings in use.</p>
