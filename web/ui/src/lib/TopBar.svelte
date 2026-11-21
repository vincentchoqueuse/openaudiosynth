<!-- TopBar.svelte — the one bar above everything: the File menu (Save to the library in development, Export sound,
     Export song, Revert, Share, Undo / Redo, Shortcuts), the sound's name; on the right the output level, the listening volume,
     play / stop the sequence (Space), then the three views (Sequence, Synth, Code), pills of one width. -->
<script>
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  let { view = $bindable(), views } = $props();
  let menu = $state(false);
  const act = (f) => { menu = false; f(); };
</script>

<svelte:window onpointerdown={(e) => { if (menu && !e.target.closest?.('.filemenu')) menu = false; }}
               onkeydown={(e) => { if (e.key === 'Escape') menu = false; }} />

<header class="topbar">
  <div class="tb-left">
  <div class="filemenu">
    <button class="btn ghost" onclick={() => (menu = !menu)} aria-haspopup="menu" aria-expanded={menu}>File <span class="caret">▾</span></button>
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
        <button role="menuitem" onclick={() => act(() => (s.shortcutsOpen = true))}>Shortcuts <kbd>?</kbd></button>
      </div>
    {/if}
  </div>
  <div class="namebox">
    {#key s.picks}<span class="name" class:flash={s.picks > 0}>{s.title}</span>{/key}
  </div>
  </div>
  <div class="tb-right">
  <div class="vu" class:hot={s.meter.hold > -1} title="Output peak (dBFS), before the volume">
    <div class="track"><div class="fill" style="width:{s.meter.level * 100}%"></div></div>
    <span class="mono">{isFinite(s.meter.hold) && s.meter.hold > -90 ? s.meter.hold.toFixed(0) : '−∞'}</span>
  </div>
  <label class="volume" title="Listening volume (double-click: 0 dB)">
    <span aria-hidden="true">Vol</span>
    <input type="range" min="-60" max="0" step="0.5" value={s.volume} oninput={(e) => s.setVolume(+e.target.value)}
           ondblclick={() => s.setVolume(0)} style="--p:{((s.volume + 60) / 60) * 100}%" aria-label="Volume" />
    <span class="mono db">{s.volume <= -60 ? 'off' : `${s.volume.toFixed(1)} dB`}</span>
  </label>
  <span class="seqlabel">Sequencer</span>
  <button class="play" class:on={q.playing} onclick={q.toggle} aria-label={q.playing ? 'Stop the clip' : 'Play the clip'} title="Play / stop the clip (Space)">{q.playing ? '■' : '▶'}</button>
  <span class="sep" aria-hidden="true"></span>
  <nav class="viewtabs" role="tablist" aria-label="Views">
      {#each views as [id, name]}
        <button role="tab" aria-selected={view === id} class:on={view === id} onclick={() => (view = id)}>{name}</button>
      {/each}
    </nav>
  </div>
</header>
