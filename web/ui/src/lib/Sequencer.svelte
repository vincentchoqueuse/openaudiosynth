<!-- Sequencer.svelte — the Sequence panel, on the right of the sound: a step sequence played with the current sound,
     shown two ways, the same content: a Grid (scale notes x steps; cells note / accent / soft / held) and its JSON
     (toDoc / fromDoc in seqExport.js: tempo, step, key, and one line per row, "x..X o_.. ...."), editable and
     applied with Apply. Each row is one phrase of the worklet's sample-accurate player; edits while playing take
     over in place. It keeps playing while the sound is edited. Sequences are kept in this browser (the current one
     autosaved, named ones on Save) and export as a self-contained song (songs/ format). -->
<script>
  import { onMount, onDestroy, untrack } from 'svelte';
  import { keyIsForControl } from './keys.js';
  import { NAMES, SCALES, STEP_LENGTHS, STEP_COUNTS, noteName, scaleRows, phrases, buildSong, toDoc, fromDoc } from './seqExport.js';
  const editor = import('./CodeEditor.svelte'); // the code editor stays out of the main bundle
  // playing: bound by the app (the audition waits while it plays); patchGen counts the patch loads and engine
  // changes (they reset the synth's tempo, sent again here)
  let { synth, setTempo, getPatch, playing = $bindable(false), patchGen = 0 } = $props();
  const OCTAVES = [1, 2, 3, 4, 5], SPANS = [1, 2, 3], LOOPS = [1, 2, 4, 8, 16];

  let bpm = $state(80), steps = $state(16), stepLen = $state(0.5), gate = $state(0.85), swing = $state(0);
  let root = $state(4), scale = $state('minor'), octave = $state(3), span = $state(2);
  let cells = $state({}); // "note:step" -> 'on' | 'acc' | 'soft' | 'tie'
  let name = $state(''), patterns = $state({}), loops = $state(4);
  let view = $state('grid'), playStep = $state(-1), message = $state('');
  let paint = null; // value applied while dragging across cells

  const rows = $derived(scaleRows(root, scale, octave, span)); // high notes on top
  const get = (n, s) => cells[`${n}:${s}`];
  const phraseRows = () => phrases(cells, rows, steps).join('\n'); // what you see is what plays
  const tempo = $derived(Math.min(300, Math.max(20, +bpm || 120))); // the field may be empty or absurd
  const snap = () => ({ name, bpm: tempo, steps, stepLen, gate, swing, root, scale, octave, span, cells });
  function take(s) { ({ bpm, steps, stepLen, gate, swing, root, scale, octave, span, cells } = s); }

  // ---- persistence (this browser only) ----
  const KEY = 'synth.sequencer.v1';
  function restore(st) { // stored data goes through fromDoc's checks, or is left out
    if (!st || typeof st !== 'object') return;
    try {
      take(fromDoc(toDoc({ ...snap(), ...st, cells: st.cells && typeof st.cells === 'object' ? st.cells : {} })));
      name = typeof st.name === 'string' ? st.name : '';
      if (LOOPS.includes(st.loops)) loops = st.loops;
    } catch { /* damaged: keep what is there */ }
  }
  function load() { try { return JSON.parse(localStorage.getItem(KEY)) ?? {}; } catch { return {}; } }
  function store(data) { try { localStorage.setItem(KEY, JSON.stringify(data)); } catch { /* storage unavailable */ } }
  let ready = $state(false); // no autosave before the stored sequence is back
  onMount(() => { const d = load(); patterns = d.patterns ?? {}; restore(d.current); ready = true; });
  $effect(() => { const cur = $state.snapshot({ ...snap(), loops }); if (ready) store({ current: cur, patterns: $state.snapshot(patterns) }); });

  function pick(n) { if (patterns[n]) { restore(patterns[n]); name = n; message = ''; changed(); } }
  function fresh() {
    cells = {};
    name = '';
    message = 'New sequence: draw it in the grid or write it in JSON, then Save';
    changed();
  }
  function save() {
    const n = (name || prompt('Name of the sequence') || '').trim();
    if (!n) return;
    name = n;
    patterns = { ...patterns, [n]: $state.snapshot({ ...snap(), loops }) };
    message = `Saved "${n}" in this browser`;
  }
  function remove() {
    if (!patterns[name]) return;
    const { [name]: _, ...rest } = patterns;
    patterns = rest;
    message = `Deleted "${name}"`;
    name = '';
  }

  // ---- export: a song with the current sound inline, one track per row ----
  function exportSong() {
    const patch = getPatch();
    const song = buildSong({ name: (name || patch.name + '_sequence').replace(/\W+/g, '_'), bpm: tempo, steps, stepLen,
      gate, swing, cells, notes: rows, loops }, patch);
    if (!song.tracks.length) { message = 'Nothing to export: the sequence is empty'; return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(song, null, 2) + '\n'], { type: 'application/json' }));
    a.download = song.name + '.json';
    a.click();
    message = `Exported ${song.name}.json (songs/ format)`;
  }

  // ---- playing ----
  let silent = false; // the sequence sent last was empty: the player stops by itself, the transport stays on
  async function send() {
    setTempo?.(tempo);
    const rowsText = phraseRows();
    silent = !rowsText;
    const r = await synth.sequence(rowsText, tempo, stepLen, gate, swing, true);
    if (r.length < 0) message = r.error;
  }
  let pending = null;
  function changed() { // debounced resend while playing: the loop keeps its position
    if (!playing) return;
    clearTimeout(pending);
    pending = setTimeout(send, 25);
  }
  function play() { playing = true; send(); }
  function stop() { clearTimeout(pending); playing = false; playStep = -1; synth.stop(); }

  // ---- grid ----
  function setCell(n, s, v) {
    const k = `${n}:${s}`;
    if (v) cells[k] = v; else delete cells[k];
    cells = { ...cells };
    changed();
  }
  const cycle = (c) => (!c ? 'on' : c === 'on' ? 'acc' : c === 'acc' ? 'soft' : null);
  function down(e, n, s) {
    e.preventDefault();
    const c = get(n, s);
    paint = e.shiftKey ? (c === 'tie' ? null : 'tie') : e.button === 2 ? null : cycle(c);
    setCell(n, s, paint);
  }
  function enter(e, n, s) { if (paint !== null && e.buttons) setCell(n, s, paint); else if (e.buttons === 0) paint = null; }
  function clear() { cells = {}; changed(); }
  function random() {
    const next = {}, pool = rows.slice(Math.floor(rows.length / 3));
    for (let s = 0; s < steps; s++) {
      if (Math.random() > 0.3) continue;
      const n = pool[Math.floor(Math.random() * pool.length)];
      next[`${n}:${s}`] = Math.random() < 0.2 ? 'acc' : 'on';
      const hold = Math.floor(Math.random() * 3);
      for (let k = 1; k <= hold && s + k < steps; k++) next[`${n}:${s + k}`] = 'tie';
    }
    cells = next;
    changed();
  }

  // ---- JSON: the same sequence as text; Apply reads it back (fromDoc says what is wrong) ----
  let text = $state(''), shown = $state(''), jsonError = $state('');
  const edited = $derived(text !== shown);
  // the text follows the grid until it is typed in
  $effect(() => {
    const t = JSON.stringify(toDoc(snap()), null, 2) + '\n';
    untrack(() => { if (text === shown) text = t; shown = t; });
  });
  function apply() {
    try {
      const s = fromDoc(JSON.parse(text));
      take(s);
      name = s.name === 'untitled' ? '' : s.name;
      jsonError = '';
      message = 'Applied';
      changed();
      text = shown; // the grid's text again (normalized)
    } catch (e) {
      jsonError = e.message;
    }
  }
  function revert() { jsonError = ''; text = shown; }

  onMount(() => synth.on('pos', (d) => { // the returned unsubscribe runs on destroy
    if (!playing) return;
    if (d.frames < 0) { // the player was stopped from elsewhere: follow it
      if (!silent) { playing = false; playStep = -1; }
      return;
    }
    const t = d.frames / d.sampleRate, sd = (60 / tempo) * stepLen;
    playStep = Math.floor(t / sd) % steps;
  }));
  onDestroy(() => { if (playing) stop(); });
  $effect(() => { patchGen; untrack(() => { if (playing) setTempo?.(tempo); }); });

  // Space: play / stop while it plays (from anywhere), or from inside this panel
  function key(e) {
    if (e.code !== 'Space') return;
    const here = e.target?.closest?.('.seqpanel'), inLibrary = e.target?.closest?.('.library nav');
    if (!playing && !here) return;
    if (keyIsForControl(e) && !inLibrary && !e.target?.closest?.('.grid')) return;
    e.preventDefault();
    playing ? stop() : play();
  }
  // any change of what is played is resent while playing (`changed` reads `playing`, not a dependency)
  $effect(() => { tempo; stepLen; gate; swing; steps; rows; untrack(changed); });
