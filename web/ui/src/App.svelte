<!-- App.svelte — web prototype of the synth, put together from its parts: the sound library on the left; the
     transport on top (the clip: play, tempo, loop, position; undo; level and volume); the current sound's name;
     then three sections: Visualize (Live: wave and spectrum, Structure: the signal chain), Sound (Description,
     Macros, Sound, Effects, Part, Modulation, Code) and Sequence (the clip each preset brings); the keyboard along
     the bottom. The state and the actions live in lib/session.svelte.js and lib/sequence.svelte.js; the whole
     interface takes the color of the instrument playing. Space plays / stops the clip. -->
<script>
  import { session as s } from './lib/session.svelte.js';
  import { keyIsForControl } from './lib/keys.js';
  import Splash from './lib/Splash.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import SoundHeader from './lib/SoundHeader.svelte';
  import ViewSection from './lib/ViewSection.svelte';
  import EditorTabs from './lib/EditorTabs.svelte';
  import SequenceSection from './lib/SequenceSection.svelte';
  import Transport from './lib/Transport.svelte';
  import { sequence } from './lib/sequence.svelte.js';
  import { history } from './lib/history.svelte.js';
  import Keyboard from './lib/Keyboard.svelte';
  import SaveDialog from './lib/SaveDialog.svelte';

  let attached = false; // the sequence comes back from this browser's storage once audio runs
  $effect(() => { if (s.started && !attached) { attached = true; sequence.attach(); history.attach(); } });

  function onKey(e) {
    if (s.saving && e.key === 'Escape') { s.saving = null; return; }
    // undo / redo: Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z or Ctrl+Y; text fields and the code editor keep their own
    const mod = e.metaKey || e.ctrlKey;
    if (s.started && !s.saving && mod && (e.code === 'KeyZ' || e.code === 'KeyY')) {
      const t = e.target, typing = t?.closest?.('.cm-editor') || t?.tagName === 'TEXTAREA' || (t?.tagName === 'INPUT' && !['range', 'checkbox', 'radio', 'button'].includes(t.type));
      if (typing) return;
      e.preventDefault();
      if (e.code === 'KeyY' || e.shiftKey) history.redo(); else history.undo();
      return;
    }
    if (!s.started || s.saving || e.code !== 'Space') return;
    if (e.target?.closest?.('.library nav')) { // in the library too, Space plays the clip
      e.preventDefault();
      sequence.toggle();
      return;
    }
    // Space is the transport's everywhere but where it types or ticks (text fields, menus, checkboxes, sliders)
    if (keyIsForControl(e) && e.target?.tagName !== 'BUTTON') return;
    e.preventDefault();
    sequence.toggle();
  }
</script>

<svelte:window onkeydown={onKey} />

{#if !s.started}
  <Splash />
{:else}
  <div class="app" style="--accent:{s.color}">
    <Sidebar />

    <main>
      <Transport />
      <SoundHeader />
      <ViewSection />
      <EditorTabs />
      <SequenceSection />
    </main>

    <footer><Keyboard synth={s.synth} held={s.held} /></footer>

    {#if s.toast}<div class="toast {s.toast.kind}" role="status">{s.toast.text}</div>{/if}
    {#if s.saving}<SaveDialog />{/if}
  </div>
{/if}
