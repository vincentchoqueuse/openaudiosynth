<!-- Sequencer.svelte — step sequencer for the current sound: a grid of scale notes x steps, each cell off / on /
     accent / soft / tie (held from the previous step of the same row), tempo, step length, gate and swing.
     Each row becomes one phrase in the audition syntax, played in a loop by the worklet's sample-accurate player;
     edits while playing take over in place, without restarting the loop. Space plays / stops. The sequence is
     kept in this browser (autosave and named patterns) and exports as a self-contained song (songs/ format). -->
<script>
  import { onMount, onDestroy, untrack } from 'svelte';
  import { keyIsForControl } from './keys.js';
  import { NAMES, SCALES, noteName, scaleRows, phrases, buildSong } from './seqExport.js';
  // active: its tab is shown (Space is its own only then, or while it plays); playing is bound by the app, which
  // keeps this component mounted on every tab so that the loop goes on while the sound is edited; patchGen
  // counts the patch loads and engine changes (they reset the synth's tempo, sent again here)
  let { synth, setTempo, getPatch, active = true, playing = $bindable(false), patchGen = 0 } = $props();
  const STEP_LENGTHS = [['1/32', 0.125], ['1/16', 0.25], ['1/8', 0.5], ['1/4', 1], ['1/2', 2], ['1 bar', 4]];
  const STEP_COUNTS = [8, 12, 16, 24, 32], OCTAVES = [1, 2, 3, 4, 5], SPANS = [1, 2, 3], LOOPS = [1, 2, 4, 8, 16];

  let bpm = $state(80), steps = $state(16), stepLen = $state(0.5), gate = $state(0.85), swing = $state(0);
  let root = $state(4), scale = $state('minor'), octave = $state(3), span = $state(2);
  let cells = $state({}); // "note:step" -> 'on' | 'acc' | 'soft' | 'tie'
  let playStep = $state(-1), message = $state('');
  let paint = null; // value applied while dragging across cells

  let loops = $state(4), patternName = $state(''), patterns = $state({});
  const name = noteName;
  const rows = $derived(scaleRows(root, scale, octave, span)); // high notes on top
  const get = (n, s) => cells[`${n}:${s}`];
  const phraseRows = () => phrases(cells, rows, steps).join('\n'); // what you see is what plays
  // the tempo actually used everywhere (player, playhead, autosave, export): the field may be empty or absurd
  const tempo = $derived(Math.min(300, Math.max(20, +bpm || 120)));

  // ---- persistence (this browser only): the current sequence, and named patterns ----
  const KEY = 'synth.sequencer.v1';
  const snapshot = () => ({ bpm: tempo, steps, stepLen, gate, swing, root, scale, octave, span, loops, cells });
  // stored data may be old or damaged: each field is taken only if it is one of the values the controls offer
  function restore(st) {
    if (!st || typeof st !== 'object') return;
    const num = (v, lo, hi, cur) => (typeof v === 'number' && v >= lo && v <= hi ? v : cur);
    const one = (v, list, cur) => (list.includes(v) ? v : cur);
    bpm = num(st.bpm, 20, 300, bpm);
    steps = one(st.steps, STEP_COUNTS, steps);
    stepLen = one(st.stepLen, STEP_LENGTHS.map((l) => l[1]), stepLen);
    gate = num(st.gate, 0.05, 1, gate);
    swing = num(st.swing, 0, 0.5, swing);
    root = one(st.root, [...NAMES.keys()], root);
    scale = one(st.scale, Object.keys(SCALES), scale);
    octave = one(st.octave, OCTAVES, octave);
    span = one(st.span, SPANS, span);
    loops = one(st.loops, LOOPS, loops);
    const ok = {};
    for (const [k, v] of Object.entries(st.cells && typeof st.cells === 'object' ? st.cells : {}))
      if (/^\d+:\d+$/.test(k) && ['on', 'acc', 'soft', 'tie'].includes(v)) ok[k] = v;
    cells = ok;
  }
  function load() { try { return JSON.parse(localStorage.getItem(KEY)) ?? {}; } catch { return {}; } }
  function store(data) { try { localStorage.setItem(KEY, JSON.stringify(data)); } catch { /* storage unavailable */ } }
  let ready = $state(false); // no autosave before the stored sequence is back
  onMount(() => { const d = load(); patterns = d.patterns ?? {}; restore(d.current); ready = true; });
  $effect(() => { const cur = $state.snapshot(snapshot()); if (ready) store({ current: cur, patterns: $state.snapshot(patterns) }); });
  function savePattern() {
    const n = (patternName || prompt('Pattern name') || '').trim();
    if (!n) return;
    patterns = { ...patterns, [n]: $state.snapshot(snapshot()) };
    patternName = n;
    message = `Saved "${n}" in this browser`;
  }
  function loadPattern(n) { if (patterns[n]) { restore(patterns[n]); patternName = n; changed(); } }
  function deletePattern() {
    if (!patternName || !patterns[patternName]) return;
    const { [patternName]: _, ...rest } = patterns;
    patterns = rest;
    message = `Deleted "${patternName}"`;
    patternName = '';
  }

  // ---- export: a song with the current sound inline, one track per row ----
  function exportSong() {
    const patch = getPatch();
    const song = buildSong({ name: (patternName || patch.name + '_sequence').replace(/\W+/g, '_'), bpm: tempo, steps,
      stepLen, gate, swing, cells, notes: rows, loops }, patch);
    if (!song.tracks.length) { message = 'Nothing to export: the grid is empty'; return; }
    const text = JSON.stringify(song, null, 2) + '\n';
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = song.name + '.json';
    a.click();
    message = `Exported ${song.name}.json: put it in songs/, then synth_render --song songs/${song.name}.json --out x.wav`;
  }

  let silent = false; // the grid sent last was empty: the player stops by itself, the transport stays on
  async function send() {
    setTempo?.(tempo);
    const rowsText = phraseRows();
    silent = !rowsText;
    const r = await synth.sequence(rowsText, tempo, stepLen, gate, swing, true);
    message = r.length < 0 ? r.error : '';
  }
  let pending = null;
  function changed() { // debounced resend while playing: the loop keeps its position
    if (!playing) return;
    clearTimeout(pending);
    pending = setTimeout(send, 25);
  }
  function play() { playing = true; send(); }
  function stop() { clearTimeout(pending); playing = false; playStep = -1; synth.stop(); }

  function setCell(n, s, v) {
    const k = `${n}:${s}`;
    if (v) cells[k] = v; else delete cells[k];
    cells = { ...cells };
    changed();
  }
  function cycle(c) { return !c ? 'on' : c === 'on' ? 'acc' : c === 'acc' ? 'soft' : null; }
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
  function copy() {
    navigator.clipboard?.writeText(phraseRows()).catch(() => {});
    message = 'Rows copied (one phrase per line, the audition syntax of patches and songs)';
  }

  onMount(() => synth.on('pos', (d) => { // the returned unsubscribe runs on destroy
    if (!playing) return;
    if (d.frames < 0) { // the player was stopped from elsewhere (engine or patch change): follow it
      if (!silent) { playing = false; playStep = -1; }
      return;
    }
    const t = d.frames / d.sampleRate, sd = (60 / tempo) * stepLen;
    playStep = Math.floor(t / sd) % steps;
  }));
  onDestroy(() => { if (playing) stop(); }); // the app going away (it stays mounted across tabs)
  // a new sound under a running sequence: the synth was re-initialized, its tempo (synced delay, LFOs) comes back
  $effect(() => { patchGen; untrack(() => { if (playing) setTempo?.(tempo); }); });

  function key(e) {
    const inLibrary = e.target?.closest?.('.library nav'); // there Space is the transport's, not the list's
    if ((keyIsForControl(e) && !inLibrary) || (!active && !playing)) return;
    if (e.code === 'Space') { e.preventDefault(); playing ? stop() : play(); }
  }
  // any change of what is played is resent while playing; `changed` reads `playing`, which must not be a
  // dependency (pressing Play would send the sequence a second time and cut its first notes)
  $effect(() => { tempo; stepLen; gate; swing; steps; rows; untrack(changed); });
