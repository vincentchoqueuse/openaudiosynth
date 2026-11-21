<!-- SaveDialog.svelte — save the current sound into patches/ (development server only): name, description, and a
     confirmation before replacing a file. The board's presets are generated from that folder. -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.saving = null)}>
  <form class="dialog" onsubmit={(e) => { e.preventDefault(); s.save(s.saving.exists); }}>
    <h2>Save to the library</h2>
    <p class="muted">Writes <code>patches/{s.saving.name || '…'}.json</code>. The board's presets are generated
      from this folder: <code>just build</code>, then <code>just flash</code>.</p>
    <label>Name <input bind:value={s.saving.name} pattern="[a-z0-9_]+" required autocomplete="off"
           oninput={() => (s.saving.exists = false)} /></label>
    <label>Description <textarea rows="3" bind:value={s.saving.description} placeholder="What it sounds like, how to play it"></textarea></label>
    {#if s.saving.error}<p class="error">{s.saving.error}</p>{/if}
    {#if s.saving.exists}<p class="warn">patches/{s.saving.name}.json already exists.</p>{/if}
    <div class="row">
      <button type="button" class="btn ghost" onclick={() => (s.saving = null)}>Cancel</button>
      <button type="submit" class="btn primary">{s.saving.exists ? 'Replace' : 'Save'}</button>
    </div>
  </form>
</div>
