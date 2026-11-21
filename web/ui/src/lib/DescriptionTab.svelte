<!-- DescriptionTab.svelte — what the sound is: its description (written here, saved with it), its instrument and
     what that instrument does, and what it is made of (macros, clip, kit, parameters moved). -->
<script>
  import { session as s } from './session.svelte.js';
  import { sequence as q } from './sequence.svelte.js';
  const named = $derived(s.macroNames.filter(Boolean));
</script>

<div class="desc-tab">
  <label class="field">
    <span>Description</span>
    <textarea rows="4" bind:value={s.description} placeholder="What it sounds like, how to play it"></textarea>
  </label>
  <dl>
    <dt>Instrument</dt><dd><b>{s.engine?.name}</b> — {s.engine?.description}</dd>
    <dt>Macros</dt><dd>{named.length ? named.join(' · ') : 'none'}</dd>
    <dt>Clip</dt><dd>{q.notes.length} note{q.notes.length === 1 ? '' : 's'} over {q.length} beats at {q.tempo} BPM{q.loop ? ', looped' : ''}</dd>
    {#if s.engine?.name === 'sampler'}<dt>Kit</dt><dd>{s.kitLabel || 'none'}</dd>{/if}
    <dt>Edited</dt><dd>{s.changedCount} parameter{s.changedCount === 1 ? '' : 's'} moved since it was loaded</dd>
    {#if s.current}<dt>File</dt><dd class="mono">patches/{s.current}</dd>{/if}
  </dl>
</div>

<style>
  .desc-tab { display: grid; gap: 18px; max-width: 760px; }
  .field { display: grid; gap: 6px; font-size: 12px; color: var(--muted); }
  .field textarea { border: 1px solid var(--line); background: var(--surface); border-radius: 8px; padding: 10px 12px; color: var(--text);
    font: 13px/1.5 var(--sans); resize: vertical; outline: none; }
  .field textarea:focus { border-color: var(--accent); }
  dl { display: grid; grid-template-columns: 7em 1fr; gap: 8px 14px; margin: 0; font-size: 12.5px; }
  dt { color: var(--muted); }
  dd { margin: 0; }
</style>
