<!-- TopBar.svelte — the one bar above everything: first the Menu, then the three views (Sequence, Synth, Code),
     pills of one width, then the preset's name; the Menu holds (Save to the library in development, Export sound,
     Export song, Revert, Share, Undo / Redo, Shortcuts, Report a bug: a new GitHub issue from .github/ISSUE_TEMPLATE/web_bug.md, titled [web]), the sound's name; on the right, each set apart by a rule: the CPU load and the output level
     (Meter), the listening volume, and
     play / stop the sequence (Space). -->
<script>
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { REPO_URL } from './links.js';
  import Pills from './Pills.svelte';
  import Icon from './Icon.svelte';
  import Meter from './Meter.svelte';
  let { view = $bindable(), views } = $props();
  let menu = $state(false);
  const act = (f) => { menu = false; f(); };
</script>

<svelte:window onpointerdown={(e) => { if (menu && !e.target.closest?.('.filemenu')) menu = false; }}
               onkeydown={(e) => { if (e.key === 'Escape') menu = false; }} />

<header class="topbar">
  <div class="tb-left">
  <div class="filemenu">
    <button class="btn ghost" onclick={() => (menu = !menu)} aria-haspopup="menu" aria-expanded={menu}><Icon name="menu" size={14} />Menu</button>
    {#if menu}
      <div class="menu" role="menu">
        {#if s.canSave}<button role="menuitem" onclick={() => act(s.openSave)}>Save to library…</button>{/if}
        <button role="menuitem" onclick={() => act(s.exportPatch)}>Export sound (JSON)</button>
        <button role="menuitem" onclick={() => act(q.exportSong)}>Export song</button>
        <button role="menuitem" disabled={!s.changedCount} onclick={() => act(s.revert)}>{s.info ? 'Revert to saved' : 'Back to defaults'}</button>
        <button role="menuitem" onclick={() => act(s.resetEngine)}>Instrument defaults</button>
        <button role="menuitem" onclick={() => act(() => (s.shareOpen = true))}>Share (QR code)…</button>
        <hr />
        <button role="menuitem" disabled={!h.canUndo} onclick={() => act(h.undo)}>Undo <kbd>⌘Z</kbd></button>
        <button role="menuitem" disabled={!h.canRedo} onclick={() => act(h.redo)}>Redo <kbd>⇧⌘Z</kbd></button>
        <hr />
        {#if !s.phone}<button role="menuitem" onclick={() => act(() => (s.shortcutsOpen = true))}>Shortcuts <kbd>?</kbd></button>{/if}
        <a role="menuitem" href="{REPO_URL}/issues/new?template=web_bug.md&title=%5Bweb%5D%20&labels=bug,web" target="_blank" rel="noopener noreferrer" onclick={() => (menu = false)}>Report a bug <kbd>↗</kbd></a>
      </div>
    {/if}
  </div>
  <span class="vsep" aria-hidden="true"></span>
  <Pills items={views.map(({ id, label }) => ({ id, label }))} value={view} onpick={(id) => (view = id)} size="lg" tabs label="Views" />
  <span class="vsep" aria-hidden="true"></span>
  <div class="namebox">
    <span class="tag">Preset</span>{#key s.picks}<span class="name" class:flash={s.picks > 0}>{s.title}</span>{/key}
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
  <span class="seqlabel">Sequencer</span>
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Play / stop the clip (Space)">{q.playing ? '■' : '▶'}</button>
  </div>
</header>
