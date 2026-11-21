<!-- Icon.svelte — a small drawing in a 16 x 16 box: a stroked path `d` (in the text color) and / or a filled one
     (`fill`). Every icon of the page goes through it (named ones from icons.js: `name`), so they share their weight
     and size. -->
<script>
  import { ICONS } from './icons.js';
  let { name = '', d = ICONS[name]?.d ?? '', fill = ICONS[name]?.fill ?? '', size = 16, weight = 1.5 } = $props();
</script>

<svg viewBox="0 0 16 16" width={size} height={size} aria-hidden="true">
  {#if d}<path {d} fill="none" stroke="currentColor" stroke-width={weight} stroke-linecap="round" stroke-linejoin="round" />{/if}
  {#if fill}<path d={fill} fill="currentColor" />{/if}
</svg>
