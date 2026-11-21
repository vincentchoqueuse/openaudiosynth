<!-- InfoPanel.svelte — the Synth tab's Info, to read: the sound's name and description (edited in Code), the instrument it
     plays on and what that instrument is, and the file it was loaded from. -->
<script>
  import Card from './Card.svelte';
  import { session as s } from './session.svelte.js';
</script>

<Card class="infocard">
  <h2>{s.title}</h2>
  {#if s.current}<p class="muted mono small">patches/{s.current}</p>{/if}
  <h3>Description</h3>
  <p>{s.description || 'No description.'}</p>
  <h3>Instrument</h3>
  <p><b>{s.engine?.name}</b> — {s.engine?.description}</p>
</Card>

<style>
  :global(.infocard) { max-width: 760px; display: grid; gap: 10px; }
  :global(.infocard) h2 { margin: 0; font-size: 16px; }
  :global(.infocard) h3 { margin: 6px 0 0; font-size: 12px; color: var(--muted); font-weight: 600; }
  :global(.infocard) p { margin: 0; font-size: 12.5px; line-height: 1.5; }
</style>
