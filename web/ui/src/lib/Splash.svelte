<!-- Splash.svelte — the first screen: audio can only start on a click (browsers' rule), so the page waits for it. -->
<script>
  import ProjectLinks from './ProjectLinks.svelte';
  import { session as s } from './session.svelte.js';
</script>

<main class="splash">
  <div class="mark">OASynth</div>
  <div class="byline">by Choc</div>
  <p>The DSP engine of the boards, compiled to WebAssembly, playing in your browser.</p>
  <button class="primary" onclick={s.start} disabled={s.starting}>{s.starting ? 'Loading…' : 'Start audio'}</button>
  {#if s.toast}<p class="error">{s.toast.text}</p>{/if}
  <ProjectLinks />
</main>
