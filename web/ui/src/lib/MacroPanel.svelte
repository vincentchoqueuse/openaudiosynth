<!-- MacroPanel.svelte — the side panel's body for one macro: its name, its value, the MIDI CC that turns it, then the
     settings it moves (any that moves live: the instrument's, the master or an effect), each with its value with the
     macro at 0 and at 1, sliders over the setting's travel read in its units; Add setting takes one of the 32 slots
     the macros share (session.macroMaps). -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import Select from './Select.svelte';
  import Param from './Param.svelte';
  import { precise } from './slider.js';
  let { k } = $props(); // 0..7

  const rows = $derived(s.macroMaps.filter((r) => r.macro === k && r.dst));
  const free = $derived(s.macroMaps.find((r) => !(r.macro >= 0 && r.dst)));
  const settings = $derived([{ heading: s.engine?.name ?? 'instrument' }, ...s.macroTargets.engine.map((d) => ({ value: d.id, label: human(d.name) })),
                             { heading: 'master and effects' }, ...s.macroTargets.fx.map((d) => ({ value: d.id, label: human(d.name) }))]);
  function retarget(r, id) { // a new setting starts where it is now, going a third of its travel up
    const now = toNorm(s.paramById(id), s.values[id] ?? s.paramById(id).def);
    s.setP(`map${r.slot}_destination`, id + 1);
    s.setP(`map${r.slot}_from`, now);
    s.setP(`map${r.slot}_to`, Math.min(1, now + 0.3));
  }
  function add() { // the first of the instrument's settings that no macro moves yet
    const { engine } = s.macroTargets, r = free;
    if (!r || !engine.length) return;
    retarget(r, (engine.find((x) => !s.macroMaps.some((o) => o.dst?.id === x.id)) ?? engine[0]).id);
    s.setP(`map${r.slot}_macro`, k + 1);
  }
  function remove(r) { s.setP(`map${r.slot}_macro`, 0); s.setP(`map${r.slot}_destination`, 0); }
  const shown = (d, n) => format(d, fromNorm(d, n));
</script>

<div class="sp-form">
  <span class="sp-label">Name</span>
  <input class="sp-input" placeholder="Macro {k + 1}" value={s.macroNames[k]} oninput={(e) => (s.macroNames[k] = e.target.value)} aria-label="Name" />
  {#if !s.phone}
    <span class="sp-label">MIDI CC</span>
    <input class="sp-input" type="number" min="0" max="119" value={Math.round(s.getP(`macro${k + 1}_cc`))} aria-label="MIDI CC (0 = none)"
           onchange={(e) => s.setP(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))} />
  {/if}
</div>
<Param d={s.byName[`macro${k + 1}`]} label="Value" />

<h3 class="sub">What it moves</h3>
{#each rows as r (r.slot)}
  <div class="sp-target">
    <span class="sp-target-head">
      <Select value={r.dst.id} onchange={(v) => retarget(r, v)} label="Setting" options={settings} />
      <button class="x" onclick={() => remove(r)} aria-label="Remove this setting" title="remove">×</button>
    </span>
    <label class="sp-range"><span class="sp-label">At 0</span>
      <input type="range" min="0" max="1" step="0.001" value={r.from} style="--p:{r.from * 100}%"
             use:precise={{ value: () => r.from, set: (v) => s.setP(`map${r.slot}_from`, v) }} aria-label="Value at 0" />
      <span class="mono muted">{shown(r.dst, r.from)}</span></label>
    <label class="sp-range"><span class="sp-label">At 1</span>
      <input type="range" min="0" max="1" step="0.001" value={r.to} style="--p:{r.to * 100}%"
             use:precise={{ value: () => r.to, set: (v) => s.setP(`map${r.slot}_to`, v) }} aria-label="Value at 1" />
      <span class="mono muted">{shown(r.dst, r.to)}</span></label>
  </div>
{:else}<p class="hint">Nothing yet.</p>{/each}
<button class="btn ghost" disabled={!free} onclick={add}>+ Add setting</button>
<p class="hint">The macros share {s.macroMaps.length} settings; {s.macroMaps.filter((r) => r.macro >= 0 && r.dst).length} in use.</p>
