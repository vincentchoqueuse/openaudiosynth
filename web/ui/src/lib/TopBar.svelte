<!-- TopBar.svelte — the one bar above everything, in three parts. Left: where you are, the three views (Sequence,
     Synth, Code), pills of one width with their icon, then Init and Randomize for the view open (the clip's notes,
     or the sound; in the Menu on a phone). Middle: what plays, the sound's name between Previous and Next
     (through the library), a dot when settings have moved since it was loaded (a click on it reverts), and A/B to
     hear it as loaded, then the edit again. Right: playing, the CPU load and the output level (Meter, on a wide
     screen), the listening volume, the tempo and play / stop (Space); last, the Menu (Save to the library in
     development, Export sound, Export song, Revert, Share, Undo / Redo, Shortcuts, Report a bug: a new GitHub issue
     from .github/ISSUE_TEMPLATE/web_bug.md, titled [web]). -->
<script>
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { REPO_URL } from './links.js';
  import Pills from './Pills.svelte';
  import Menu from './Menu.svelte';
  import Meter from './Meter.svelte';
  import Icon from './Icon.svelte';
  let { view = $bindable(), views } = $props();
  // another sound, or a setting moved while the loaded sound is heard: the edit that waited is dropped
  $effect(() => { s.patchGen; s.edited = null; });
  $effect(() => { if (s.edited && s.changedCount) s.edited = null; });
  // Init and Randomize act on the view open: the clip's notes in Sequence (cleared, or drawn at random in its scale),
  // the sound in Synth (a new sound on the same instrument, every setting at its default; or every setting of the
  // instrument thrown somewhere else). Undo brings back what they changed, but an Init of the sound is a new sound.
  function init() {
    if (view === 'clip') return q.clear();
    if (s.changedCount && !confirm('Start a new sound on this instrument? The edits of this one are not kept.')) return;
    s.newSound(s.engine.name);
  }
  const random = () => (view === 'clip' ? q.random() : s.randomize());
  const acts = $derived(view === 'clip' ? { init: 'Clear the clip', random: 'New random notes in the clip, in its scale' }
    : view === 'synth' ? { init: 'A new sound on this instrument: every setting at its default', random: 'Throw every setting of the instrument somewhere else (Undo brings the sound back)' } : null);
  const items = $derived([
    ...(s.phone && acts ? [{ label: 'Init (clear the clip)', run: init }, { label: 'Randomize the clip', run: random }, '-'] : []),
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
    {#if views.length > 1}<Pills items={views.map(({ id, label, icon }) => ({ id, label, icon }))} value={view} onpick={(id) => (view = id)} size="lg" tabs label="Views" />{/if}
    {#if acts}
      <span class="tb-acts">
        <button class="tb-act" onclick={init} title="Init: {acts.init}" aria-label="Init: {acts.init}"><Icon name="init" size={15} /></button>
        <button class="tb-act" onclick={random} title="Randomize: {acts.random}" aria-label="Randomize: {acts.random}"><Icon name="dice" size={15} /></button>
      </span>
    {/if}
  </div>
  <div class="tb-mid namebox">
    <button class="step" onclick={() => s.step(-1)} disabled={!s.patches.length} aria-label="Previous sound" title="Previous sound of the library">‹</button>
    {#key s.picks}<span class="name" class:flash={s.picks > 0} title={s.title}>{s.label}</span>{/key}
    {#if s.changedCount}<button class="moved" onclick={s.revert} title="{s.changedCount} setting{s.changedCount > 1 ? 's' : ''} moved since it was loaded: click to revert (Undo brings the edit back)" aria-label="Edited: revert to the sound as loaded"></button>{/if}
    <button class="step" onclick={() => s.step(1)} disabled={!s.patches.length} aria-label="Next sound" title="Next sound of the library">›</button>
    <button class="ab" class:on={!!s.edited} disabled={!s.canCompare} onclick={s.compare} aria-pressed={!!s.edited}
            title={s.edited ? 'Hearing the sound as loaded: click for your edit' : 'Hear the sound as loaded, then your edit again'}>{s.edited ? 'B · loaded' : 'A/B'}</button>
  </div>
  <div class="tb-right">
    <Meter label="CPU" value={s.cpu} text="{Math.round(s.cpu * 100)} %" hot={s.cpu > 0.7}
           title="CPU: the share of the audio's time spent computing it (in this browser)" />
    <Meter label="Out" value={s.meter.level} text={isFinite(s.meter.hold) && s.meter.hold > -90 ? `${s.meter.hold.toFixed(0)} dB` : '−∞'}
           hot={s.meter.hold > -1} title="Output peak (dBFS), before the volume" />
    <label class="volume" title="Listening volume: {s.volume <= -60 ? 'off' : `${s.volume.toFixed(1)} dB`} (double-click: 0 dB)">
      <Icon name="volume" size={15} />
      <input type="range" min="-60" max="0" step="0.5" value={s.volume} oninput={(e) => s.setVolume(+e.target.value)}
             ondblclick={() => s.setVolume(0)} style="--p:{((s.volume + 60) / 60) * 100}%" aria-label="Volume" />
    </label>
    <label class="tempo" title="Tempo of the clip and of everything synced (LFOs, delay)"><input type="number" min="20" max="300" bind:value={q.bpm} aria-label="Tempo" /><span class="tempo-unit">BPM</span></label>
    <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Play / stop the clip (Space)">{q.playing ? '■' : '▶'}</button>
    <span class="vsep" aria-hidden="true"></span>
    <Menu label="Menu" icon="menu" {items} align="right" compact />
  </div>
</header>
