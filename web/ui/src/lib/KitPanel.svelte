<!-- KitPanel.svelte — the sampler's kit: its 16 pads (the first 16 samples of the kit, by note) as a grid,
     a click plays one and selects it; the selected pad's waveform with its start and end points, dragged with
     the pointer (the region played is lit, an arrow tells the direction); its settings; the kit's own settings.
     Audio files dropped anywhere on the panel (or picked with the button) become a new kit. -->
<script>
  import Param from './Param.svelte';
  import { peaks, noteName } from './kit.js';

  let { kit, kitName, byName, values, setValue, synth, onfiles } = $props();
  const PADS = 16;
  const SETTINGS = ['tune', 'start', 'end', 'reverse', 'loop', 'decay', 'level', 'pan', 'cutoff', 'choke'];
  const KIT = ['volume', 'tune', 'velocity', 'resonance', 'release'];

  let sel = $state(0);
  let drag = $state(false);
  let wave = $state(null), width = $state(0);
  let input;

  const entries = $derived(kit?.entries ?? []);
  const pads = $derived(Array.from({ length: PADS }, (_, k) => entries[k] ?? null));
  const extra = $derived(entries.slice(PADS));
  // a new kit: back to its first pad if the selected one is now empty
  $effect(() => { if (kit && sel >= Math.min(PADS, kit.entries.length)) sel = 0; });
  const get = (n) => { const d = byName[n]; return d ? values[d.id] ?? d.def : 0; };
  const set = (n, v) => { const d = byName[n]; if (d) setValue(d, v); };
  const p = (k, s) => `pad${k + 1}_${s}`;

  function hit(k) {
    sel = k;
    const e = entries[k];
    if (!e) return;
    synth.noteOn(e.note, 110);
    setTimeout(() => synth.noteOff(e.note), 250);
  }

  // waveform with the region played
  $effect(() => {
    const e = entries[sel], c = wave;
    if (!c || !width) return;
    const s = get(p(sel, 'start')), en = get(p(sel, 'end')), rev = get(p(sel, 'reverse')) >= 0.5;
    const dpr = devicePixelRatio, W = Math.round(width * dpr), H = Math.round(c.clientHeight * dpr);
    c.width = W;
    c.height = H;
    const ctx = c.getContext('2d'), css = getComputedStyle(c);
    ctx.clearRect(0, 0, W, H);
    if (!e) return;
    const pk = peaks(e.pcm, W), x0 = s * W, x1 = Math.max(x0 + 1, en * W);
    ctx.fillStyle = css.getPropertyValue('--accent-soft');
    ctx.fillRect(x0, 0, x1 - x0, H);
    for (let x = 0; x < W; x++) {
      ctx.fillStyle = x >= x0 && x <= x1 ? css.getPropertyValue('--accent') : css.getPropertyValue('--track');
      const lo = pk[2 * x], hi = pk[2 * x + 1];
      ctx.fillRect(x, H / 2 - hi * H * 0.46, 1, Math.max(1, (hi - lo) * H * 0.46));
    }
    ctx.fillStyle = css.getPropertyValue('--text');
    for (const x of [x0, x1]) ctx.fillRect(Math.round(x) - dpr, 0, 2 * dpr, H);
    ctx.font = `${11 * dpr}px system-ui, sans-serif`;
    ctx.fillText(rev ? '◀ reverse' : '▶', x0 + 6 * dpr, 14 * dpr);
  });

  // dragging the nearest marker
  let grab = null;
  function down(ev) {
    if (!entries[sel]) return;
    const r = wave.getBoundingClientRect(), f = (ev.clientX - r.left) / r.width;
    const s = get(p(sel, 'start')), e = get(p(sel, 'end'));
    grab = Math.abs(f - s) <= Math.abs(f - e) ? 'start' : 'end';
    wave.setPointerCapture(ev.pointerId);
    move(ev);
  }
  function move(ev) {
    if (!grab) return;
    const r = wave.getBoundingClientRect();
    let f = Math.min(1, Math.max(0, (ev.clientX - r.left) / r.width));
    if (grab === 'start') f = Math.min(f, get(p(sel, 'end')) - 0.002);
    else f = Math.max(f, get(p(sel, 'start')) + 0.002);
    set(p(sel, grab), +f.toFixed(4));
  }
  function up() {
    if (grab) hit(sel);
    grab = null;
  }

  function dropped(ev) {
    ev.preventDefault();
    drag = false;
    if (ev.dataTransfer?.files?.length) onfiles(ev.dataTransfer.files);
  }
  const secs = (e) => (e.frames / 48000).toFixed(2) + ' s';
