<!-- App.svelte — web prototype of the synth, put together from its parts: the sound library on the left; the
     transport on top (the sequence: play, tempo, position); the current sound (header, audition, macros, output,
     signal chain); the editors in tabs (Sound, Effects, Part, Modulation, Sequence, JSON); the keyboard along the
     bottom. The state and the actions live in lib/session.svelte.js and lib/sequence.svelte.js; the whole
     interface takes the color of the instrument playing. Space plays / stops the sequence (in the library, it
     auditions the sound just picked). -->
<script>
  import { session as s } from './lib/session.svelte.js';
  import { keyIsForControl } from './lib/keys.js';
  import Splash from './lib/Splash.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import SoundHeader from './lib/SoundHeader.svelte';
  import AuditionBar from './lib/AuditionBar.svelte';
  import MacroStrip from './lib/MacroStrip.svelte';
  import Scope from './lib/Scope.svelte';
  import ChainView from './lib/ChainView.svelte';
  import EditorTabs from './lib/EditorTabs.svelte';
  import Transport from './lib/Transport.svelte';
  import { sequence } from './lib/sequence.svelte.js';
  import Keyboard from './lib/Keyboard.svelte';
  import SaveDialog from './lib/SaveDialog.svelte';

  let peak = $state(-Infinity);
  let attached = false; // the sequence comes back from this browser's storage once audio runs
  $effect(() => { if (s.started && !attached) { attached = true; sequence.attach(); } });

  function onKey(e) {
    if (s.saving && e.key === 'Escape') { s.saving = null; return; }
    // in the library, Space auditions the sound just picked instead of clicking its button again
    if (!s.started || s.saving || e.code !== 'Space') return;
    if (e.target?.closest?.('.library nav')) { // in the library, Space auditions the sound just picked
      e.preventDefault();
      if (!s.seqPlaying) s.playing ? s.stop() : s.current && s.play();
      return;
    }
    if (keyIsForControl(e) && !e.target?.closest?.('.grid')) return;
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
      <AuditionBar />
      <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
                  bind:names={s.macroNames} baseline={s.baseline} />
      <Scope analyser={s.synth.analyser} bind:peak />
      <ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} />
      <EditorTabs />
    </main>

    <footer><Keyboard synth={s.synth} held={s.held} /></footer>

    {#if s.toast}<div class="toast {s.toast.kind}" role="status">{s.toast.text}</div>{/if}
    {#if s.saving}<SaveDialog />{/if}
  </div>
{/if}
