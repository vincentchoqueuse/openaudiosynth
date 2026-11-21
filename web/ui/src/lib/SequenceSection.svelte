<!-- SequenceSection.svelte — the Sequence section, apart from the sound: the clip the transport plays (each preset
     brings its own), folded to one line on a click. -->
<script>
  import SequenceTab from './SequenceTab.svelte';
  import { sequence as q } from './sequence.svelte.js';
  import { remembered, remember } from './fold.js';
  let open = $state(remembered('synth.seq.open', true));
  const fold = () => { open = !open; remember('synth.seq.open', open); };
</script>

<section class="section">
  <div class="section-head">
    <h2>Sequence</h2>
    <span class="muted summary">{q.name || 'untitled'} · {q.notes.length} note{q.notes.length === 1 ? '' : 's'} · {q.length} beats{q.loop ? ', looped' : ''}</span>
    {#if q.playing}<span class="live" title="Playing"></span>{/if}
    <button class="fold" onclick={fold} aria-expanded={open} title={open ? 'Fold' : 'Unfold'}>{open ? '˄' : '˅'}</button>
  </div>
  {#if open}<SequenceTab />{/if}
</section>
