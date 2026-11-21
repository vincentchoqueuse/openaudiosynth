<!-- LfoView.svelte — an LFO edited the way an envelope is (MsegEditor): a bar of choices on top (its shape; a rate
     in Hz or a division in beats, as the envelopes' seconds or beats), its drawing (two cycles), and a bar of
     settings below (rate or division, restart on a note for the part LFOs). The fourier shape is drawn from its
     harmonics, set on drawbars: per harmonic 1..8, a (cosine) and b (sine), -1..1, dragged up and down
     (double-click: back to the default). `prefix` is "mlfo1_", "mlfo2_" or "vlfo_" (dsp/mod/partmod.h); the shapes
     are bipolar and drawn as the DSP computes them, sh as fixed random steps, wiener as a periodic random walk
     like the one each LFO draws at start. -->
<script>
  import { hasIcon, waveIcon } from './waves.js';
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
  const H_COUNT = 8;
  const coef = $derived(Array.from({ length: H_COUNT }, (_, k) => [get(`${prefix}a${k + 1}`), get(`${prefix}b${k + 1}`)]));
  function fourier(p) { // as voicemod_fourier: normalized by sum(|a| + |b|) above 1
    let y = 0, norm = 0;
    coef.forEach(([a, b], k) => {
      if (!a && !b) return;
      y += a * Math.cos(2 * Math.PI * (k + 1) * p) + b * Math.sin(2 * Math.PI * (k + 1) * p);
      norm += Math.abs(a) + Math.abs(b);
    });
    return norm > 1 ? y / norm : y;
  }
  // wiener: a periodic random walk (a Brownian bridge, as voicemod_wiener_build); each LFO has its own, this one
  // shows the kind of path
  const walk = (() => {
    let x = 0, seed = 7;
    const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 1073741824) - 1;
    const w = Array.from({ length: 32 }, () => { const v = x; x += rnd(); return v; }).map((v, k) => v - (x * k) / 32);
    const mean = w.reduce((a, b) => a + b, 0) / 32, peak = Math.max(...w.map((v) => Math.abs(v - mean)));
    return w.map((v) => (v - mean) / peak);
  })();
  const wiener = (p) => { const x = p * 32, i = Math.floor(x) % 32; return walk[i] + (walk[(i + 1) % 32] - walk[i]) * (x - Math.floor(x)); };
  function shape(ph, k) { // as lfo_shape in dsp/mod/partmod.c
    const p = ph - Math.floor(ph);
    switch (waves[wave]) {
      case 'tri': return 1 - 4 * Math.abs(p - 0.5);
      case 'saw': return 2 * p - 1;
      case 'square': return p < 0.5 ? 1 : -1;
      case 'sh': return steps[k % steps.length];
      case 'fourier': return fourier(p);
      case 'wiener': return wiener(p);
      default: return Math.sin(2 * Math.PI * p);
    }
  }
  // drawbars: drag up / down from where the pointer went down (200 px for the full range)
  let bar = null;
  function barDown(e, name) {
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    bar = { name, y: e.clientY, v: get(prefix + name) };
  }
  function barMove(e) {
    if (!bar) return;
    const fine = e.shiftKey ? 0.2 : 1;
    set(prefix + bar.name, Math.round(Math.min(1, Math.max(-1, bar.v + ((bar.y - e.clientY) / 100) * fine)) * 100) / 100);
  }
  function barReset(name) { set(prefix + name, name === 'b1' ? 1 : 0); }
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
    <div class="shapes" role="group" aria-label="Shape">
      {#each waves as w, i}
        <button class:on={wave === i} onclick={() => set(prefix + 'wave', i)} title={w} aria-label={w} aria-pressed={wave === i}>
          {#if hasIcon(w)}<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d={waveIcon(w)} fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round" stroke-linecap="round" /></svg>{:else}{w}{/if}
        </button>
      {/each}
    </div>
    <span class="vsep" aria-hidden="true"></span>
    <div class="seg small">
      <button class:on={!sync} title="a rate in Hz" onclick={() => set(prefix + 'sync', 0)}>Hz</button>
      <button class:on={!!sync} title="a division, locked to the tempo" onclick={() => { if (!sync) set(prefix + 'sync', Math.max(1, divs.indexOf('1/4'))); }}>beats</button>
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

  {#if waves[wave] === 'fourier'}
    <div class="bars" role="group" aria-label="Harmonics">
      {#each coef as [a, b], k}
        <div class="harm">
          <div class="pair">
            {#each [['a', a, 'cosine'], ['b', b, 'sine']] as [ab, v, what]}
              <div class="drawbar {ab}" role="slider" tabindex="0" aria-label="{what} of harmonic {k + 1}" aria-valuemin="-1" aria-valuemax="1"
                   aria-valuenow={v} title="{ab}{k + 1} ({what}): {v.toFixed(2)} · drag (Shift: fine), double-click: reset"
                   onpointerdown={(e) => barDown(e, ab + (k + 1))} onpointermove={barMove} onpointerup={() => (bar = null)}
                   ondblclick={() => barReset(ab + (k + 1))}
                   onkeydown={(e) => { const step = e.key === 'ArrowUp' ? 0.05 : e.key === 'ArrowDown' ? -0.05 : 0; if (step) { e.preventDefault(); set(prefix + ab + (k + 1), Math.min(1, Math.max(-1, Math.round((v + step) * 100) / 100))); } }}>
                <div class="fill" style="{v >= 0 ? `bottom:50%; height:${v * 50}%` : `top:50%; height:${-v * 50}%`}"></div>
              </div>
            {/each}
          </div>
          <span class="mono">{k + 1}</span>
        </div>
      {/each}
      <span class="muted legend"><i class="sw a"></i> a · cosine <i class="sw b"></i> b · sine</span>
    </div>
  {/if}

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
  .bars { display: flex; align-items: flex-end; gap: 14px; margin: 4px 0 10px; flex-wrap: wrap; }
  .harm { display: flex; flex-direction: column; align-items: center; gap: 4px; font-size: 10.5px; color: var(--muted); }
  .pair { display: flex; gap: 3px; }
  .drawbar { position: relative; width: 14px; height: 110px; border-radius: 4px; background: var(--faint); cursor: ns-resize; touch-action: none; outline: none; }
  .drawbar::after { content: ''; position: absolute; left: 0; right: 0; top: 50%; height: 1px; background: var(--line); }
  .drawbar:focus-visible { box-shadow: 0 0 0 1.5px var(--accent); }
  .drawbar .fill { position: absolute; left: 0; right: 0; border-radius: 3px; }
  .drawbar.a .fill, .sw.a { background: var(--accent); }
  .drawbar.b .fill, .sw.b { background: var(--text); opacity: .75; }
  .legend { align-self: center; margin-left: 8px; font-size: 11px; display: inline-flex; align-items: center; gap: 6px; }
  .sw { display: inline-block; width: 9px; height: 9px; border-radius: 2px; }
  .rate { flex: 1; min-width: 260px; max-width: 520px; }
  .div { display: inline-flex; align-items: center; gap: 8px; font-size: 12px; color: var(--muted); }
  .div select { border: 0; background: var(--faint); border-radius: 6px; padding: 3px 6px; color: var(--text); }
</style>
