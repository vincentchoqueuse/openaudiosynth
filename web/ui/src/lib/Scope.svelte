<!-- Scope.svelte — the output, from a Web Audio AnalyserNode, like the board's audio view: waveform triggered
     on a rising zero crossing with an automatic gain (a quiet sound still fills the panel), log-frequency
     spectrum 30 Hz .. 20 kHz with falling peaks and a grid; one at a time (`view`), at full width. It also measures the level (peak with hold) for the
     meter in the transport (session.meter). -->
<script>
  import { session } from './session.svelte.js';
  let { analyser, view = 'wave' } = $props(); // 'wave' or 'spectrum': the one shown, at full width
  let wave, spec;
  const DB_FLOOR = -90;

  $effect(() => {
    if (!analyser) return;
    const time = new Float32Array(analyser.fftSize);
    const freq = new Float32Array(analyser.frequencyBinCount);
    const sr = analyser.context.sampleRate;
    let raf, gain = 4, shown = null, held = -Infinity;
    const size = (c) => {
      const dpr = devicePixelRatio;
      const W = Math.round(c.clientWidth * dpr), H = Math.round(c.clientHeight * dpr);
      if (c.width !== W || c.height !== H) (c.width = W), (c.height = H);
      return [c.getContext('2d'), W, H, dpr];
    };
    const draw = () => {
      const css = getComputedStyle(wave);
      const accent = css.getPropertyValue('--accent'), grid = css.getPropertyValue('--line'), dim = css.getPropertyValue('--muted');
      analyser.getFloatTimeDomainData(time);
      analyser.getFloatFrequencyData(freq);

      // waveform: 20 ms after the last rising zero crossing that leaves room
      {
        const [ctx, W, H, dpr] = size(wave);
        ctx.clearRect(0, 0, W, H);
        const span = 960;
        let t0 = time.length - span - 1;
        for (let i = time.length - span - 1; i > time.length / 2; i--) if (time[i - 1] < 0 && time[i] >= 0) { t0 = i; break; }
        let m = 1e-6;
        for (let i = 0; i < span; i++) m = Math.max(m, Math.abs(time[t0 + i]));
        const target = Math.min(32, Math.max(1, 0.8 / m));
        gain += (target - gain) * (target < gain ? 0.5 : 0.1); // quick to shrink, slow to grow
        ctx.strokeStyle = grid;
        ctx.lineWidth = dpr;
        ctx.beginPath(); ctx.moveTo(0, H / 2); ctx.lineTo(W, H / 2); ctx.stroke();
        ctx.strokeStyle = accent;
        ctx.lineWidth = 1.5 * dpr;
        ctx.lineJoin = 'round';
        ctx.beginPath();
        for (let i = 0; i < span; i++) {
          const x = (i / (span - 1)) * W, y = H / 2 - time[t0 + i] * gain * H * 0.45;
          i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        }
        ctx.stroke();
        // level: peak of the whole buffer, held then falling
        let pk = 0;
        for (const v of time) pk = Math.max(pk, Math.abs(v));
        const db = 20 * Math.log10(pk + 1e-9);
        held = Math.max(db, held - 0.5);
        session.meter = { level: Math.max(0, Math.min(1, (db - DB_FLOOR) / -DB_FLOOR)), hold: held };
      }

      // spectrum on a log axis, peaks falling 1.2 dB per frame
      {
        const [ctx, W, H, dpr] = size(spec);
        ctx.clearRect(0, 0, W, H);
        const lo = 30, hi = 20000, ls = Math.log(hi / lo);
        ctx.strokeStyle = grid;
        ctx.fillStyle = dim;
        ctx.lineWidth = dpr;
        ctx.font = `${10 * dpr}px ui-monospace, monospace`;
        for (const [f, t] of [[100, '100'], [1000, '1k'], [10000, '10k']]) {
          const x = (W * Math.log(f / lo)) / ls;
          ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
          ctx.fillText(t, x + 4 * dpr, H - 4 * dpr);
        }
        if (!shown || shown.length !== W) shown = new Float32Array(W).fill(DB_FLOOR);
        ctx.beginPath();
        ctx.moveTo(0, H);
        for (let x = 0; x < W; x++) {
          const f0 = lo * Math.exp((ls * x) / W), f1 = lo * Math.exp((ls * (x + 1)) / W);
          const b0 = Math.floor((f0 / sr) * analyser.fftSize), b1 = Math.max(b0 + 1, Math.floor((f1 / sr) * analyser.fftSize));
          let v = -Infinity;
          if (b1 > b0 + 1) for (let b = b0; b < b1 && b < freq.length; b++) v = Math.max(v, freq[b]);
          else { // narrower than a bin (low frequencies): interpolate between the two nearest bins, no stairs
            const fb = (0.5 * (f0 + f1) * analyser.fftSize) / sr, k = Math.floor(fb), t = fb - k;
            v = (freq[k] ?? DB_FLOOR) * (1 - t) + (freq[k + 1] ?? DB_FLOOR) * t;
          }
          shown[x] = Math.max(Math.max(DB_FLOOR, Math.min(0, v)), shown[x] - 1.2);
          ctx.lineTo(x, H * (shown[x] / DB_FLOOR));
        }
        ctx.lineTo(W, H);
        ctx.closePath();
        ctx.fillStyle = css.getPropertyValue('--accent-soft');
        ctx.fill();
        ctx.strokeStyle = accent;
        ctx.lineWidth = 1.25 * dpr;
        ctx.stroke();
      }
      raf = requestAnimationFrame(draw);
    };
    draw();
    return () => cancelAnimationFrame(raf);
  });
</script>

<!-- both stay drawn (the level is measured either way); the one not chosen is hidden -->
<div class="monitor">
  <div class="panel" hidden={view !== 'wave'}><canvas bind:this={wave}></canvas></div>
  <div class="panel" hidden={view !== 'spectrum'}><canvas bind:this={spec}></canvas></div>
</div>
