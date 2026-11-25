<!-- MacroPanel.svelte — the side panel's body for one macro: its name, its value, the MIDI CC that turns it, then the
     settings it moves (any that moves live: the instrument's, the master or an effect), each with its value with the
     macro at 0 and at 1, sliders over the setting's travel read in its units; Add setting takes one of the 32 slots
     the macros share (session.macroMaps), in the footer. Routes that read the macro as a source are shown too. -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { human } from './naming.js';
  import { session as s } from './session.svelte.js';
  import Select from './Select.svelte';
  import Param from './Param.svelte';
  import { precise } from './slider.js';
  import Icon from './Icon.svelte';
  import RouteList from './RouteList.svelte';
  import * as R from './routes.js';
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
  // routes that read this macro as their source (set in a sound's file, or before): kept in view here
  const hasRoutes = $derived(R.keys('part').some((key) => { const x = R.get(s, key); return x.src && x.srcLabel === `macro${k + 1}`; }));
</script>

<div class="ptable">
  <div class="ptable-head"><span>Setting</span><span>Value</span></div>
  <div class="param"><span class="label">Name</span><input class="sp-input cell" placeholder="Macro {k + 1}" value={s.macroNames[k]} oninput={(e) => (s.macroNames[k] = e.target.value)} aria-label="Name" /></div>
  {#if !s.phone}
    <div class="param"><span class="label">MIDI CC</span><input class="sp-input cell" type="number" min="0" max="119" value={Math.round(s.getP(`macro${k + 1}_cc`))} aria-label="MIDI CC (0 = none)"
           onchange={(e) => s.setP(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))} /></div>
  {/if}
  <Param d={s.byName[`macro${k + 1}`]} label="Value" />
</div>

<h3 class="sub">What it moves</h3>
{#if rows.length}
  <table class="ptable routes">
    <colgroup><col style="width:36%" /><col /><col style="width:5.5em" /><col /><col style="width:5.5em" /><col style="width:30px" /></colgroup>
    <thead><tr><th>Setting</th><th>At 0</th><th></th><th>At 1</th><th></th><th></th></tr></thead>
    <tbody>
      {#each rows as r (r.slot)}
        <tr>
          <td><Select value={r.dst.id} onchange={(v) => retarget(r, v)} label="Setting" options={settings} /></td>
          <td><input type="range" min="0" max="1" step="0.001" value={r.from} style="--p:{r.from * 100}%"
                     use:precise={{ value: () => r.from, set: (v) => s.setP(`map${r.slot}_from`, v) }} aria-label="Value at 0" /></td>
          <td class="mono muted num">{shown(r.dst, r.from)}</td>
          <td><input type="range" min="0" max="1" step="0.001" value={r.to} style="--p:{r.to * 100}%"
                     use:precise={{ value: () => r.to, set: (v) => s.setP(`map${r.slot}_to`, v) }} aria-label="Value at 1" /></td>
          <td class="mono muted num">{shown(r.dst, r.to)}</td>
          <td><button class="iconbtn" onclick={() => remove(r)} aria-label="Remove this setting" title="Remove"><Icon name="close" size={12} /></button></td>
        </tr>
      {/each}
    </tbody>
  </table>
{:else}<p class="hint">It moves nothing yet.</p>{/if}
{#if hasRoutes}<h3 class="sub">Its routes</h3><RouteList src="macro{k + 1}" kinds={['part']} />{/if}
<div class="sp-foot">
  <button class="btn ghost" disabled={!free} onclick={add}><Icon name="plus" size={12} /> Add a setting</button>
  <span class="muted">{s.macroMaps.filter((r) => r.macro >= 0 && r.dst).length} of {s.macroMaps.length} in use</span>
</div>