</script>

<svelte:window onkeydown={key} />

<div class="seq">
  <div class="seq-bar">
    <button class="play" class:on={playing} onclick={() => (playing ? stop() : play())} aria-label={playing ? 'Stop' : 'Play'}>{playing ? '■' : '▶'}</button>
    <label>BPM <input type="number" min="20" max="300" bind:value={bpm} /></label>
    <label>Steps <select bind:value={steps}>{#each STEP_COUNTS as n}<option value={n}>{n}</option>{/each}</select></label>
    <label>Step <select bind:value={stepLen}>{#each STEP_LENGTHS as [l, v]}<option value={v}>{l}</option>{/each}</select></label>
    <label>Gate <input type="range" min="0.05" max="1" step="0.01" bind:value={gate} style="--p:{gate * 100}%" /></label>
    <label>Swing <input type="range" min="0" max="0.5" step="0.01" bind:value={swing} style="--p:{swing * 200}%" /></label>
  </div>
  <div class="seq-bar">
    <label>Root <select bind:value={root}>{#each NAMES as n, i}<option value={i}>{n}</option>{/each}</select></label>
    <label>Scale <select bind:value={scale}>{#each Object.keys(SCALES) as s}<option value={s}>{s}</option>{/each}</select></label>
    <label>Octave <select bind:value={octave}>{#each OCTAVES as o}<option value={o}>{o}</option>{/each}</select></label>
    <label>Range <select bind:value={span}>{#each SPANS as o}<option value={o}>{o} oct</option>{/each}</select></label>
    <span class="spacer"></span>
    <button class="ghost" onclick={random}>Random</button>
    <button class="ghost" onclick={clear}>Clear</button>
    <button class="ghost" onclick={copy}>Copy phrase</button>
  </div>
  <div class="seq-bar">
    <label>Pattern
      <select value={patternName} onchange={(e) => loadPattern(e.target.value)}>
        <option value="">—</option>
        {#each Object.keys(patterns).sort() as n}<option value={n}>{n}</option>{/each}
      </select>
    </label>
    <input class="pname" placeholder="name" bind:value={patternName} />
    <button class="ghost" onclick={savePattern}>Save</button>
    <button class="ghost" onclick={deletePattern} disabled={!patterns[patternName]}>Delete</button>
    <span class="spacer"></span>
    <label>Loops <select bind:value={loops}>{#each LOOPS as n}<option value={n}>{n}</option>{/each}</select></label>
    <button class="ghost" onclick={exportSong}>Export song</button>
  </div>
  {#if message}<p class="message">{message}</p>{/if}

  <div class="grid" style="--steps:{steps}" oncontextmenu={(e) => e.preventDefault()} role="grid" tabindex="0" aria-label="Step grid">
    {#each rows as n (n)}
      <div class="rowlabel" class:root={n % 12 === root}>{name(n)}</div>
      {#each Array(steps) as _, s}
        {@const c = get(n, s)}
        <div class="cell {c ?? ''}" class:beat={s % 4 === 0} class:now={s === playStep}
             onpointerdown={(e) => down(e, n, s)} onpointerenter={(e) => enter(e, n, s)} role="gridcell" tabindex="-1" aria-selected={!!c}></div>
      {/each}
    {/each}
  </div>
  <p class="muted hint">Click cycles note → accent → soft → off · right-click: off · Shift-click: hold from the previous step · drag to paint · Space: play / stop</p>
</div>
