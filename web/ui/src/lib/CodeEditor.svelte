<!-- CodeEditor.svelte — a JSON editor (CodeMirror 6, MIT): line numbers, the line of the cursor highlighted,
     JSON colors, matching brackets, folding, search (Cmd/Ctrl+F), undo; a JSON syntax error is underlined where it
     is, with its message on hover and a mark in the gutter. Colors from the app's theme (light and dark) and the
     instrument's accent. Cmd/Ctrl+Enter calls onrun. `value` is two-way: a new value from outside replaces the
     text (keeping the cursor where it can). -->
<script>
  import { onMount } from 'svelte';
  import { EditorView, basicSetup } from 'codemirror';
  import { keymap } from '@codemirror/view';
  import { json, jsonParseLinter } from '@codemirror/lang-json';
  import { linter, lintGutter } from '@codemirror/lint';
  import { HighlightStyle, syntaxHighlighting } from '@codemirror/language';
  import { tags } from '@lezer/highlight';

  let { value = $bindable(''), onrun } = $props();
  let host, view;

  const colors = HighlightStyle.define([
    { tag: tags.propertyName, color: 'var(--accent)' },
    { tag: tags.string, color: 'var(--code-string)' },
    { tag: tags.number, color: 'var(--code-number)' },
    { tag: [tags.bool, tags.null], color: 'var(--code-keyword)' },
    { tag: tags.punctuation, color: 'var(--muted)' },
  ]);

  const theme = EditorView.theme({
    '&': { height: '100%', fontSize: '12.5px', backgroundColor: 'var(--faint)', color: 'var(--text)', borderRadius: '8px' },
    '&.cm-focused': { outline: '2px solid var(--accent)', outlineOffset: '-1px' },
    '.cm-scroller': { fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace', lineHeight: '1.55' },
    '.cm-content': { caretColor: 'var(--text)', padding: '8px 0' },
    '.cm-cursor': { borderLeftColor: 'var(--text)' },
    '.cm-gutters': { backgroundColor: 'var(--surface)', color: 'var(--muted)', border: 'none', borderRight: '1px solid var(--line)',
                     borderRadius: '8px 0 0 8px' },
    '.cm-activeLine': { backgroundColor: 'var(--accent-soft)' },
    '.cm-activeLineGutter': { backgroundColor: 'var(--accent-soft)', color: 'var(--text)' },
    '&.cm-focused .cm-selectionBackground, .cm-selectionBackground, ::selection': {
      backgroundColor: 'color-mix(in srgb, var(--accent) 30%, transparent)' },
    '.cm-matchingBracket': { backgroundColor: 'color-mix(in srgb, var(--accent) 28%, transparent)', outline: 'none' },
    '.cm-foldPlaceholder': { backgroundColor: 'var(--surface)', border: '1px solid var(--line)', color: 'var(--muted)' },
    '.cm-tooltip': { backgroundColor: 'var(--surface)', border: '1px solid var(--line)', color: 'var(--text)' },
    '.cm-panels': { backgroundColor: 'var(--surface)', color: 'var(--text)' },
    '.cm-panels input, .cm-panels button': { font: 'inherit' },
  });

  onMount(() => {
    view = new EditorView({
      parent: host,
      doc: value,
      extensions: [
        basicSetup,
        json(),
        linter(jsonParseLinter(), { delay: 250 }),
        lintGutter(),
        syntaxHighlighting(colors),
        theme,
        keymap.of([{ key: 'Mod-Enter', run: () => { onrun?.(); return true; } }]),
        EditorView.updateListener.of((u) => { if (u.docChanged) value = u.state.doc.toString(); }),
      ],
    });
    return () => view.destroy();
  });

  // a value set from outside (From the sound): replace the text, keep the cursor inside it
  $effect(() => {
    const v = value;
    if (!view || v === view.state.doc.toString()) return;
    const head = Math.min(view.state.selection.main.head, v.length);
    view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: v }, selection: { anchor: head } });
  });
</script>

<div class="code" bind:this={host}></div>

<style>
  .code { height: 460px; min-height: 200px; resize: vertical; overflow: hidden; border: 1px solid var(--line); border-radius: 8px; }
  .code :global(.cm-editor) { height: 100%; }
</style>
