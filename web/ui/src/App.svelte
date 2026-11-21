<!-- App.svelte — web prototype of the synth, put together from its parts: the sound library on the left; the current
     sound (header, audition, macros, output, signal chain, editors in tabs); the Sequence panel on the right; the
     keyboard along the bottom. The state and the actions live in lib/session.svelte.js; the whole interface takes
     the color of the instrument playing. Space auditions the sound (the sequence has its own Space). -->
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
  import Sequencer from './lib/Sequencer.svelte';
  import Keyboard from './lib/Keyboard.svelte';
  import SaveDialog from './lib/SaveDialog.svelte';

  let peak = $state(-Infinity);
  // the Sequence panel on the right: its loop keeps playing while the sound is edited; it can fold away
  let seqOpen = $state((() => { try { return localStorage.getItem('synth.seqpanel') !== 'closed'; } catch { return true; } })());
  function toggleSeq() {
    seqOpen = !seqOpen;
    try { localStorage.setItem('synth.seqpanel', seqOpen ? 'open' : 'closed'); } catch { /* storage unavailable */ }
  }

  function onKey(e) {
    if (s.saving && e.key === 'Escape') { s.saving = null; return; }
    // in the library, Space auditions the sound just picked instead of clicking its button again
    const inLibrary = e.code === 'Space' && e.target?.closest?.('.library nav');
    if (!s.started || s.saving || s.seqPlaying || e.target?.closest?.('.seqpanel') || (!inLibrary && keyIsForControl(e))) return;
    if (e.code === 'Space') { e.preventDefault(); s.playing ? s.stop() : s.current && s.play(); }
  }
</script>

<svelte:window onkeydown={onKey} />

{#if !s.started}
  <Splash />
{:else}
  <div class="app" class:seq-closed={!seqOpen} style="--accent:{s.color}">
    <Sidebar />

    <main>
      <SoundHeader />
      <AuditionBar />
      <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
                  bind:names={s.macroNames} baseline={s.baseline} />
      <Scope analyser={s.synth.analyser} bind:peak />
      <ChainView engine={s.engine} fx={s.catalog.fx} values={s.values} byName={s.byName} getP={s.getP} />
      <EditorTabs />
    </main>

    <section class="seqpanel" class:closed={!seqOpen} aria-label="Sequence">
      <button class="fold" onclick={toggleSeq} aria-expanded={seqOpen} title={seqOpen ? 'Fold the sequence away' : 'Sequence'}>
        {#if seqOpen}›{:else}‹ <span>Sequence</span>{#if s.seqPlaying}<span class="live" title="Playing"></span>{/if}{/if}
      </button>
      <div class="seqbody" hidden={!seqOpen}>
        <Sequencer synth={s.synth} bind:playing={s.seqPlaying} patchGen={s.patchGen} setTempo={(b) => s.setP('tempo', b)}
                   getPatch={s.currentPatch} />
      </div>
    </section>

    <footer><Keyboard synth={s.synth} held={s.held} /></footer>

    {#if s.toast}<div class="toast {s.toast.kind}" role="status">{s.toast.text}</div>{/if}
    {#if s.saving}<SaveDialog />{/if}
  </div>
{/if}
