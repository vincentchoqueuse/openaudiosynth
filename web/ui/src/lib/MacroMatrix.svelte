<!-- MacroMatrix.svelte — the macros set up in the Synth tab, as tables like the modulation matrix: first the eight
     macros (a name, the MIDI CC that turns them), then their targets, one row each: which macro, which parameter
     (any that moves live: the engine's, the master or an effect), its value with the macro at 0 and at 1 (sliders
     over the parameter's travel, read in its units). The knobs themselves are played in the Play tab. Targets are
     the part's map1..map32 slots (dsp/mod/partmod.h). -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';
  import { human } from './naming.js';
  let { engine, shared, byName, values, setValue, names = $bindable() } = $props();
  const MACROS = 8, MAPS = 32;

  const get = (n) => { const d = byName[n]; return d ? values[d.id] ?? d.def : 0; };
  const set = (n, v) => { const d = byName[n]; if (d) setValue(d, v); };
  const dstMax = $derived(byName['map1_destination']?.max ?? 256);
  const desc = (id) => (id >= 0 && id < 256 ? engine?.params[id] : id < dstMax ? shared.find((d) => d.id === id) : undefined);
  const live = $derived((engine?.params ?? []).filter((d) => d.realtime));
  const fx = $derived(shared.filter((d) => d.id >= 256 && d.id < dstMax && d.realtime));
  const label = (k) => names[k] || `Macro ${k + 1}`;

  // the targets in use, by macro then slot: {slot, macro (0..7), dst, from, to}
  const rows = $derived.by(() => {
    const out = [];
    for (let s = 1; s <= MAPS; s++) {
      const mac = Math.round(get(`map${s}_macro`)), dst = desc(Math.round(get(`map${s}_destination`)) - 1);
      if (mac >= 1 && mac <= MACROS && dst) out.push({ slot: s, macro: mac - 1, dst, from: get(`map${s}_from`), to: get(`map${s}_to`) });
    }
    return out.sort((a, b) => a.macro - b.macro || a.slot - b.slot);
  });
  const freeSlot = $derived(Array.from({ length: MAPS }, (_, i) => i + 1).find((s) => Math.round(get(`map${s}_macro`)) < 1));

  function add() { // a new target on the first macro without one (else macro 1), where its parameter is now
    if (!freeSlot || !live.length) return;
    const k = Array.from({ length: MACROS }, (_, i) => i).find((i) => !rows.some((r) => r.macro === i)) ?? 0;
    const d = live.find((x) => !rows.some((r) => r.dst.id === x.id)) ?? live[0];
    const now = toNorm(d, values[d.id] ?? d.def);
    set(`map${freeSlot}_destination`, d.id + 1);
    set(`map${freeSlot}_from`, now);
    set(`map${freeSlot}_to`, Math.min(1, now + 0.3));
    set(`map${freeSlot}_macro`, k + 1);
  }
  function retarget(r, id) { // a new parameter starts where it is now, going a third of its travel up
    const d = desc(id), now = toNorm(d, values[d.id] ?? d.def);
    set(`map${r.slot}_destination`, id + 1);
    set(`map${r.slot}_from`, now);
    set(`map${r.slot}_to`, Math.min(1, now + 0.3));
  }
  function remove(r) { set(`map${r.slot}_macro`, 0); set(`map${r.slot}_destination`, 0); }
  const shown = (d, n) => format(d, fromNorm(d, n));
</script>

<table class="matrix names">
  <thead><tr><th>#</th><th>Name</th><th>CC</th><th>Targets</th></tr></thead>
  <tbody>
    {#each Array(MACROS) as _, k}
      {@const n = rows.filter((r) => r.macro === k).length}
      <tr class:empty={!names[k] && !n}>
        <td class="num mono">{k + 1}</td>
        <td><input class="name" placeholder="Macro {k + 1}" value={names[k]} oninput={(e) => (names[k] = e.target.value)} aria-label="Name of macro {k + 1}" /></td>
        <td class="cc"><input type="number" min="0" max="119" value={Math.round(get(`macro${k + 1}_cc`))} aria-label="MIDI CC of macro {k + 1}"
                   onchange={(e) => set(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))} title="MIDI CC that turns it (0 = none)" /></td>
        <td class="muted">{n ? `${n} target${n > 1 ? 's' : ''}` : '—'}</td>
      </tr>
    {/each}
  </tbody>
</table>

<h3>Targets</h3>
<table class="matrix">
  <thead><tr><th>Macro</th><th>Parameter</th><th>At 0</th><th></th><th>At 1</th><th></th><th></th></tr></thead>
  <tbody>
    {#each rows as r (r.slot)}
      <tr>
        <td><select value={r.macro + 1} onchange={(e) => set(`map${r.slot}_macro`, +e.target.value)} aria-label="Macro">
          {#each Array(MACROS) as _, k}<option value={k + 1}>{k + 1} · {label(k)}</option>{/each}
        </select></td>
        <td><select value={r.dst.id} onchange={(e) => retarget(r, +e.target.value)} aria-label="Parameter">
          <optgroup label={engine.name}>{#each live as d}<option value={d.id}>{human(d.name)}</option>{/each}</optgroup>
          <optgroup label="master and effects">{#each fx as d}<option value={d.id}>{human(d.name)}</option>{/each}</optgroup>
        </select></td>
        <td class="amt"><input type="range" min="0" max="1" step="0.001" value={r.from} style="--p:{r.from * 100}%"
                   oninput={(e) => set(`map${r.slot}_from`, +e.target.value)} aria-label="Value at macro 0" /></td>
        <td class="val mono">{shown(r.dst, r.from)}</td>
        <td class="amt"><input type="range" min="0" max="1" step="0.001" value={r.to} style="--p:{r.to * 100}%"
                   oninput={(e) => set(`map${r.slot}_to`, +e.target.value)} aria-label="Value at macro 1" /></td>
        <td class="val mono">{shown(r.dst, r.to)}</td>
        <td><button class="x" onclick={() => remove(r)} aria-label="Remove this target" title="remove">×</button></td>
      </tr>
    {/each}
  </tbody>
</table>
{#if freeSlot}<button class="ghost add" onclick={add}>+ Target</button>{/if}
<p class="hint">Each macro moves its targets together, each from its value at 0 to its value at 1. The knobs are in Play.</p>

<style>
  .matrix { width: 100%; border-collapse: collapse; font-size: 12px; }
  th { text-align: left; font-weight: 500; font-size: 10.5px; color: var(--muted); letter-spacing: .04em; padding: 0 6px 6px; border-bottom: 1px solid var(--line); }
  td { padding: 4px 6px; border-bottom: 1px solid color-mix(in srgb, var(--line) 50%, transparent); vertical-align: middle; }
  tr.empty td { opacity: .55; }
  .num { width: 1.6em; color: var(--muted); }
  select, input.name, .cc input { width: 100%; border: 0; background: var(--faint); border-radius: 6px; padding: 3px 6px; font-size: 12px; min-width: 0; color: var(--text); }
  .cc { width: 5em; }
  .names { margin-bottom: 18px; }
  .amt { width: 22%; min-width: 90px; }
  .amt input { width: 100%; }
  .val { width: 6.5em; text-align: right; color: var(--muted); white-space: nowrap; font-size: 11.5px; }
  .x { width: 20px; height: 20px; border-radius: 50%; color: var(--muted); }
  .x:hover { background: var(--faint); color: var(--text); }
  .add { margin-top: 6px; }
</style>
