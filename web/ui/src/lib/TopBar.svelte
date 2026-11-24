<!-- TopBar.svelte — the one bar above everything. On the left: the Menu (Save to the library in development,
     Export sound, Export song, Revert, Share, Undo / Redo, Shortcuts, Report a bug: a new GitHub
     issue from .github/ISSUE_TEMPLATE/web_bug.md, titled [web]); the three views (Sequence, Synth, Code), pills of
     one width with their icon; the sound: Previous / Next through the library, its name, a dot when settings have
     moved since it was loaded (a click on it reverts), and A/B to hear it as loaded, then the edit again. On the right, each set apart by a
     rule: the CPU load and the output level (Meter), the listening volume, the tempo, and play / stop the
     sequence (Space). -->
<script>
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { REPO_URL } from './links.js';
  import Pills from './Pills.svelte';
  import Menu from './Menu.svelte';
  import Meter from './Meter.svelte';
  let { view = $bindable(), views } = $props();
  // another sound, or a setting moved while the loaded sound is heard: the edit that waited is dropped
  $effect(() => { s.patchGen; s.edited = null; });
  $effect(() => { if (s.edited && s.changedCount) s.edited = null; });
  const items = $derived([
    ...(s.canSave ? [{ label: 'Save to library…', run: s.openSave }] : []),
    { label: 'Export sound (JSON)', run: s.exportPatch },
    { label: 'Export song', run: q.exportSong },
    { label: s.info ? 'Revert to saved' : 'Back to defaults', run: s.revert, disabled: !s.changedCount },
    { label: 'Share (QR code)…', run: () => (s.shareOpen = true) },
    '-',
    { label: 'Undo', kbd: '⌘Z', run: h.undo, disabled: !h.canUndo },
    { label: 'Redo', kbd: '⇧⌘Z', run: h.redo, disabled: !h.canRedo },
    '-',
    ...(s.phone ? [] : [{ label: 'Shortcuts', kbd: '?', run: () => (s.shortcutsOpen = true) }]),
    { label: 'Report a bug', kbd: '↗', href: `${REPO_URL}/issues/new?template=web_bug.md&title=%5Bweb%5D%20&labels=bug,web` },
  ]);
</script>

<header class="topbar">
  <div class="tb-left">
  <Menu label="Menu" icon="menu" {items} />
  <span class="vsep" aria-hidden="true"></span>
  {#if views.length > 1}<Pills items={views.map(({ id, label, icon }) => ({ id, label, icon }))} value={view} onpick={(id) => (view = id)} size="lg" tabs label="Views" />{/if}
  <span class="vsep" aria-hidden="true"></span>
  <div class="namebox">
    <span class="tag">Sound</span>
    <button class="step" onclick={() => s.step(-1)} disabled={!s.patches.length} aria-label="Previous sound" title="Previous sound of the library">‹</button>
    <button class="step" onclick={() => s.step(1)} disabled={!s.patches.length} aria-label="Next sound" title="Next sound of the library">›</button>
    {#key s.picks}<span class="name" class:flash={s.picks > 0} title={s.title}>{s.label}</span>{/key}
    {#if s.changedCount}<button class="moved" onclick={s.revert} title="{s.changedCount} setting{s.changedCount > 1 ? 's' : ''} moved since it was loaded: click to revert (Undo brings the edit back)" aria-label="Edited: revert to the sound as loaded"></button>{/if}
    <button class="ab" class:on={!!s.edited} disabled={!s.canCompare} onclick={s.compare} aria-pressed={!!s.edited}
            title={s.edited ? 'Hearing the sound as loaded: click for your edit' : 'Hear the sound as loaded, then your edit again'}>{s.edited ? 'B · loaded' : 'A/B'}</button>
  </div>
  </div>
  <div class="tb-right">
  <Meter label="CPU" value={s.cpu} text="{Math.round(s.cpu * 100)} %" hot={s.cpu > 0.7}
         title="CPU: the share of the audio's time spent computing it (in this browser)" />
  <span class="vsep" aria-hidden="true"></span>
  <Meter label="Out" value={s.meter.level} text={isFinite(s.meter.hold) && s.meter.hold > -90 ? `${s.meter.hold.toFixed(0)} dB` : '−∞'}
         hot={s.meter.hold > -1} title="Output peak (dBFS), before the volume" />
  <span class="vsep" aria-hidden="true"></span>
  <label class="volume" title="Listening volume (double-click: 0 dB)">
    <span aria-hidden="true">Vol</span>
    <input type="range" min="-60" max="0" step="0.5" value={s.volume} oninput={(e) => s.setVolume(+e.target.value)}
           ondblclick={() => s.setVolume(0)} style="--p:{((s.volume + 60) / 60) * 100}%" aria-label="Volume" />
    <span class="mono db">{s.volume <= -60 ? 'off' : `${s.volume.toFixed(1)} dB`}</span>
  </label>
  <span class="vsep" aria-hidden="true"></span>
  <label class="tempo" title="Tempo of the clip and of everything synced (LFOs, delay)"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /> BPM</label>
  <span class="seqlabel">Clip</span>
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Play / stop the clip (Space)">{q.playing ? '■' : '▶'}</button>
  </div>
</header>
