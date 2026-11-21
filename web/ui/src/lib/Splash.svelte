<!-- Splash.svelte — the first screen: the name, a line of its author's, and the button that starts audio (browsers
     only start it on a click). -->
<script>
  import ProjectLinks from './ProjectLinks.svelte';
  import { session as s } from './session.svelte.js';
</script>

<main class="splash">
  <div class="mark">OASynth</div>
  <div class="byline">by Choc</div>
  <p class="quote" lang="fr">« Le silicium calcule, l'artisan incarne. »</p>
  <p>The DSP engine of the boards, compiled to WebAssembly, playing in your browser.</p>
  <button class="primary" onclick={s.start} disabled={s.starting}>{s.starting ? 'Loading…' : 'Start audio'}</button>
  {#if s.toast}<p class="error">{s.toast.text}</p>{/if}
  <ProjectLinks />
</main>
