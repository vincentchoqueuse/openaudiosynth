<!-- TopBar.svelte — the bar above both tabs: the sound (its instrument to switch, its name, which opens its
     description, how many parameters moved since it was loaded with Revert, Save to library in development,
     Export), undo / redo, the output level and the listening volume. Playing the clip is in the Play tab. -->
<script>
  import { session as s } from './session.svelte.js';
  import { history as h } from './history.svelte.js';
  let about = $state(false); // the sound's description, under its name
</script>

<svelte:window onpointerdown={(e) => { if (about && !e.target.closest?.('.namebox')) about = false; }}
               onkeydown={(e) => { if (e.key === 'Escape') about = false; }} />

<header class="topbar">
  <select class="engine" value={s.engineId} onchange={(e) => s.setEngine(+e.target.value)} aria-label="Instrument">
    {#each s.catalog.engines as e}<option value={e.id}>{e.name}</option>{/each}
  </select>
  <div class="namebox">
    {#key s.picks}
      <button class="name" class:flash={s.picks > 0} onclick={() => (about = !about)} aria-expanded={about} title="its description">{s.title} <span class="caret">˅</span></button>
    {/key}
    {#if about}
      <div class="about" role="dialog" aria-label="About this sound">
        <label>Description
          <textarea rows="4" bind:value={s.description} placeholder="What it sounds like, how to play it"></textarea>
        </label>
        <p class="muted small"><b>{s.engine?.name}</b> — {s.engine?.description}</p>
        {#if s.current}<p class="muted small mono">patches/{s.current}</p>{/if}
      </div>
    {/if}
  </div>
  {#if s.changedCount}
    <span class="badge" title="parameters moved since the sound was loaded">{s.changedCount} edited</span>
    <button class="link" onclick={s.revert} title={s.info ? 'reload the saved sound' : 'back to the defaults'}>Revert</button>
  {/if}
  {#if s.canSave}<button class="btn" onclick={s.openSave}>Save</button>{/if}
  <button class="btn ghost" onclick={s.exportPatch}>Export</button>
  <span class="spacer"></span>
  <button class="ghost help" onclick={() => (s.shortcutsOpen = true)} title="Shortcuts (?)" aria-label="Shortcuts">?</button>
  <span class="hist">
    <button class="ghost" onclick={h.undo} disabled={!h.canUndo} title="Undo (⌘Z)" aria-label="Undo">↶</button>
    <button class="ghost" onclick={h.redo} disabled={!h.canRedo} title="Redo (⇧⌘Z)" aria-label="Redo">↷</button>
  </span>
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
</header>
