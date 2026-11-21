<!-- SoundHeader.svelte — the current sound: its instrument (engine) to switch, how many parameters moved since it
     was loaded (with Revert), its name and description, and Save to library (development) / Export. -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<header class="sound">
  <div class="ident">
    <div class="kicker">
      <select class="engine" value={s.engineId} onchange={(e) => s.setEngine(+e.target.value)} aria-label="Instrument">
        {#each s.catalog.engines as e}<option value={e.id}>{e.name}</option>{/each}
      </select>
      {#if s.changedCount}
        <span class="badge" title="parameters moved since the sound was loaded">{s.changedCount} edited</span>
        <button class="link" onclick={s.revert} title={s.info ? 'reload the saved sound' : 'back to the defaults'}>Revert</button>
      {/if}
    </div>
    <h1>{s.title}</h1>
    <p class="desc">{s.info?.description || s.engine?.description}</p>
  </div>
  <div class="actions">
    {#if s.canSave}<button class="btn" onclick={s.openSave}>Save to library</button>{/if}
    <button class="btn ghost" onclick={s.exportPatch}>Export</button>
  </div>
</header>
