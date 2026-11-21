<!-- JsonWorkspace.svelte — the JSON tab, laid out like a code editor: an explorer on the left lists the documents
     the page is made of (the sound as its patch file, the sequence), the chosen one opens on the right (CodeEditor:
     line numbers, colors, errors underlined). Apply (or Cmd/Ctrl+Enter) reads it back: the engine's patch reader
     for the sound, fromDoc for the sequence, whose message says what is wrong. Reload writes it again from what
     plays. The text follows what plays until it is typed in. -->
<script>
  import { untrack } from 'svelte';
  import { session as s } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  import { SCALES, STEP_LENGTHS } from './seqExport.js';
  const editor = import('./CodeEditor.svelte'); // the code editor stays out of the main bundle

  // the documents: what each is, its file name, its text from what plays, and how it is applied
  const DOCS = [
    { id: 'sound', group: 'Sound', file: () => `${s.title}.json`, text: () => JSON.stringify(s.patchWithAudition(), null, 2) + '\n',
      apply: async (t) => { const r = await s.applyFromEditor(t); return r.ok ? '' : r.text; },
      hint: 'The sound as its patch file (docs/patch_format.md): parameters in physical units or labels, macros with their targets, kit, audition. Apply reloads the sound from this text.' },
    { id: 'sequence', group: 'Sequence', file: () => `${q.name || 'untitled'}.json`, text: () => q.docText(), apply: async (t) => q.applyText(t),
      hint: `One line per note, high to low: x note, X accent, o soft, _ held, . rest, in groups of four steps. key: a root and a scale (${Object.keys(SCALES).join(', ')}), from octave over range octaves; step: ${STEP_LENGTHS.map((l) => l[0]).join(', ')}.` },
  ];
  let open = $state('sound');
  const doc = $derived(DOCS.find((d) => d.id === open));

  let text = $state(''), shown = $state(''), error = $state(''), busy = $state(false);
  const edited = $derived(text !== shown);
  const syntax = $derived.by(() => { try { JSON.parse(text); return ''; } catch (e) { return e.message; } });
  // the text from what plays; it replaces the editor's while nothing is typed in (or when another file opens)
  let last = '';
  $effect(() => {
    const t = doc.text(), switched = open !== last;
    untrack(() => {
      if (switched || text === shown) { text = t; error = ''; }
      shown = t;
      last = open;
    });
  });
  async function apply() {
    if (syntax || busy) return;
    busy = true;
    error = await doc.apply(text);
    busy = false;
    if (!error) text = shown;
  }
  function reload() { error = ''; text = shown; }
</script>

<div class="jsonws">
  <nav class="explorer" aria-label="Documents">
    {#each DOCS as d}
      <div class="group">{d.group}</div>
      <button class="file" class:on={open === d.id} onclick={() => (open = d.id)}>{d.file()}{open === d.id && edited ? ' •' : ''}</button>
    {/each}
  </nav>
  <div class="pane">
    <div class="jhead">
      <span class="fname mono">{doc.file()}</span>
      <span class="status" class:bad={syntax || error} class:ok={!syntax && !error && !edited}>
        {syntax ? `JSON: ${syntax}` : error || (edited ? 'Edited' : 'As it plays')}
      </span>
      <button class="btn ghost" onclick={reload} disabled={!edited && !error} title="the text again from what plays">Reload</button>
      <button class="btn" onclick={apply} disabled={!!syntax || busy || !edited} title="Cmd/Ctrl+Enter">Apply</button>
    </div>
    {#await editor}
      <div class="loading">Loading the editor…</div>
    {:then m}
      <m.default bind:value={text} onrun={apply} />
    {/await}
    <p class="hint">{doc.hint}</p>
  </div>
</div>

<style>
  .jsonws { display: grid; grid-template-columns: 200px 1fr; gap: 14px; align-items: start; }
  .explorer { display: flex; flex-direction: column; gap: 2px; background: var(--surface); border: 1px solid var(--line); border-radius: 8px; padding: 10px 8px; }
  .group { font-size: 10.5px; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); padding: 8px 8px 2px; }
  .file { text-align: left; font: 12px var(--mono); padding: 5px 8px; border-radius: 6px; color: var(--text); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .file:hover { background: var(--faint); }
  .file.on { background: var(--accent-soft); }
  .pane { display: grid; gap: 10px; min-width: 0; }
  .jhead { display: flex; align-items: center; gap: 10px; }
  .fname { font-size: 13px; }
  .status { flex: 1; font-size: 12px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .status.bad { color: var(--hot); white-space: normal; }
  .status.ok { color: var(--ok); }
  .hint { margin: 0; font-size: 12px; color: var(--muted); }
  .loading { height: 460px; display: grid; place-items: center; color: var(--muted); font-size: 12px; }
  @media (max-width: 760px) { .jsonws { grid-template-columns: 1fr; } }
</style>
