<!-- StageViz.svelte — a small drawing at the top of a stage's card, from its parameters as they are: an oscillator's
     wave (a "..._wave" choice, with its pulse width), a filter's response (cutoff and resonance, its mode: low-, band-,
     high-pass or notch, 12 or 24 dB, frequency on a log axis 20 Hz .. 20 kHz), an envelope's shape (attack, decay,
     sustain, release, on a square-root time axis so short and long times both show) or an LFO's two cycles (a rate
     and a wave). Nothing when the stage is none of these. Drawn, not computed by the DSP: a picture of the settings. -->
<script>
  let { st, params, get } = $props(); // the stage, its parameter descriptors, value by name

  const W = 240, H = 46, P = 3;
  const find = (re) => params.find((d) => re.test(d.name));
  const label = (d) => (d?.labels ? d.labels[Math.round(get(d.name) - d.min)] : null);

  const kind = $derived.by(() => {
    const wave = find(/(^|_)wave$/), rate = find(/(^|_)rate$/);
    if (find(/(^|_)attack$/) && find(/(^|_)release$/)) return 'env';
    if (wave && rate && st.kind === 'mod') return 'lfo';
    if (wave && st.kind === 'source') return 'osc';
    if (st.kind === 'filter' && find(/(^|_)cutoff$/)) return 'filter';
    return null;
  });

  // one cycle of a wave, phase 0..1 -> -1..1
  function wave(name, p, pw = 0.5) {
    switch (name) {
      case 'saw': case 'supersaw': return 1 - 2 * p;
      case 'rsaw': return 2 * p - 1;
      case 'square': return p < 0.5 ? 1 : -1;
      case 'pulse': return p < pw ? 1 : -1;
      case 'wide': return p < 0.3 ? 1 : -1;
      case 'narrow': return p < 0.12 ? 1 : -1;
      case 'tri': return 1 - 4 * Math.abs(p - 0.5);
      case 'shark': return p < 0.5 ? -1 + 4 * p * p * 2 * 0.5 : 1 - 2 * (p - 0.5) * 2; // rounded rise, straight fall
      case 'sh': return [0.6, -0.4, 0.9, -0.7][Math.floor(p * 4) % 4];
      default: return Math.sin(2 * Math.PI * p);
    }
  }
  const line = (pts) => pts.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`).join(' ');
  const Y = (v) => H / 2 - v * (H / 2 - P);

  const path = $derived.by(() => {
    if (kind === 'osc' || kind === 'lfo') {
      const w = label(find(/(^|_)wave$/)) ?? 'sine';
      const pwd = find(/(^|_)pw$/), pw = pwd ? get(pwd.name) : 0.5;
      const cycles = 2, pts = [];
      for (let i = 0; i <= 200; i++) {
        const t = (cycles * i) / 200;
        let v = wave(w, t - Math.floor(t), pw);
        if (w === 'supersaw') v = (v + wave('saw', ((t * 1.07) % 1)) + wave('saw', ((t * 0.94) % 1))) / 3;
        pts.push([P + ((W - 2 * P) * i) / 200, Y(v * 0.9)]);
      }
      return line(pts);
    }
    if (kind === 'filter') {
      const fc = get(find(/(^|_)cutoff$/).name);
      const rd = find(/(^|_)(resonance|emphasis)$/), r = rd ? get(rd.name) : 0.7;
      const q = rd && rd.max <= 1.5 ? 0.7 + r * 9 : Math.max(0.5, r); // a 0..1 resonance, or a Q
      const mode = (label(find(/(^|_)(mode|type)$/)) ?? 'lp').toLowerCase();
      const poles4 = /24|ladder/.test(mode) || st.label.toLowerCase().includes('ladder') || /^(mono|acid|poly)/.test(st.id) && !find(/(^|_)(mode|type)$/);
      const pts = [];
      for (let i = 0; i <= 120; i++) {
        const f = 20 * Math.pow(1000, i / 120), x = f / fc, d = Math.hypot(1 - x * x, x / q);
        let m = /hp/.test(mode) ? (x * x) / d : /bp/.test(mode) ? x / q / d : /notch/.test(mode) ? Math.abs(1 - x * x) / d : 1 / d;
        if (poles4) m = m * m * Math.max(1, q / 2);
        if (/off/.test(mode)) m = 1;
        const db = Math.max(-36, Math.min(18, 20 * Math.log10(m + 1e-9)));
        pts.push([P + ((W - 2 * P) * i) / 120, P + ((18 - db) / 54) * (H - 2 * P)]);
      }
      return line(pts);
    }
    if (kind === 'env') {
      const v = (re, def) => { const d = find(re); return d ? get(d.name) : def; };
      const a = v(/(^|_)attack$/, 0.01), dcy = v(/(^|_)decay$/, 0.3), s = v(/(^|_)sustain$/, 0.7), rel = v(/(^|_)release$/, 0.5);
      const hasDecay = !!find(/(^|_)decay$/), hasSus = !!find(/(^|_)sustain$/);
      const sq = (t) => Math.sqrt(Math.max(0, t)); // short and long times both show
      const ta = sq(a), td = hasDecay ? sq(dcy) : 0, hold = 0.35 * (ta + td + sq(rel) + 0.3), tr = sq(rel);
      const total = ta + td + hold + tr, X = (t) => P + ((W - 2 * P) * t) / total, lvl = hasSus ? s : 1;
      const bottom = H - P, top = P;
      const yl = (l) => bottom - l * (bottom - top);
      return line([[X(0), bottom], [X(ta), top], [X(ta + td), yl(lvl)], [X(ta + td + hold), yl(lvl)], [X(total), bottom]]);
    }
    return '';
  });
</script>

{#if kind && path}
  <svg class="stageviz" viewBox="0 0 {W} {H}" preserveAspectRatio="none" aria-hidden="true">
    {#if kind !== 'env' && kind !== 'filter'}<line x1="0" x2={W} y1={H / 2} y2={H / 2} class="axis" />{/if}
    {#if kind === 'filter'}<line x1="0" x2={W} y1={P + (18 / 54) * (H - 2 * P)} y2={P + (18 / 54) * (H - 2 * P)} class="axis" />{/if}
    <path d={kind === 'env' ? `${path} Z` : kind === 'filter' ? `${path} L${W - P},${H} L${P},${H} Z` : `${path} L${W - P},${H / 2} L${P},${H / 2} Z`} class="fill" />
    <path d={path} class="stroke" />
  </svg>
{/if}

<style>
  .stageviz { display: block; width: 100%; height: 46px; margin: 2px 0 8px; border-radius: 6px; background: var(--side); }
  .axis { stroke: var(--line); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .fill { fill: var(--accent-soft); }
  .stroke { fill: none; stroke: var(--accent); stroke-width: 1.6; vector-effect: non-scaling-stroke; stroke-linejoin: round; }
</style>
