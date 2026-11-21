<!-- App.svelte — web prototype of the synth, put together from its parts: the sound library on the left; the top
     bar (File menu, the sound's name, play / stop, the views, undo, level and volume); three main views in the top bar: Sequence (Live,
     then Sequencer), Synth (Structure, then the sound's editors) and Code (the sound's JSON); the macros to play and the keyboard along the bottom. State and actions live in lib/session.svelte.js and
     lib/sequence.svelte.js; the interface takes the color of the instrument playing. Space plays / stops the clip. -->
<script>
  import { session as s } from './lib/session.svelte.js';
  import { keyIsForControl } from './lib/keys.js';
  import Splash from './lib/Splash.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import TopBar from './lib/TopBar.svelte';
  import ClipTab from './lib/ClipTab.svelte';
  import SynthTab from './lib/SynthTab.svelte';
  import CodeView from './lib/CodeView.svelte';
  import { remembered, remember } from './lib/fold.js';
  import { sequence } from './lib/sequence.svelte.js';
  import { history } from './lib/history.svelte.js';
  import Keyboard from './lib/Keyboard.svelte';
  import MacroStrip from './lib/MacroStrip.svelte';
  import SaveDialog from './lib/SaveDialog.svelte';
  import SettingsDialog from './lib/SettingsDialog.svelte';
  import ShortcutsDialog from './lib/ShortcutsDialog.svelte';
  import ShareDialog from './lib/ShareDialog.svelte';

  // the three main views (kept in the browser)
  const VIEWS = [
    ['clip', 'Sequence'],
    ['synth', 'Synth'],
    ['code', 'Code'],
  ];
  let main = $state(remembered('synth.main', 'clip'));
  if (!VIEWS.some(([id]) => id === main)) main = 'clip';
  $effect(() => remember('synth.main', main));
  const lit = $derived(new Set([...s.held, ...sequence.sounding])); // played on the keys, or by the clip
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
      <TopBar bind:view={main} views={VIEWS} />
      <!-- Clip stays mounted: its scope measures the level of the top bar -->
      <div hidden={main !== 'clip'}><h1 class="viewtitle">Sequence</h1><ClipTab /></div>
      {#if main === 'synth'}<h1 class="viewtitle">Synth</h1><SynthTab />{/if}
      {#if main === 'code'}<h1 class="viewtitle">Code</h1><CodeView />{/if}
    </main>

    <footer>
      <MacroStrip engine={s.engine} shared={s.shared} byName={s.byName} values={s.values} setValue={s.setValue}
                  bind:names={s.macroNames} baseline={s.baseline} />
      <Keyboard synth={s.synth} held={lit} />
    </footer>

    {#if s.toast}<div class="toast {s.toast.kind}" role="status">{s.toast.text}</div>{/if}
    {#if s.saving}<SaveDialog />{/if}
    {#if s.settingsOpen}<SettingsDialog />{/if}
    {#if s.shortcutsOpen}<ShortcutsDialog />{/if}
    {#if s.shareOpen}<ShareDialog />{/if}
  </div>
{/if}
