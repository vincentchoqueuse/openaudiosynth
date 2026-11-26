<!-- WavetableView.svelte — the table an oscillator of `wt` plays, as the engine holds it (synth_probe_wavetable, read
     from the worklet): its frames stacked in perspective from the first (front) to the last (back), the one its
     position plays lit (interpolated between two frames, as the oscillator does), and under them that frame large.
     Read again when the table changes; the position moves the lit frame live. -->
<script>
  import { session as s } from './session.svelte.js';
  let { osc, prefix } = $props(); // 0 or 1, and its parameters' prefix ("a_", "b_")

  const N = 128;
  let data = $state(null); // {count, n, frames}
  const table = $derived(Math.round(s.getP(prefix + 'table')));
  $effect(() => {
    table; s.patchGen;
    let live = true;
    s.synth?.wavetable(osc, N).then((r) => { if (live) data = r.count ? r : null; });
    return () => (live = false);
  });
  const pos = $derived(Math.min(1, Math.max(0, s.getP(prefix + 'position'))));

  const W = 400, H = 250, AMP = 16, BIG = 34, DX = 90, DY = 104;
  const frame = (f) => data.frames.subarray(f * N, (f + 1) * N);
  // a frame (or two mixed by t) as a path: x from x0 over w, y around y0
  function path(a, b, t, x0, y0, w, amp) {
    let d = '';
    for (let i = 0; i < N; i++) {
      const v = b ? a[i] + (b[i] - a[i]) * t : a[i];
      d += `${i ? 'L' : 'M'}${(x0 + (i * w) / (N - 1)).toFixed(1)},${(y0 - v * amp).toFixed(1)}`;
    }
    return d;
  }
  const at = (f, c) => ({ x: 20 + (f / Math.max(1, c - 1)) * DX, y: 140 - (f / Math.max(1, c - 1)) * DY });
  const cur = $derived.by(() => {
    if (!data) return null;
    const x = pos * (data.count - 1), f = Math.min(data.count - 2, Math.floor(x));
    return { f, t: x - f, x };
  });
</script>

{#if data}
  <svg class="wtview" viewBox="0 0 {W} {H}" role="img" aria-label="Wavetable: {data.count} frames, position {Math.round(pos * 100)} %">
    {#each Array.from({ length: data.count }, (_, k) => data.count - 1 - k) as f (f)}
      {@const p = at(f, data.count)}
      <path class="wt-frame" class:near={cur && Math.abs(f - cur.x) < 1} d={path(frame(f), null, 0, p.x, p.y, 270, AMP)} />
    {/each}
    {#if cur}
      {@const p = at(cur.x, data.count)}
      <path class="wt-cur" d={path(frame(cur.f), frame(cur.f + 1), cur.t, p.x, p.y, 270, AMP)} />
      <line class="wt-axis" x1="20" y1={H - 44} x2={W - 20} y2={H - 44} />
      <path class="wt-big" d={path(frame(cur.f), frame(cur.f + 1), cur.t, 20, H - 44, W - 40, BIG)} />
      <text class="wt-label" x={W - 20} y="14" text-anchor="end">frame {(cur.x + 1).toFixed(1)} / {data.count}</text>
    {/if}
  </svg>
{/if}

<style>
  .wtview { display: block; width: 100%; height: auto; margin: 0 0 8px; }
  .wt-frame { fill: none; stroke: var(--muted); stroke-width: 0.8; opacity: 0.35; }
  .wt-frame.near { opacity: 0.6; }
  .wt-cur { fill: none; stroke: var(--accent); stroke-width: 2; }
  .wt-big { fill: none; stroke: var(--accent); stroke-width: 2; }
  .wt-axis { stroke: var(--line); stroke-width: 1; }
  .wt-label { fill: var(--muted); font-size: 11px; font-family: var(--mono, monospace); }
</style>
