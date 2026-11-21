<!-- MacroStrip.svelte — the Play tab's macros: the part's 8 macros as rows shaped like every other parameter (name,
     slider, value in percent; double-click: back to the sound's value), stacked, each with its MIDI CC and the
     parameters it moves in small text under it. They are set up (names, CC, targets) in the Synth tab (MacroMatrix); a macro's
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
    <div class="mrow" class:empty={!names[k] && !moves[k].length}>
      <div class="param" class:changed={Math.abs(m - base(k)) > 1e-3}>
        <span class="label" title="double-click: back to the sound's value" ondblclick={() => set(`macro${k + 1}`, base(k))} role="presentation">
          {name(k)}
        </span>
        <input type="range" min="0" max="1" step="0.001" value={m} style="--p:{m * 100}%" aria-label={name(k)}
               oninput={(e) => set(`macro${k + 1}`, +e.target.value)} ondblclick={() => set(`macro${k + 1}`, base(k))} />
        <span class="val">{Math.round(m * 100)} %</span>
        <span class="cc" title="MIDI CC that turns it (set in Synth, Macros)">{cc(k) ? `CC ${cc(k)}` : '—'}</span>
      </div>
      {#if !(moves[k].length === 1 && moves[k][0].toLowerCase() === name(k).toLowerCase())}
        <div class="moves" title={moves[k].join(', ')}>{moves[k].length ? moves[k].join(' · ') : 'nothing assigned'}</div>
      {/if}
    </div>
  {/each}
</div>

<style>
  .macrogrid { display: grid; gap: 6px; max-width: 900px; }
  .macrogrid .param { grid-template-columns: 10em 1fr 5.2em 4.5em; min-height: 32px; }
  .cc { font-family: var(--mono); font-size: 11px; color: var(--muted); text-align: right; }
  .mrow { min-width: 0; }
  .mrow.empty { opacity: .5; }
  .moves { font-size: 10.5px; color: var(--muted); margin: -6px 0 2px; padding-left: calc(10em + 10px); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  @media (max-width: 760px) { .macrogrid .param { grid-template-columns: 7em 1fr 4em 3.5em; } .moves { padding-left: 0; } }
</style>
