<!-- Section.svelte — one part of a side panel, the same everywhere: an icon and a title on one line (an optional
     note on the right), then its content. The panels are built from these, in the order they are worked: what it is
     (its effect, its shape), its settings, what moves it or what it moves. -->
<script>
  import Icon from './Icon.svelte';
  let { icon, title, note = '', children } = $props();
</script>

<section class="psec">
  <h3 class="psec-head"><span class="psec-icon" aria-hidden="true"><Icon name={icon} size={13} /></span>{title}{#if note}<span class="psec-note">{note}</span>{/if}</h3>
  {@render children()}
</section>

<style>
  .psec { margin: 0 0 18px; }
  .psec-head { display: flex; align-items: center; gap: 8px; margin: 0 0 8px; padding-bottom: 6px; border-bottom: 1px solid var(--line);
    font-size: 11px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase; color: var(--text); }
  .psec-icon { display: inline-grid; place-items: center; width: 22px; height: 22px; border-radius: 6px; background: var(--accent-soft); color: var(--accent); }
  .psec-note { margin-left: auto; font-weight: 400; letter-spacing: 0; text-transform: none; color: var(--muted); }
</style>
