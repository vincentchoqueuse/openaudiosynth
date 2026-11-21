<!-- App.svelte — web prototype of the synth, put together from its parts: the sound library on the left; the top
     bar (the sound: name, Save, Export; undo; level and volume); three main views as nav tabs with an icon: Play (Live,
     then Macros and Sequencer), Synth (Structure, then the sound's editors) and Code (the sound's JSON); the keyboard along the bottom. State and actions live in lib/session.svelte.js and
     lib/sequence.svelte.js; the interface takes the color of the instrument playing. Space plays / stops the clip. -->
<script>
  import { session as s } from './lib/session.svelte.js';
  import { keyIsForControl } from './lib/keys.js';
  import Splash from './lib/Splash.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import TopBar from './lib/TopBar.svelte';
  import PlayerBar from './lib/PlayerBar.svelte';
  import PlayTab from './lib/PlayTab.svelte';
  import SynthTab from './lib/SynthTab.svelte';
  import CodeView from './lib/CodeView.svelte';
  import { remembered, remember } from './lib/fold.js';
  import { sequence } from './lib/sequence.svelte.js';
  import { history } from './lib/history.svelte.js';
  import Keyboard from './lib/Keyboard.svelte';
  import SaveDialog from './lib/SaveDialog.svelte';
  import SettingsDialog from './lib/SettingsDialog.svelte';
  import ShortcutsDialog from './lib/ShortcutsDialog.svelte';
  import ShareDialog from './lib/ShareDialog.svelte';

  // the three main views (kept in the browser)
  const VIEWS = [
    ['play', 'Play', 'M4.5 3.2v9.6L12.5 8z'],
    ['synth', 'Synth', 'M3 2.5v11M8 2.5v11M13 2.5v11M1.5 10h3M6.5 5.5h3M11.5 8.5h3'],
    ['code', 'Code', 'M5.5 4 2 8l3.5 4M10.5 4 14 8l-3.5 4'],
  ];
  let main = $state(remembered('synth.main', 'play'));
  const pickMain = (t) => { main = t; remember('synth.main', t); };
  // text on the accent: dark on a light accent (the drone's pale blue), white on a dark one
  const onAccent = $derived.by(() => {
    const h = /^#?([0-9a-f]{6})$/i.exec(s.color ?? '');
    if (!h) return '#fff';
    const [r, g, b] = [0, 2, 4].map((i) => parseInt(h[1].slice(i, i + 2), 16) / 255).map((c) => (c <= 0.04 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.4 ? '#0e1013' : '#fff';
  });
  let attached = false; // the sequence comes back from this browser's storage once audio runs
  $effect(() => { if (s.started && !attached) { attached = true; sequence.attach(); history.attach(); } });

  function onKey(e) {
    if (e.key === 'Escape' && (s.saving || s.settingsOpen || s.shortcutsOpen || s.shareOpen)) { s.saving = null; s.settingsOpen = s.shortcutsOpen = s.shareOpen = false; return; }
    if (e.key === '?' && s.started && !keyIsForControl(e)) { e.preventDefault(); s.shortcutsOpen = !s.shortcutsOpen; return; }
    // undo / redo: Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z or Ctrl+Y; text fields and the code editor keep their own
    const mod = e.metaKey || e.ctrlKey;
    if (s.started && !s.saving && mod && (e.code === 'KeyZ' || e.code === 'KeyY')) {
      const t = e.target, typing = t?.closest?.('.cm-editor') || t?.tagName === 'TEXTAREA' || (t?.tagName === 'INPUT' && !['range', 'checkbox', 'radio', 'button'].includes(t.type));
      if (typing) return;
      e.preventDefault();
      if (e.code === 'KeyY' || e.shiftKey) history.redo(); else history.undo();
      return;
    }
    if (!s.started || s.saving || s.settingsOpen || s.shortcutsOpen || e.code !== 'Space') return;
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
  <div class="app" style="--accent:{s.color}; --on-accent:{onAccent}">
    <Sidebar />

    <main>
      <TopBar />
      <PlayerBar />
      <nav class="maintabs" role="tablist" aria-label="Views">
        {#each VIEWS as [id, name, d]}
          <button role="tab" aria-selected={main === id} class:on={main === id} onclick={() => pickMain(id)}>
            <svg viewBox="0 0 16 16" width="15" height="15" aria-hidden="true"><path d={d} fill={id === 'play' ? 'currentColor' : 'none'} stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" /></svg>{name}
          </button>
        {/each}
      </nav>
      <!-- Play stays mounted: its scope measures the level of the top bar -->
      <div hidden={main !== 'play'}><PlayTab /></div>
      {#if main === 'synth'}<SynthTab />{/if}
      {#if main === 'code'}<CodeView doc="sound" />{/if}
    </main>

    <footer><Keyboard synth={s.synth} held={s.held} /></footer>

    {#if s.toast}<div class="toast {s.toast.kind}" role="status">{s.toast.text}</div>{/if}
    {#if s.saving}<SaveDialog />{/if}
    {#if s.settingsOpen}<SettingsDialog />{/if}
    {#if s.shortcutsOpen}<ShortcutsDialog />{/if}
    {#if s.shareOpen}<ShareDialog />{/if}
  </div>
{/if}
