<!-- Section.svelte — the one shape of every block of the page (Live, Macros, Sequencer, Structure, Sound): a small
     capital title, its own controls on the right (`tools`), and a chevron that folds it to its title line, kept in
     the browser under `id`. `summary` shows on the title line when folded. -->
<script>
  import { remembered, remember } from './fold.js';
  let { id, title, summary = '', tools, children } = $props();
  // svelte-ignore state_referenced_locally
  let open = $state(remembered(`synth.section.${id}`, true)); // a section's id never changes
  const fold = () => { open = !open; remember(`synth.section.${id}`, open); };
</script>

<section class="section" class:closed={!open}>
  <div class="section-head">
    <h2>{title}</h2>
    {#if !open && summary}<span class="summary muted">{summary}</span>{/if}
    <span class="spacer"></span>
    {#if open && tools}{@render tools()}{/if}
    <button class="fold" onclick={fold} aria-expanded={open} aria-label="{open ? 'Fold' : 'Unfold'} {title}">{open ? '˄' : '˅'}</button>
  </div>
  <div class="section-body" hidden={!open}>{@render children()}</div>
</section>