</script>

<section class="kitpanel" class:drag role="region" aria-label="Kit"
         ondragover={(e) => { e.preventDefault(); drag = true; }} ondragleave={() => (drag = false)} ondrop={dropped}>
  <div class="kit-head">
    <h2>Kit <span class="muted">{kitName || 'none'}{entries.length ? ` · ${entries.length} samples` : ''}</span></h2>
    <button class="btn ghost" onclick={() => input.click()}>Load samples…</button>
    <input bind:this={input} type="file" accept="audio/*,.wav,.aif,.aiff,.flac,.mp3" multiple hidden
           onchange={(e) => { if (e.target.files.length) onfiles(e.target.files); e.target.value = ''; }} />
  </div>
  {#if !entries.length}
    <p class="hint">Drop audio files here (WAV, AIFF, FLAC, MP3). A name starting with a MIDI note, like <code>36_kick.wav</code>, sets the pad's note; the others follow from C2.</p>
  {/if}

  <div class="pads">
    {#each pads as e, k}
      <button class="pad" class:on={k === sel} class:empty={!e} onclick={() => hit(k)} title={e ? `${e.name}, ${noteName(e.note)}, ${secs(e)}` : 'no sample'}>
        <span class="num">{k + 1}</span>
        <span class="pname">{e ? e.name : '—'}</span>
        {#if e}<span class="note">{noteName(e.note)}</span>{/if}
      </button>
    {/each}
  </div>

  <div class="editor">
    <canvas bind:this={wave} bind:clientWidth={width} class="wave" onpointerdown={down} onpointermove={move} onpointerup={up}
            aria-label="waveform of pad {sel + 1}: drag the start and end points"></canvas>
    <div class="settings">
      <h3>Pad {sel + 1}{entries[sel] ? ` · ${entries[sel].name}` : ''}</h3>
      {#each SETTINGS as s}
        {@const d = byName[p(sel, s)]}
        {#if d}<Param {d} label={s.charAt(0).toUpperCase() + s.slice(1)} value={values[d.id] ?? d.def} onchange={(v) => setValue(d, v)} />{/if}
      {/each}
    </div>
    <div class="settings">
      <h3>Kit</h3>
      {#each KIT as s}
        {@const d = byName[s]}
        {#if d}<Param {d} label={s.charAt(0).toUpperCase() + s.slice(1)} value={values[d.id] ?? d.def} onchange={(v) => setValue(d, v)} />{/if}
      {/each}
      {#if extra.length}
        <p class="hint">{extra.length} more sample{extra.length > 1 ? 's' : ''} ({extra.map((e) => noteName(e.note)).join(', ')}) play with the default settings.</p>
      {/if}
    </div>
  </div>
</section>

<style>
  .kitpanel { background: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 14px; display: grid; gap: 12px; }
  .kitpanel.drag { outline: 2px dashed var(--accent); outline-offset: -6px; }
  .kit-head { display: flex; align-items: center; gap: 10px; }
  .kit-head h2 { flex: 1; margin: 0; font-size: 14px; }
  .hint { margin: 0; color: var(--muted); font-size: 12.5px; }
  .pads { display: grid; grid-template-columns: repeat(8, minmax(0, 1fr)); gap: 6px; }
  .pad { display: grid; gap: 2px; text-align: left; padding: 8px; min-height: 58px; border-radius: 8px; border: 1px solid var(--line);
         background: var(--faint); color: var(--text); cursor: pointer; }
  .pad.on { border-color: var(--accent); background: var(--accent-soft); }
  .pad.empty { opacity: 0.45; }
  .pad:active { transform: translateY(1px); }
  .num { font-size: 10.5px; color: var(--muted); }
  .pname { font-size: 12.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .note { font-size: 10.5px; color: var(--muted); font-variant-numeric: tabular-nums; }
  .editor { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: 12px; }
  .wave { grid-column: 1 / -1; width: 100%; height: 110px; border-radius: 8px; background: var(--faint); cursor: ew-resize; touch-action: none; }
  .settings h3 { margin: 0 0 6px; font-size: 12.5px; color: var(--muted); font-weight: 600; }
  @media (max-width: 760px) {
    .pads { grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .editor { grid-template-columns: 1fr; }
  }
</style>
