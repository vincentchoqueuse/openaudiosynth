<!-- Section.svelte — one part of a side panel, the same everywhere: an icon and a title on one line (an optional
     note on the right), then its content. The panels are built from these, in the order they are worked: what it is
     (its effect, its shape), its settings, what moves it or what it moves. `fold`: closed until its title is clicked
     (About, at the end of every panel). -->
<script>
  import Icon from './Icon.svelte';
  let { icon, title, note = '', fold = false, children } = $props();
</script>

{#snippet head()}<span class="psec-icon" aria-hidden="true"><Icon name={icon} size={13} /></span>{title}{#if note}<span class="psec-note">{note}</span>{/if}{/snippet}
{#if fold}
  <details class="psec"><summary class="psec-head">{@render head()}<span class="psec-caret" aria-hidden="true"><Icon name="down" size={12} /></span></summary>{@render children()}</details>
{:else}
  <section class="psec"><h3 class="psec-head">{@render head()}</h3>{@render children()}</section>
{/if}

<style>
  .psec { margin: 0 0 18px; }
  .psec-head { display: flex; align-items: center; gap: 8px; margin: 0 0 8px; padding-bottom: 6px; border-bottom: 1px solid var(--line);
    font-size: 11px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase; color: var(--text); }
  .psec-icon { display: inline-grid; place-items: center; width: 22px; height: 22px; border-radius: 6px; background: var(--accent-soft); color: var(--accent); }
  summary.psec-head { cursor: pointer; list-style: none; }
  summary.psec-head::-webkit-details-marker { display: none; }
  summary.psec-head:hover { color: var(--accent); }
  .psec-caret { margin-left: auto; color: var(--muted); display: inline-grid; transition: transform .15s; }
  details[open] > summary .psec-caret { transform: rotate(180deg); }
  .psec-note { margin-left: auto; font-weight: 400; letter-spacing: 0; text-transform: none; color: var(--muted); }
</style>
