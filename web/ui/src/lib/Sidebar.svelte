<!-- Sidebar.svelte — the left column: the brand, the sound library (instruments, then their presets, as on the
     board), the MIDI input in use and the small icons (settings, theme, links). On a phone it is a bar at the top
     whose library opens on demand (Sounds), and closes once a sound is picked. -->
<script>
  import Library from './Library.svelte';
  import ProjectLinks from './ProjectLinks.svelte';
  import { session as s } from './session.svelte.js';
  let open = $state(false); // the library on a phone
  const pick = async (p) => { await s.loadPatch(p); open = false; };
</script>

<aside class:open>
  <div class="brand"><span class="logo"></span>OASynth <span class="by">by Choc</span>
    <button class="ghost drawer" onclick={() => (open = !open)} aria-expanded={open}>Sounds {open ? '˄' : '˅'}</button>
  </div>
  <Library patches={s.patches} current={s.current} onpick={pick} />
  <div class="side-foot">
    <span class="midi" class:on={s.midiNames.length} title={s.midiNames.join(', ') || 'No MIDI input'}>
      {s.midiNames.length ? s.midiNames[0] : 'no MIDI input'}
    </span>
    <ProjectLinks />
  </div>
</aside>
