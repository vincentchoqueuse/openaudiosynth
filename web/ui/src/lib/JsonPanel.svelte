<!-- JsonPanel.svelte — the current sound as its patch file (docs/patch_format.md), to read or edit by hand in a code
     editor (CodeEditor: line numbers, current line, colors, errors underlined): macros, parameters by name or
     role, kit, audition. The text is checked as JSON while typing; Apply (or Cmd/Ctrl+Enter)
     hands it to the engine's own patch reader, whose message tells what is wrong (an unknown parameter lists the
     valid names). "From the sound" writes the text again from what plays. -->
<script>
  const editor = import('./CodeEditor.svelte'); // loaded with the tab: the code editor stays out of the main bundle
  let { getPatch, apply } = $props();
  let text = $state('');
  let result = $state(null); // {ok, text}
  let busy = $state(false);

  const fresh = () => { text = JSON.stringify(getPatch(), null, 2) + '\n'; result = null; };
  fresh();

  const syntax = $derived.by(() => {
    try { JSON.parse(text); return ''; } catch (e) { return e.message; }
  });
  async function run() {
    if (syntax || busy) return;
    busy = true;
    result = await apply(text);
    busy = false;
  }
</script>

<section class="card jsonpanel">
  <div class="jhead">
    <h2>Patch file</h2>
    <span class="status" class:bad={syntax || (result && !result.ok)} class:ok={!syntax && result?.ok}>
      {syntax ? `JSON: ${syntax}` : result ? (result.ok ? result.text || 'Applied' : result.text) : 'Valid JSON'}
    </span>
    <button class="btn ghost" onclick={fresh} title="write the text again from the sound playing">From the sound</button>
    <button class="btn" onclick={run} disabled={!!syntax || busy} title="Cmd/Ctrl+Enter">Apply</button>
  </div>
  {#await editor}
    <div class="loading">Loading the editor…</div>
  {:then m}
    <m.default bind:value={text} onrun={run} />
  {/await}
  <p class="hint">Format: docs/patch_format.md. Parameters take physical units or labels; <code>macros</code> lists the live
    controls with their targets; <code>kit</code> names a sample kit. Apply reloads the sound from this text.</p>
</section>

<style>
  .jsonpanel { display: grid; gap: 10px; }
  .jhead { display: flex; align-items: center; gap: 10px; }
  .jhead h2 { margin: 0; }
  .status { flex: 1; font-size: 12px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .status.bad { color: var(--hot); white-space: normal; }
  .status.ok { color: var(--ok); }
  .hint { margin: 0; font-size: 12px; color: var(--muted); }
  .loading { height: 460px; display: grid; place-items: center; color: var(--muted); font-size: 12px; }
</style>
