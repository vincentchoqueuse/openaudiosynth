<!-- InfoPanel.svelte — the Synth tab's Info: the sound's name and description (saved with it), the instrument it
     plays on and what that instrument is, and the file it was loaded from. -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<section class="card info">
  <h2>{s.title}</h2>
  {#if s.current}<p class="muted mono small">patches/{s.current}</p>{/if}
  <label class="desc">Description
    <textarea rows="5" bind:value={s.description} placeholder="What it sounds like, how to play it"></textarea>
  </label>
  <h3>Instrument</h3>
  <p><b>{s.engine?.name}</b> — {s.engine?.description}</p>
</section>

<style>
  .info { max-width: 760px; display: grid; gap: 10px; }
  .info h2 { margin: 0; font-size: 16px; }
  .info h3 { margin: 6px 0 0; font-size: 12px; color: var(--muted); font-weight: 600; }
  .info p { margin: 0; font-size: 12.5px; line-height: 1.5; }
  .small { font-size: 11.5px; }
  .desc { display: grid; gap: 6px; font-size: 12px; color: var(--muted); }
  textarea { width: 100%; box-sizing: border-box; border: 1px solid var(--line); background: var(--bg); border-radius: 8px; padding: 8px 10px; color: var(--text);
             font: 12.5px/1.5 var(--sans); resize: vertical; outline: none; }
  textarea:focus { border-color: var(--accent); }
</style>
