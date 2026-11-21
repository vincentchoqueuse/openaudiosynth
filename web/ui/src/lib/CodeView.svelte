<!-- CodeView.svelte — the Code view: the instrument (the sound's patch file) or the sequencer (the clip), chosen
     with a pill, in a code editor as tall as the page (CodeEditor: line numbers, colors, errors underlined). Apply (or Cmd/Ctrl+Enter) reads it back: the engine's patch reader for the sound, fromDoc for the clip,
     whose message says what is wrong. Reload writes it again from what plays; the text follows what plays until
     it is typed in. -->
<script>
  import Seg from './Seg.svelte';
  import { untrack } from 'svelte';
  import { session as s } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { SCALES, GRIDS } from './seqExport.js';
  const editor = import('./CodeEditor.svelte'); // the code editor stays out of the main bundle
  import { remembered, remember } from './prefs.js';
  let doc = $state(remembered('synth.code', 'sound'));
  $effect(() => remember('synth.code', doc));

  const DOCS = {
    sound: { text: () => JSON.stringify(s.currentPatch(), null, 2) + '\n',
      apply: async (t) => { const r = await s.applyFromEditor(t); return r.ok ? '' : r.text; },
      hint: 'The sound as its patch file (docs/patch_format.md): parameters in physical units or labels, macros with their targets, kit, clip. Apply reloads the sound from this text.' },
    clip: { text: () => q.docText(), apply: async (t) => q.applyText(t),
      hint: `A MIDI clip: notes {pitch (MIDI, 60 = C4), start and length in beats, velocity 1..127}; length is the clip in beats, and its loop when loop is true. grid: the piano roll's snap (${GRIDS.map((g) => g[0]).join(', ')}); key: the scale it shows (${Object.keys(SCALES).join(', ')}).` },
  };
  const d = $derived(DOCS[doc]);

  let text = $state(''), shown = $state(''), error = $state(''), busy = $state(false);
  const edited = $derived(text !== shown);
  const syntax = $derived.by(() => { try { JSON.parse(text); return ''; } catch (e) { return e.message; } });
  $effect(() => { // the text from what plays replaces the editor's while nothing is typed in
    const t = d.text();
    untrack(() => { if (text === shown) { text = t; error = ''; } shown = t; });
  });
  async function apply() {
    if (syntax || busy) return;
    busy = true;
    error = await d.apply(text);
    busy = false;
    if (!error) text = shown;
  }
  function pick(v) { doc = v; error = ''; text = shown = DOCS[v].text(); } // another document: its text as it plays
  function reload() { error = ''; text = shown; }
</script>

<div class="codeview">
  <div class="toolbar">
    <Seg items={[{ id: 'sound', label: 'Instrument' }, { id: 'clip', label: 'Sequencer' }]} value={doc} onpick={pick} label="Document" />
    <span class="status" class:bad={syntax || error}>
      {syntax ? `JSON: ${syntax}` : error || (edited ? 'Edited, not applied' : '')}
    </span>
    <span class="spacer"></span>
    <button class="btn ghost" onclick={reload} disabled={!edited && !error} title="the text again from what plays">Reload</button>
    <button class="btn" onclick={apply} disabled={!!syntax || busy || !edited} title="Cmd/Ctrl+Enter">Apply</button>
  </div>
  {#await editor}
    <div class="loading">Loading the editor…</div>
  {:then m}
    <div class="edit"><m.default bind:value={text} onrun={apply} /></div>
  {/await}
  <p class="hint">{d.hint}</p>
</div>

<style>
  .codeview { display: grid; grid-template-columns: minmax(0, 1fr); grid-template-rows: auto minmax(0, 1fr) auto; min-width: 0; gap: 10px; height: calc(100% - 96px); min-height: 360px; }
  .codeview .toolbar { margin: 0; }
  .edit { min-height: 0; min-width: 0; overflow: hidden; }
  .edit :global(.code) { height: 100%; resize: none; }
  .hint { margin: 0; }
  .status { font-size: 12px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .status.bad { color: var(--hot); white-space: normal; }
  .loading { height: 100%; display: grid; place-items: center; color: var(--muted); font-size: 12px; }
</style>
