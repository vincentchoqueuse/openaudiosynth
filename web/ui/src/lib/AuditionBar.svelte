<!-- AuditionBar.svelte — hear the current sound: its audition phrase (or notes typed in the same syntax), once or
     in a loop. It waits while the sequence plays (one player). -->
<script>
  import { session as s } from './session.svelte.js';
</script>

<div class="player">
  <button class="play" class:on={s.playing} onclick={() => (s.playing ? s.stop() : s.play())}
          disabled={(!s.current && !s.playing) || s.seqPlaying} aria-label={s.playing ? 'Stop' : 'Play audition'}
          title={s.seqPlaying ? 'The sequence is playing' : 'Space'}>{s.playing ? '■' : '▶'}</button>
  <input class="phrase" placeholder={s.current ? 'Audition phrase, or type notes: C3 E3 G3+B3 - _' : 'Load a sound to audition it'}
         bind:value={s.notes} />
  <label class="toggle"><input type="checkbox" bind:checked={s.loop} /> Loop</label>
</div>
