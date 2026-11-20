<!-- MacroStrip.svelte — the sound's live controls, always in view: the part's 8 macros, each with its name, its
     knob (a vertical drag or the wheel; double-click: back to the patch's value) and the parameters it moves,
     each with a bar showing where the macro puts it between its two values. A click on a macro's name opens its
     editor: name, targets (any parameter that moves live, with its value at macro 0 and at macro 1, in its own
     units), add or remove a target, the MIDI CC that turns it. Targets are engine parameters, the master or an
     effect; they are the part's map1..map32 slots (dsp/mod/partmod.h). A macro's CC turns it in the synth. -->
<script>
  import Param from './Param.svelte';
  import { fromNorm, toNorm, format } from './synth.js';

  let { engine, shared, byName, values, setValue, names = $bindable(), baseline } = $props();
  const MACROS = 8, MAPS = 32;
  let open = $state(-1); // macro whose editor is shown

  const get = (n) => { const d = byName[n]; return d ? values[d.id] ?? d.def : 0; };
  const set = (n, v) => { const d = byName[n]; if (d) setValue(d, v); };
  // a target's descriptor: an engine parameter (id 0..255) or the master / an effect (shared, below the maximum)
  const dstMax = $derived(byName['map1_destination']?.max ?? 256);
  const desc = (id) => (id >= 0 && id < 256 ? engine?.params[id] : id < dstMax ? shared.find((d) => d.id === id) : undefined);
  // the targets of every macro: [{slot, dst (descriptor), from, to (normalized)}]
  const targets = $derived.by(() => {
    const out = Array.from({ length: MACROS }, () => []);
    for (let s = 1; s <= MAPS; s++) {
      const mac = Math.round(get(`map${s}_macro`)), di = Math.round(get(`map${s}_destination`)) - 1;
      const dst = desc(di);
      if (mac >= 1 && mac <= MACROS && dst) out[mac - 1].push({ slot: s, dst, from: get(`map${s}_from`), to: get(`map${s}_to`) });
    }
    return out;
  });
  const live = $derived((engine?.params ?? []).filter((d) => d.realtime));
  const fx = $derived(shared.filter((d) => d.id >= 256 && d.id < dstMax && d.realtime));
  const used = (k) => names[k] || targets[k].length;

  function turn(k, v) { set(`macro${k + 1}`, Math.min(1, Math.max(0, v))); }
  let drag = null;
  function down(k, e) {
    drag = { k, y: e.clientY, v: get(`macro${k + 1}`) };
    e.currentTarget.setPointerCapture(e.pointerId);
  }
  function move(e) { if (drag) turn(drag.k, drag.v + (drag.y - e.clientY) / 150); }
  function wheel(k, e) { e.preventDefault(); turn(k, get(`macro${k + 1}`) - Math.sign(e.deltaY) * 0.02); }
  function key(k, e) {
    const step = e.shiftKey ? 0.1 : 0.01;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { turn(k, get(`macro${k + 1}`) + step); e.preventDefault(); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { turn(k, get(`macro${k + 1}`) - step); e.preventDefault(); }
  }
  const reset = (k) => { const d = byName[`macro${k + 1}`]; turn(k, baseline[d?.id] ?? 0); };

  function addTarget(k) {
    let s = 1;
    while (s <= MAPS && Math.round(get(`map${s}_macro`)) >= 1) s++;
    if (s > MAPS || !live.length) return;
    const d = live.find((x) => !targets[k].some((t) => t.dst.id === x.id)) ?? live[0];
    const now = toNorm(d, values[d.id] ?? d.def);
    set(`map${s}_destination`, d.id + 1);
    set(`map${s}_from`, now);
    set(`map${s}_to`, Math.min(1, now + 0.3));
    set(`map${s}_macro`, k + 1);
  }
  function removeTarget(t) { set(`map${t.slot}_macro`, 0); set(`map${t.slot}_destination`, 0); }
  function retarget(t, id) {
    const d = desc(id);
    const now = toNorm(d, values[d.id] ?? d.def);
    set(`map${t.slot}_destination`, id + 1);
    set(`map${t.slot}_from`, now);
    set(`map${t.slot}_to`, Math.min(1, now + 0.3));
  }
  // where the macro puts a target now, 0..1 of its range
  const pos = (t, m) => Math.min(1, Math.max(0, t.from + (t.to - t.from) * m));
  const label = (d) => d.name.replace(/_/g, ' ');
</script>

<section class="macros" aria-label="Macros">
  {#each Array(MACROS) as _, k}
    {@const m = get(`macro${k + 1}`)}
    <div class="macro" class:empty={!used(k)} class:on={open === k}>
      <div class="knob" role="slider" tabindex="0" aria-label={names[k] || `Macro ${k + 1}`} aria-valuemin="0" aria-valuemax="1"
           aria-valuenow={m.toFixed(2)} style="--v:{m}" onpointerdown={(e) => down(k, e)} onpointermove={move}
           onpointerup={() => (drag = null)} onwheel={(e) => wheel(k, e)} onkeydown={(e) => key(k, e)}
           ondblclick={() => reset(k)} title="drag up/down, wheel or arrows; double-click: the patch's value">
        <span class="val">{Math.round(m * 100)}</span>
      </div>
      <button class="mname" onclick={() => (open = open === k ? -1 : k)} title="edit this macro">
        {names[k] || `Macro ${k + 1}`}
      </button>
      <div class="chips">
        {#each targets[k] as t (t.slot)}
          <span class="tgt" title="{t.dst.name}: {format(t.dst, fromNorm(t.dst, t.from))} → {format(t.dst, fromNorm(t.dst, t.to))}">
            <span class="cbar"><span style="width:{pos(t, m) * 100}%"></span></span>{label(t.dst)}
          </span>
        {/each}
      </div>
    </div>
  {/each}
</section>

{#if open >= 0}
  {@const k = open}
  <section class="editor card" aria-label="Macro {k + 1}">
    <div class="ehead">
      <input class="ename" placeholder="Macro {k + 1}" value={names[k]} oninput={(e) => (names[k] = e.target.value)} aria-label="Name" />
      <label class="cc">CC <input type="number" min="0" max="119" value={Math.round(get(`macro${k + 1}_cc`))}
             onchange={(e) => set(`macro${k + 1}_cc`, Math.max(0, Math.min(119, Math.round(+e.target.value || 0))))}
             title="MIDI CC that turns this macro (0 = none)" /></label>
      <span class="muted">macro{k + 1} · its targets move together, each between two values</span>
      <button class="link" onclick={() => (open = -1)}>Close</button>
    </div>
    {#each targets[k] as t (t.slot)}
      <div class="trow">
        <select value={t.dst.id} onchange={(e) => retarget(t, +e.target.value)} aria-label="Parameter">
          <optgroup label={engine.name}>{#each live as d}<option value={d.id}>{d.name}</option>{/each}</optgroup>
          <optgroup label="master and effects">{#each fx as d}<option value={d.id}>{d.name}</option>{/each}</optgroup>
        </select>
        <Param d={t.dst} label="at 0" value={fromNorm(t.dst, t.from)} onchange={(v) => set(`map${t.slot}_from`, toNorm(t.dst, v))} />
        <Param d={t.dst} label="at 1" value={fromNorm(t.dst, t.to)} onchange={(v) => set(`map${t.slot}_to`, toNorm(t.dst, v))} />
        <button class="link" onclick={() => removeTarget(t)} aria-label="Remove {t.dst.name}">Remove</button>
      </div>
    {:else}
      <p class="muted">No target yet: add the parameters this macro moves.</p>
    {/each}
    <button class="btn ghost" onclick={() => addTarget(k)}>+ Target</button>
  </section>
{/if}

<style>
  .macros { display: grid; grid-template-columns: repeat(8, minmax(0, 1fr)); gap: 8px; margin-bottom: 14px; }
  .macro { background: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 10px 8px 8px;
           display: grid; justify-items: center; align-content: start; gap: 6px; min-width: 0; }
  .macro.empty { opacity: 0.5; }
  .macro.on { border-color: var(--accent); }
  .knob { width: 46px; height: 46px; border-radius: 50%; cursor: ns-resize; touch-action: none; display: grid; place-items: center;
          background: conic-gradient(from 225deg, var(--accent) calc(var(--v) * 270deg), var(--track) 0 270deg, transparent 0); }
  .knob .val { width: 34px; height: 34px; border-radius: 50%; background: var(--surface); display: grid; place-items: center;
               font-size: 11px; font-variant-numeric: tabular-nums; color: var(--muted); }
  .knob:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  .mname { all: unset; cursor: pointer; font-size: 12px; font-weight: 600; max-width: 100%; white-space: nowrap; overflow: hidden;
           text-overflow: ellipsis; }
  .mname:hover { color: var(--accent); }
  .chips { display: grid; gap: 3px; width: 100%; }
  .tgt { display: grid; gap: 2px; font-size: 10px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .cbar { display: block; height: 3px; border-radius: 2px; background: var(--track); overflow: hidden; }
  .cbar span { display: block; height: 100%; background: var(--accent); }
  .editor { display: grid; gap: 8px; margin: -4px 0 14px; }
  .ehead { display: flex; align-items: center; gap: 10px; }
  .ehead .muted { flex: 1; font-size: 12px; }
  .cc { font-size: 12px; color: var(--muted); display: flex; align-items: center; gap: 4px; }
  .cc input { width: 52px; font: inherit; padding: 3px 6px; border: 1px solid var(--line); border-radius: 6px;
              background: var(--faint); color: var(--text); }
  .ename { font: inherit; font-weight: 600; padding: 4px 8px; border: 1px solid var(--line); border-radius: 6px; background: var(--faint);
           color: var(--text); width: 180px; }
  .trow { display: grid; grid-template-columns: 170px minmax(0, 1fr) minmax(0, 1fr) auto; gap: 10px; align-items: center; }
  .trow select { font: inherit; padding: 4px; border-radius: 6px; border: 1px solid var(--line); background: var(--faint); color: var(--text); }
  @media (max-width: 760px) {
    .macros { grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .trow { grid-template-columns: 1fr; }
  }
</style>
