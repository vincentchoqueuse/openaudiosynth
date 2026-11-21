<!-- LfoView.svelte — an LFO edited the way an envelope is (MsegEditor): a bar of choices on top (its shape; free
     rate or locked to the tempo), its drawing (two cycles, on a time axis in seconds or beats), and a bar of
     settings below (rate or division, restart on a note for the part LFOs). `prefix` is "mlfo1_", "mlfo2_" or
     "vlfo_" (dsp/mod/partmod.h); the shapes are bipolar, sh draws random steps. -->
<script>
  import Param from './Param.svelte';
  let { prefix, get, set, byName } = $props();
  const H = 220, PAD = 16;
  let W = $state(600);

  const d = (n) => byName[prefix + n];
  const waves = $derived(d('wave')?.labels ?? ['sine', 'tri', 'saw', 'square', 'sh']);
  const divs = $derived(d('sync')?.labels ?? ['off']);
  const wave = $derived(Math.round(get(prefix + 'wave')));
  const sync = $derived(Math.round(get(prefix + 'sync'))); // 0: free rate
  const rate = $derived(get(prefix + 'rate'));
  // a division as beats ("1/16" -> 0.25, "1/8d" -> 0.75, "1/4t" -> 2/3, "2" -> 8): one cycle
  function divBeats(label) {
    const m = /^(\d+)(?:\/(\d+))?([td]?)$/.exec(label ?? '');
    if (!m) return 1;
    const bars = +m[1] / (m[2] ? +m[2] : 1);
    return bars * 4 * (m[3] === 't' ? 2 / 3 : m[3] === 'd' ? 1.5 : 1);
  }
  const cycle = $derived(sync ? divBeats(divs[sync]) : 1 / Math.max(0.01, rate)); // beats or seconds
  const unit = $derived(sync ? 'b' : 's');

  // the shape over a cycle, phase 0..1 -> -1..1 (sh: steps from a fixed draw, so the picture holds still)
  const steps = [0.6, -0.3, 0.9, -0.8, 0.2, -0.5, 0.7, -0.1];
  function shape(ph, k) {
    const p = ph - Math.floor(ph);
    switch (waves[wave]) {
      case 'tri': return p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4;
      case 'saw': return 1 - 2 * p;
      case 'square': return p < 0.5 ? 1 : -1;
      case 'sh': return steps[k % steps.length];
      default: return Math.sin(2 * Math.PI * p);
    }
  }
  const X = (t) => PAD + ((W - 2 * PAD) * t) / (2 * cycle);
  const Y = (v) => H / 2 - (v * (H / 2 - PAD));
  const path = $derived.by(() => {
    const n = 400, pts = [];
    for (let i = 0; i <= n; i++) {
      const ph = (2 * i) / n, k = Math.floor(ph * 4); // sh: four steps per cycle
      const v = waves[wave] === 'sh' ? steps[k % steps.length] : shape(ph, k);
      pts.push(`${i ? 'L' : 'M'}${X((ph * cycle)).toFixed(1)},${Y(v).toFixed(1)}`);
    }
    return pts.join(' ');
  });
  const ticks = $derived(Array.from({ length: 9 }, (_, i) => (i * 2 * cycle) / 8));
  const fmt = (t) => (t < 1 && !sync ? `${(t * 1000).toFixed(0)}ms` : `${+t.toFixed(2)}${unit}`);
</script>

<div class="mseg">
  <div class="mseg-bar">
    <div class="seg small">
      {#each waves as w, i}<button class:on={wave === i} onclick={() => set(prefix + 'wave', i)}>{w}</button>{/each}
    </div>
    <div class="seg small">
      <button class:on={!sync} onclick={() => set(prefix + 'sync', 0)}>free</button>
      <button class:on={!!sync} onclick={() => { if (!sync) set(prefix + 'sync', Math.max(1, divs.indexOf('1/4'))); }}>tempo</button>
    </div>
    <span class="muted info">{sync ? `one cycle per ${divs[sync]}` : `${+rate.toPrecision(3)} Hz`} · {fmt(cycle)} per cycle</span>
  </div>

  <div class="mseg-canvas" bind:clientWidth={W}>
    <svg width={W} height={H} aria-label="LFO shape">
      {#each ticks as t}
        <line class="grid" x1={X(t)} x2={X(t)} y1={PAD} y2={H - PAD} />
        <text class="tick" x={X(t) + 3} y={H - 3}>{fmt(t)}</text>
      {/each}
      <line class="grid" x1={PAD} x2={W - PAD} y1={Y(0)} y2={Y(0)} />
      <path class="area" d="{path} L{X(2 * cycle)},{Y(0)} L{X(0)},{Y(0)} Z" />
      <path class="curve" d={path} />
    </svg>
  </div>

  <div class="mseg-bar lfo-settings">
    {#if sync}
      <label class="div">Division
        <select value={sync} onchange={(e) => set(prefix + 'sync', +e.target.value)}>
          {#each divs as l, i}{#if i}<option value={i}>{l}</option>{/if}{/each}
        </select>
      </label>
    {:else if d('rate')}
      <div class="rate"><Param d={d('rate')} label="Rate" value={rate} onchange={(v) => set(prefix + 'rate', v)} /></div>
    {/if}
    {#if d('retrig')}
      <button class="chip" class:on={get(prefix + 'retrig') > 0.5} onclick={() => set(prefix + 'retrig', get(prefix + 'retrig') > 0.5 ? 0 : 1)}
              title="restarts when a note starts on a silent part">Restart on note</button>
    {:else}
      <span class="muted info">each note has its own, starting at its note-on</span>
    {/if}
  </div>
</div>

<style>
  .lfo-settings { gap: 16px; }
  .rate { flex: 1; min-width: 260px; max-width: 520px; }
  .div { display: inline-flex; align-items: center; gap: 8px; font-size: 12px; color: var(--muted); }
  .div select { border: 0; background: var(--faint); border-radius: 6px; padding: 3px 6px; color: var(--text); }
</style>
