<!-- Analyse.svelte — the Analyse view: what comes out, measured, and set against a reference recording when one is
     loaded (Ref… in the top bar). The spectrum, 20 Hz .. 20 kHz on a log axis, the synth filled in the accent and the
     reference drawn over it (Live: peaks falling; Average: about two seconds), the waveform (triggered on a rising
     zero crossing, its gain automatic), the levels (peak held, RMS, crest factor, headroom to 0 dBFS) and the energy
     in six bands, each with the reference's and the difference. Both are tapped before the A/B switch
     (synth.js synthTap, refTap), so the synth is measured while the reference is heard; the reference sounds with
     the clip, or alone while it is heard. -->
<script>
  import Section from './Section.svelte';
  import Seg from './Seg.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './prefs.js';

  const FLOOR = -110, TOP = -10, LO = 20, HI = 20000;
  const BANDS = [['Sub', 20, 80], ['Low', 80, 250], ['Low mid', 250, 800], ['Mid', 800, 2500], ['High', 2500, 8000], ['Air', 8000, 20000]];
  let mode = $state(remembered('synth.analyse', 'avg') === 'live' ? 'live' : 'avg');
  $effect(() => remember('synth.analyse', mode));
  let spec, wave;
  // the figures, refreshed a few times a second: {peak, rms, crest} per source, bands [[synth, ref]]
  let read = $state({ synth: null, ref: null, bands: BANDS.map(() => [null, null]) });
  let playing = $state(false); // the reference sounds now (with the clip, or heard)

  $effect(() => {
    const A = s.synth.synthTap, B = s.synth.refTap;
    if (!A || !B) return;
    const sr = A.context.sampleRate, N = A.fftSize;
    const fa = new Float32Array(A.frequencyBinCount), fb = new Float32Array(B.frequencyBinCount);
    const ta = new Float32Array(N), tb = new Float32Array(N);
    const meas = () => ({ peak: -Infinity, ms: 0, held: -Infinity });
    const ma = meas(), mb = meas();
    const bandsA = BANDS.map(() => -Infinity), bandsB = BANDS.map(() => -Infinity);
    let raf, frame = 0, gain = 4, ca = null, cb = null; // ca, cb: the curves, per pixel (dB)

    const size = (c) => {
      const dpr = devicePixelRatio, W = Math.round(c.clientWidth * dpr), H = Math.round(c.clientHeight * dpr);
      if (c.width !== W || c.height !== H) (c.width = W), (c.height = H);
      return [c.getContext('2d'), W, H, dpr];
    };
    // a level of the whole frame: peak held (falling 0.3 dB a frame), mean square smoothed over ~300 ms
    function level(t, m) {
      let pk = 0, sq = 0;
      for (const v of t) { pk = Math.max(pk, Math.abs(v)); sq += v * v; }
      m.held = Math.max(20 * Math.log10(pk + 1e-9), m.held - 0.3);
      m.ms += (sq / t.length - m.ms) * 0.1;
    }
    // the dB of a pixel's span of frequencies: its loudest bin, or between the two nearest when narrower than a bin
    function at(freq, f0, f1) {
      const b0 = Math.floor((f0 / sr) * N), b1 = Math.max(b0 + 1, Math.floor((f1 / sr) * N));
      if (b1 > b0 + 1) { let v = -Infinity; for (let b = b0; b < b1 && b < freq.length; b++) v = Math.max(v, freq[b]); return v; }
      const fx = (0.5 * (f0 + f1) * N) / sr, k = Math.floor(fx), t = fx - k;
      return (freq[k] ?? FLOOR) * (1 - t) + (freq[k + 1] ?? FLOOR) * t;
    }
    // the energy in a band (dB): the bins' powers summed
    function band(freq, lo, hi) {
      let p = 0;
      for (let b = Math.ceil((lo / sr) * N); b <= Math.floor((hi / sr) * N) && b < freq.length; b++) p += 10 ** (freq[b] / 10);
      return 10 * Math.log10(p + 1e-12);
    }
    function curve(c, freq, W) {
      const ls = Math.log(HI / LO), k = mode === 'avg' ? 0.06 : 1;
      if (!c || c.length !== W) c = new Float32Array(W).fill(FLOOR);
      for (let x = 0; x < W; x++) {
        const v = Math.max(FLOOR, Math.min(TOP, at(freq, LO * Math.exp((ls * x) / W), LO * Math.exp((ls * (x + 1)) / W))));
        c[x] = mode === 'avg' ? c[x] + (v - c[x]) * k : Math.max(v, c[x] - 1.2);
      }
      return c;
    }

    const draw = () => {
      const css = getComputedStyle(spec);
      const accent = css.getPropertyValue('--accent'), soft = css.getPropertyValue('--accent-soft'), refc = css.getPropertyValue('--warn');
      const grid = css.getPropertyValue('--line'), dim = css.getPropertyValue('--muted');
      const refOn = s.synth.refPlaying;
      A.getFloatFrequencyData(fa); A.getFloatTimeDomainData(ta); level(ta, ma);
      if (refOn) { B.getFloatFrequencyData(fb); B.getFloatTimeDomainData(tb); level(tb, mb); }
      const ease = (arr, i, v) => (arr[i] = isFinite(arr[i]) ? arr[i] + (v - arr[i]) * 0.08 : v);
      BANDS.forEach(([, lo, hi], i) => { ease(bandsA, i, band(fa, lo, hi)); if (refOn) ease(bandsB, i, band(fb, lo, hi)); });

      // the spectrum: grid, the synth filled, the reference over it
      {
        const [ctx, W, H, dpr] = size(spec);
        ctx.clearRect(0, 0, W, H);
        const ls = Math.log(HI / LO), y = (db) => (H * (TOP - db)) / (TOP - FLOOR);
        ctx.lineWidth = dpr; ctx.strokeStyle = grid; ctx.fillStyle = dim; ctx.font = `${10 * dpr}px ui-monospace, monospace`;
        for (const [f, t] of [[50, '50'], [100, '100'], [200, '200'], [500, '500'], [1000, '1k'], [2000, '2k'], [5000, '5k'], [10000, '10k']]) {
          const x = (W * Math.log(f / LO)) / ls;
          ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke(); ctx.fillText(t, x + 4 * dpr, H - 4 * dpr);
        }
        for (let db = -20; db > FLOOR; db -= 20) { ctx.beginPath(); ctx.moveTo(0, y(db)); ctx.lineTo(W, y(db)); ctx.stroke(); ctx.fillText(`${db}`, 4 * dpr, y(db) - 3 * dpr); }
        ca = curve(ca, fa, W);
        ctx.beginPath(); ctx.moveTo(0, H);
        for (let x = 0; x < W; x++) ctx.lineTo(x, y(ca[x]));
        ctx.lineTo(W, H); ctx.closePath(); ctx.fillStyle = soft; ctx.fill();
        ctx.strokeStyle = accent; ctx.lineWidth = 1.4 * dpr; ctx.stroke();
        if (refOn) {
          cb = curve(cb, fb, W);
          ctx.beginPath();
          for (let x = 0; x < W; x++) x ? ctx.lineTo(x, y(cb[x])) : ctx.moveTo(x, y(cb[x]));
          ctx.strokeStyle = refc; ctx.lineWidth = 1.6 * dpr; ctx.stroke();
        } else cb = null;
      }

      // the waveform: 20 ms after the last rising zero crossing that leaves room
      {
        const [ctx, W, H, dpr] = size(wave);
        ctx.clearRect(0, 0, W, H);
        const span = 960;
        let t0 = ta.length - span - 1;
        for (let i = ta.length - span - 1; i > ta.length / 2; i--) if (ta[i - 1] < 0 && ta[i] >= 0) { t0 = i; break; }
        let m = 1e-6;
        for (let i = 0; i < span; i++) m = Math.max(m, Math.abs(ta[t0 + i]));
        const target = Math.min(32, Math.max(1, 0.8 / m));
        gain += (target - gain) * (target < gain ? 0.5 : 0.1);
        ctx.strokeStyle = grid; ctx.lineWidth = dpr;
        ctx.beginPath(); ctx.moveTo(0, H / 2); ctx.lineTo(W, H / 2); ctx.stroke();
        ctx.strokeStyle = accent; ctx.lineWidth = 1.5 * dpr; ctx.lineJoin = 'round';
        ctx.beginPath();
        for (let i = 0; i < span; i++) { const x = (i / (span - 1)) * W, yy = H / 2 - ta[t0 + i] * gain * H * 0.45; i ? ctx.lineTo(x, yy) : ctx.moveTo(x, yy); }
        ctx.stroke();
      }

      // the figures, five times a second
      if (++frame % 12 === 0) {
        const fig = (m) => { const rms = 10 * Math.log10(m.ms + 1e-12); return m.held > -90 ? { peak: m.held, rms, crest: m.held - rms } : null; };
        playing = refOn;
        read = { synth: fig(ma), ref: refOn ? fig(mb) : null,
                 bands: BANDS.map((_, i) => [ma.held > -90 ? bandsA[i] : null, refOn && mb.held > -90 ? bandsB[i] : null]) };
      }
      raf = requestAnimationFrame(draw);
    };
    draw();
    return () => cancelAnimationFrame(raf);
  });

  const db = (v) => (v === null || v === undefined || !isFinite(v) ? '—' : `${v.toFixed(1)} dB`);
  const diff = (a, b) => (a === null || b === null ? '—' : `${b - a >= 0 ? '+' : ''}${(b - a).toFixed(1)} dB`);
  const LEVELS = [['Peak', (x) => db(x?.peak)], ['RMS', (x) => db(x?.rms)], ['Crest factor', (x) => db(x?.crest)], ['Headroom', (x) => db(x ? -x.peak : null)]];
