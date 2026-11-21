<!-- Sidebar.svelte — the left column: the brand, the sound library (instruments, then their presets, as on the
     board), the MIDI input in use and the project's links. -->
<script>
  import Library from './Library.svelte';
  import ProjectLinks from './ProjectLinks.svelte';
  import { session as s } from './session.svelte.js';
</script>

<aside>
  <div class="brand"><span class="logo"></span>OASynth <span class="by">by Choc</span></div>
  <Library patches={s.patches} current={s.current} onpick={s.loadPatch} />
  <div class="side-foot">
    <span class="midi" class:on={s.midiNames.length} title={s.midiNames.join(', ') || 'No MIDI input'}>
      {s.midiNames.length ? s.midiNames[0] : 'no MIDI input'}
    </span>
    <ProjectLinks />
  </div>
</aside>
