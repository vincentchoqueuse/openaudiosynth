<!-- Section.svelte — the one shape of every block of the page (Live, Macros, Sequencer, Structure, Editor): a small
     capital title, its own controls on the right (`tools`), and a chevron that folds it to its title line (the
     title folds it too), kept in the browser under `id`. `summary` shows on the title line when folded. -->
<script>
  import { remembered, remember } from './fold.js';
  let { id, title, summary = '', tools, children } = $props();
  // svelte-ignore state_referenced_locally
  let open = $state(remembered(`synth.section.${id}`, true)); // a section's id never changes
  const fold = () => { open = !open; remember(`synth.section.${id}`, open); };
</script>

<section class="section" class:closed={!open}>
  <div class="section-head">
    <button class="stitle" onclick={fold} aria-expanded={open}><h2>{title}</h2></button>
    {#if !open && summary}<span class="summary muted">{summary}</span>{/if}
    <span class="spacer"></span>
    {#if open && tools}{@render tools()}{/if}
    <button class="fold" class:closed={!open} onclick={fold} aria-expanded={open} aria-label="{open ? 'Fold' : 'Unfold'} {title}" title={open ? 'Fold' : 'Unfold'}>
      <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true"><path d="M4 10l4-4 4 4" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
    </button>
  </div>
  <div class="section-body" hidden={!open}>{@render children()}</div>
</section>