</script>

<div class="atools">
  <Seg items={[{ id: 'live', label: 'Live', title: 'peaks, falling' }, { id: 'avg', label: 'Average', title: 'over about two seconds: to compare' }]}
       value={mode} onpick={(v) => (mode = v)} label="Spectrum" />
  <span class="legend"><i class="sw syn"></i>Synth</span>
  {#if s.ref}
    <span class="legend"><i class="sw ref"></i>{s.ref.name}{#if !playing}<span class="muted"> · not playing: play the clip, or hear it (Ref)</span>{/if}</span>
    <span class="spacer"></span>
    <button class="tool" onclick={s.toggleRef} aria-pressed={s.refListen} title="Which one is heard (the ` key too); both are measured">Hearing: {s.refListen ? 'reference' : 'synth'}</button>
    <button class="tool" onclick={s.matchRef} title="Bring the reference to the synth's level (play the synth meanwhile)">Match level</button>
  {:else}<span class="muted hint0">No reference: Ref… in the top bar loads a recording to compare with.</span>{/if}
</div>

<Section icon="analyse" title="Spectrum" note="20 Hz – 20 kHz · dBFS"><div class="panel big"><canvas bind:this={spec}></canvas></div></Section>
<div class="cols">
  <Section icon="shape" title="Waveform" note="synth · automatic gain"><div class="panel"><canvas bind:this={wave}></canvas></div></Section>
  <Section icon="volume" title="Levels">
    <table class="dt nums"><thead><tr><th></th><th>Synth</th>{#if s.ref}<th>Reference</th>{/if}</tr></thead>
      <tbody>{#each LEVELS as [name, f]}<tr><td>{name}</td><td class="mono">{f(read.synth)}</td>{#if s.ref}<td class="mono">{f(read.ref)}</td>{/if}</tr>{/each}</tbody></table>
  </Section>
</div>
<Section icon="sliders" title="Bands" note={s.ref ? 'difference: the reference minus the synth' : ''}>
  <table class="dt nums"><thead><tr><th>Band</th><th>Range</th><th>Synth</th>{#if s.ref}<th>Reference</th><th>Difference</th>{/if}</tr></thead>
    <tbody>{#each BANDS as [name, lo, hi], i}
      {@const [a, b] = read.bands[i]}
      <tr><td>{name}</td><td class="muted mono">{lo >= 1000 ? lo / 1000 + 'k' : lo}–{hi >= 1000 ? hi / 1000 + 'k' : hi} Hz</td><td class="mono">{db(a)}</td>
        {#if s.ref}<td class="mono">{db(b)}</td><td class="mono" class:up={a !== null && b !== null && b - a > 3} class:down={a !== null && b !== null && b - a < -3}>{diff(a, b)}</td>{/if}</tr>
    {/each}</tbody></table>
</Section>

<style>
  .atools { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; margin: 4px 0 14px; font-size: 12px; }
  .legend { display: inline-flex; align-items: center; gap: 6px; }
  .sw { display: inline-block; width: 14px; height: 3px; border-radius: 2px; }
  .sw.syn { background: var(--accent); }
  .sw.ref { background: var(--warn); }
  .spacer { flex: 1; }
  .hint0 { font-size: 11.5px; }
  .panel { height: 120px; border: 1px solid var(--line); border-radius: 8px; background: var(--surface); overflow: hidden; }
  .panel.big { height: 280px; }
  .panel canvas { display: block; width: 100%; height: 100%; }
  .cols { display: grid; grid-template-columns: minmax(0, 1.4fr) minmax(0, 1fr); gap: 20px; }
  .nums td.mono { text-align: right; }
  .nums th:not(:first-child) { text-align: right; }
  .up { color: var(--warn); }
  .down { color: var(--accent); }
  @media (max-width: 900px) { .cols { grid-template-columns: 1fr; } }
</style>
