<!-- InfoPanel.svelte — the Synth tab's Info, to read: the sound's name and description (edited in Code), the instrument it
     plays on and what that instrument is, and the file it was loaded from. -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<section class="card info">
  <h2>{s.title}</h2>
  {#if s.current}<p class="muted mono small">patches/{s.current}</p>{/if}
  <h3>Description</h3>
  <p>{s.description || 'No description.'}</p>
  <h3>Instrument</h3>
  <p><b>{s.engine?.name}</b> — {s.engine?.description}</p>
</section>

<style>
  .info { max-width: 760px; display: grid; gap: 10px; }
  .info h2 { margin: 0; font-size: 16px; }
  .info h3 { margin: 6px 0 0; font-size: 12px; color: var(--muted); font-weight: 600; }
  .info p { margin: 0; font-size: 12.5px; line-height: 1.5; }
</style>
