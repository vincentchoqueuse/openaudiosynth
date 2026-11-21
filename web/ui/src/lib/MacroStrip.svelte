<!-- MacroStrip.svelte — the sound's live controls, always in view: the part's 8 macros, each with its name, its
     knob (a vertical drag or the wheel; double-click: back to the patch's value) and the parameters it moves,
     each with a bar showing where the macro puts it between its two values: the Play tab's knobs. They are set up
     (names, CC, targets) in the Synth tab (MacroMatrix). -->
<script>
  import { fromNorm, toNorm, format } from './synth.js';

  let { engine, shared, byName, values, setValue, names = $bindable(), baseline } = $props();
  const MACROS = 8, MAPS = 32;

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

  // where the macro puts a target now, 0..1 of its range
  const pos = (t, m) => Math.min(1, Math.max(0, t.from + (t.to - t.from) * m));
  const label = (d) => d.name.replace(/_/g, ' ');
</script>

<section class="macros" aria-label="Macros">
  {#each Array(MACROS) as _, k}
    {@const m = get(`macro${k + 1}`)}
    <div class="macro" class:empty={!used(k)}>
      <div class="knob" role="slider" tabindex="0" aria-label={names[k] || `Macro ${k + 1}`} aria-valuemin="0" aria-valuemax="1"
           aria-valuenow={m.toFixed(2)} style="--v:{m}" onpointerdown={(e) => down(k, e)} onpointermove={move}
           onpointerup={() => (drag = null)} onwheel={(e) => wheel(k, e)} onkeydown={(e) => key(k, e)}
           ondblclick={() => reset(k)} title="drag up/down, wheel or arrows; double-click: the patch's value">
        <span class="val">{Math.round(m * 100)}</span>
      </div>
      <span class="mname">{names[k] || `Macro ${k + 1}`}</span>
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

<style>
  .macros { display: grid; grid-template-columns: repeat(8, minmax(0, 1fr)); gap: 8px; margin-bottom: 14px; }
  .macro { background: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 10px 8px 8px;
           display: grid; justify-items: center; align-content: start; gap: 6px; min-width: 0; }
  .macro.empty { opacity: 0.5; }
  .knob { width: 46px; height: 46px; border-radius: 50%; cursor: ns-resize; touch-action: none; display: grid; place-items: center;
          background: conic-gradient(from 225deg, var(--accent) calc(var(--v) * 270deg), var(--track) 0 270deg, transparent 0); }
  .knob .val { width: 34px; height: 34px; border-radius: 50%; background: var(--surface); display: grid; place-items: center;
               font-size: 11px; font-variant-numeric: tabular-nums; color: var(--muted); }
  .knob:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  .mname { all: unset; font-size: 12px; font-weight: 600; max-width: 100%; white-space: nowrap; overflow: hidden;
           text-overflow: ellipsis; }
  .chips { display: grid; gap: 3px; width: 100%; }
  .tgt { display: grid; gap: 2px; font-size: 10px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .cbar { display: block; height: 3px; border-radius: 2px; background: var(--track); overflow: hidden; }
  .cbar span { display: block; height: 100%; background: var(--accent); }
</style>
