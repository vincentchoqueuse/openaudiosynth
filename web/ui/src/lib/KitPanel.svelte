<!-- KitPanel.svelte — the sampler's kit: its 16 pads (the first 16 samples of the kit, by note) chosen above the card like the modulators,
     one per pad (a click plays it and opens it), then Kit (the kit's own settings); a pad's tab shows its
     waveform with its start and end points, dragged with the pointer (the region played is lit, an arrow tells the
     direction), and its settings.
     Audio files dropped anywhere on the panel (or picked with the button) become a new kit. -->
<script>
  import Seg from './Seg.svelte';
  import Param from './Param.svelte';
  import { peaks, noteName } from './kit.js';

  import { session as s } from './session.svelte.js';
  const kit = $derived(s.kit), synth = $derived(s.synth);
  const PADS = 16;
  const SETTINGS = ['tune', 'start', 'end', 'reverse', 'loop', 'decay', 'level', 'pan', 'cutoff', 'choke'];
  const KIT = ['volume', 'tune', 'velocity', 'resonance', 'release'];

  let sel = $state(0); // a pad (0..15) or 'kit'
  let drag = $state(false);
  let wave = $state(null), width = $state(0);
  let input;

  const entries = $derived(kit?.entries ?? []);
  const pads = $derived(Array.from({ length: PADS }, (_, k) => entries[k] ?? null));
  const extra = $derived(entries.slice(PADS));
  const padItems = $derived([...pads.map((e, k) => ({ id: k, label: e ? `${k + 1} ${e.name}` : `${k + 1}`,
    title: e ? `${e.name}, ${noteName(e.note)}, ${secs(e)}` : 'no sample' })), { id: 'kit', label: 'Kit' }]);
  // a new kit: back to its first pad if the selected one is now empty
  $effect(() => { if (kit && sel !== 'kit' && sel >= Math.min(PADS, kit.entries.length)) sel = 0; });
  const get = s.getP, set = s.setP;
  const cap = (w) => w.charAt(0).toUpperCase() + w.slice(1);
  const p = (k, s) => `pad${k + 1}_${s}`;

  function hit(k) {
    sel = k;
    const e = k === 'kit' ? null : entries[k];
    if (!e) return;
    synth.noteOn(e.note, 110);
    setTimeout(() => synth.noteOff(e.note), 250);
  }

  // waveform with the region played
  $effect(() => {
    const e = sel === 'kit' ? null : entries[sel], c = wave;
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
    if (ev.dataTransfer?.files?.length) s.dropFiles(ev.dataTransfer.files);
  }
  const secs = (e) => (e.frames / 48000).toFixed(2) + ' s';
</script>

<div class="picker">
  <div class="pads"><Seg items={padItems} value={sel} onpick={(k) => (k === 'kit' ? (sel = k) : hit(k))} label="Pads" /></div>
  <button class="btn ghost load" onclick={() => input.click()}>Load samples…</button>
  <input bind:this={input} type="file" accept="audio/*,.wav,.aif,.aiff,.flac,.mp3" multiple hidden
         onchange={(e) => { if (e.target.files.length) s.dropFiles(e.target.files); e.target.value = ''; }} />
</div>
<section class="card kitpanel" class:drag aria-label="Kit"
         ondragover={(e) => { e.preventDefault(); drag = true; }} ondragleave={() => (drag = false)} ondrop={dropped}>
  {#if !entries.length}
    <p class="hint">Drop audio files here (WAV, AIFF, FLAC, MP3). A name starting with a MIDI note, like <code>36_kick.wav</code>, sets the pad's note; the others follow from C2.</p>
  {/if}
    {#if sel === 'kit'}
      <div class="settings">
        {#each KIT as key}
          {@const d = s.byName[key]}
          {#if d}<Param {d} label={cap(key)} />{/if}
        {/each}
        {#if extra.length}
          <p class="hint">{extra.length} more sample{extra.length > 1 ? 's' : ''} ({extra.map((e) => noteName(e.note)).join(', ')}) play with the default settings.</p>
        {/if}
      </div>
    {:else}
      <p class="padinfo">{entries[sel] ? `${entries[sel].name} · ${noteName(entries[sel].note)} · ${secs(entries[sel])}` : 'No sample on this pad.'}</p>
      <canvas bind:this={wave} bind:clientWidth={width} class="wave" onpointerdown={down} onpointermove={move} onpointerup={up}
              aria-label="waveform of pad {sel + 1}: drag the start and end points"></canvas>
      <div class="settings two">
        {#each SETTINGS as key}
          {@const d = s.byName[p(sel, key)]}
          {#if d}<Param {d} label={cap(key)} />{/if}
        {/each}
      </div>
    {/if}
</section>

<style>
  .kitpanel { display: grid; gap: 12px; }
  .kitpanel.drag { outline: 2px dashed var(--accent); outline-offset: -6px; }
  .picker { display: flex; align-items: flex-start; gap: 10px; }
  .pads :global(.seg) { flex-wrap: wrap; }
  .load { margin-left: auto; flex: none; }
  .hint { margin: 0; }
  .padinfo { margin: 0 0 8px; font-size: 12px; color: var(--muted); }
  .wave { display: block; width: 100%; height: 110px; margin-bottom: 12px; border-radius: 8px; background: var(--faint); cursor: ew-resize; touch-action: none; }
  .settings.two { columns: 2; column-gap: 28px; }
  .settings { max-width: 760px; }
  @media (max-width: 760px) { .settings.two { columns: 1; } }
</style>
