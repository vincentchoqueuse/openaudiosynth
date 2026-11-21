<!-- MacroStrip.svelte — the macros to play, above the keyboard: the part's 8 macros as rows shaped like every other
     parameter (name, slider, value in percent; double-click: back to the sound's value), 2 rows of 4 on a wide
     screen; the MIDI CC and the parameters each moves are in its tooltip. They are set up (names, CC, targets) in the Synth tab (MacroMatrix); a macro's
     CC turns it in the synth. -->
<script>
  import { human } from './naming.js';
  let { engine, shared, byName, values, setValue, names = $bindable(), baseline } = $props();
  const MACROS = 8, MAPS = 32;

  const get = (n) => { const d = byName[n]; return d ? values[d.id] ?? d.def : 0; };
  const set = (n, v) => { const d = byName[n]; if (d) setValue(d, v); };
  // a target: an engine parameter (id 0..255) or the master / an effect (shared, below the maximum)
  const dstMax = $derived(byName['map1_destination']?.max ?? 256);
  const desc = (id) => (id >= 0 && id < 256 ? engine?.params[id] : id < dstMax ? shared.find((d) => d.id === id) : undefined);
  // what each macro moves, by name
  const moves = $derived.by(() => {
    const out = Array.from({ length: MACROS }, () => []);
    for (let s = 1; s <= MAPS; s++) {
      const mac = Math.round(get(`map${s}_macro`)), dst = desc(Math.round(get(`map${s}_destination`)) - 1);
      if (mac >= 1 && mac <= MACROS && dst) out[mac - 1].push(human(dst.name));
    }
    return out;
  });
  const base = (k) => baseline[byName[`macro${k + 1}`]?.id] ?? 0;
  const cc = (k) => Math.round(get(`macro${k + 1}_cc`));
  const name = (k) => names[k] || `Macro ${k + 1}`;
</script>

<div class="macrogrid">
  {#each Array(MACROS) as _, k}
    {@const m = get(`macro${k + 1}`)}
    <div class="mrow" class:empty={!names[k] && !moves[k].length}
         title="{cc(k) ? `CC ${cc(k)}` : 'no CC'} · {moves[k].length ? moves[k].join(', ') : 'nothing assigned'}">
      <div class="param" class:changed={Math.abs(m - base(k)) > 1e-3}>
        <span class="label" title="double-click: back to the sound's value" ondblclick={() => set(`macro${k + 1}`, base(k))} role="presentation">
          {name(k)}
        </span>
        <input type="range" min="0" max="1" step="0.001" value={m} style="--p:{m * 100}%" aria-label={name(k)}
               oninput={(e) => set(`macro${k + 1}`, +e.target.value)} ondblclick={() => set(`macro${k + 1}`, base(k))} />
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
  @media (max-width: 760px) { .macrogrid { grid-template-columns: 1fr; } }
</style>
