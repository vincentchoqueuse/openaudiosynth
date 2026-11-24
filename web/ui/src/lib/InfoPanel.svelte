<!-- InfoPanel.svelte — what the sound is, at the top of the Synth view's Overview: its name and description (edited in
     Code or when saving), then the instrument it plays on and what that instrument is. -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<div class="about">
  <h2>{s.label}</h2>
  <p>{s.description || 'No description.'}</p>
  <p class="muted"><b>{s.engine?.name}</b> — {s.engine?.description}</p>
</div>

<style>
  .about { max-width: 860px; display: grid; gap: 6px; margin: 4px 0 16px; }
  .about h2 { margin: 0; font-size: 16px; text-transform: none; letter-spacing: -.01em; color: var(--text); }
  .about p { margin: 0; font-size: 12.5px; line-height: 1.5; }
</style>