</script>

<svelte:window onkeydown={key} />

<div class="seq">
  <div class="sp-head">
    <button class="play" class:on={playing} onclick={() => (playing ? stop() : play())} aria-label={playing ? 'Stop' : 'Play'}
            title="Space">{playing ? '■' : '▶'}</button>
    <div class="sp-title">
      <select value={patterns[name] ? name : ''} onchange={(e) => pick(e.target.value)} aria-label="Saved sequences">
        <option value="">{name ? name + (patterns[name] ? '' : ' (not saved)') : 'untitled'}</option>
        {#each Object.keys(patterns).sort() as n}<option value={n}>{n}</option>{/each}
      </select>
      <span class="muted">{tempo} bpm · {steps} steps of {STEP_LENGTHS.find((l) => l[1] === stepLen)?.[0]} · {NAMES[root]} {scale}</span>
    </div>
  </div>
  <div class="sp-actions">
    <button class="ghost" onclick={fresh}>New</button>
    <button class="ghost" onclick={save}>Save</button>
    <button class="ghost" onclick={remove} disabled={!patterns[name]}>Delete</button>
    <span class="spacer"></span>
    <div class="seg small" role="tablist" aria-label="View">
      <button role="tab" aria-selected={view === 'grid'} class:on={view === 'grid'} onclick={() => (view = 'grid')}>Grid</button>
      <button role="tab" aria-selected={view === 'json'} class:on={view === 'json'} onclick={() => (view = 'json')}>JSON{edited ? ' •' : ''}</button>
    </div>
  </div>
  {#if message}<p class="message">{message}</p>{/if}

  {#if view === 'grid'}
    <div class="seq-bar">
      <label>BPM <input type="number" min="20" max="300" bind:value={bpm} /></label>
      <label>Step <select bind:value={stepLen}>{#each STEP_LENGTHS as [l, v]}<option value={v}>{l}</option>{/each}</select></label>
      <label>Steps <select bind:value={steps}>{#each STEP_COUNTS as n}<option value={n}>{n}</option>{/each}</select></label>
    </div>
    <div class="seq-bar">
      <label>Key <select bind:value={root}>{#each NAMES as n, i}<option value={i}>{n}</option>{/each}</select>
        <select bind:value={scale}>{#each Object.keys(SCALES) as s}<option value={s}>{s}</option>{/each}</select></label>
      <label>Oct <select bind:value={octave}>{#each OCTAVES as o}<option value={o}>{o}</option>{/each}</select></label>
      <label>Range <select bind:value={span}>{#each SPANS as o}<option value={o}>{o}</option>{/each}</select></label>
    </div>
    <div class="seq-bar">
      <label>Gate <input type="range" min="0.05" max="1" step="0.01" bind:value={gate} style="--p:{gate * 100}%" /></label>
      <label>Swing <input type="range" min="0" max="0.5" step="0.01" bind:value={swing} style="--p:{swing * 200}%" /></label>
    </div>
    <div class="grid" style="--steps:{steps}" oncontextmenu={(e) => e.preventDefault()} role="grid" tabindex="0" aria-label="Step grid">
      {#each rows as n (n)}
        <div class="rowlabel" class:root={n % 12 === root}>{noteName(n)}</div>
        {#each Array(steps) as _, s}
          {@const c = get(n, s)}
          <div class="cell {c ?? ''}" class:beat={s % 4 === 0} class:now={s === playStep}
               onpointerdown={(e) => down(e, n, s)} onpointerenter={(e) => enter(e, n, s)} role="gridcell" tabindex="-1" aria-selected={!!c}></div>
        {/each}
      {/each}
    </div>
    <div class="sp-actions">
      <button class="ghost" onclick={random}>Random</button>
      <button class="ghost" onclick={clear}>Clear</button>
      <span class="spacer"></span>
      <select bind:value={loops} aria-label="Times the sequence plays in the exported song">{#each LOOPS as n}<option value={n}>× {n}</option>{/each}</select>
      <button class="ghost" onclick={exportSong}>Export song</button>
    </div>
    <p class="muted hint">Click: note → accent → soft → off · Shift-click: hold · right-click: off · drag to paint</p>
  {:else}
    <div class="sp-json">
      {#await editor then { default: CodeEditor }}
        <CodeEditor bind:value={text} onrun={apply} />
      {/await}
    </div>
    {#if jsonError}<p class="error small">{jsonError}</p>{/if}
    <div class="sp-actions">
      <button class="btn primary" onclick={apply} disabled={!edited} title="Cmd/Ctrl+Enter">Apply</button>
      <button class="ghost" onclick={revert} disabled={!edited}>Revert</button>
    </div>
    <p class="muted hint">One line per note, high to low: <code>x</code> note, <code>X</code> accent, <code>o</code> soft,
      <code>_</code> held, <code>.</code> rest, in groups of four steps (spaces are free). <code>key</code>: a root and a scale
      ({Object.keys(SCALES).join(', ')}), from <code>octave</code> over <code>range</code> octaves; <code>step</code>:
      {STEP_LENGTHS.map((l) => l[0]).join(', ')}.</p>
  {/if}
</div>

<style>
  .sp-head { display: flex; align-items: center; gap: 12px; margin-bottom: 8px; }
  .sp-title { display: flex; flex-direction: column; gap: 2px; min-width: 0; flex: 1; }
  .sp-title select { font-size: 14px; font-weight: 600; border: 0; background: none; color: var(--text); padding: 0; max-width: 100%; }
  .sp-title .muted { font-size: 11px; }
  .sp-actions { display: flex; align-items: center; gap: 8px; margin: 8px 0; font-size: 12px; }
  .sp-actions .spacer { flex: 1; }
  .sp-actions select { border: 0; background: var(--faint); border-radius: 6px; padding: 3px 6px; color: var(--text); font-size: 12px; }
  .sp-json :global(.code) { height: 340px; } /* the code editor's own height is the sound's JSON tab's */
  .small { font-size: 11.5px; }
  .hint { font-size: 11px; line-height: 1.5; }
  .seq .grid { grid-template-columns: 2.6em repeat(var(--steps), minmax(5px, 1fr)); }
  .seq-bar { gap: 10px; margin-bottom: 6px; }
</style>
