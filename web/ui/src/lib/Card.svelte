<!-- Card.svelte — every card of the page: a surface with a border, an optional small title, its content.
     `plain` drops the border and the surface (the macros above the keyboard); any other attribute (a class, an
     aria-label, drag events) goes to the section. -->
<script>
  let { title = '', plain = false, class: cls = '', children, ...rest } = $props();
</script>

<section class="card {cls}" class:plain {...rest}>
  {#if title}<h2>{title}</h2>{/if}
  {@render children()}
</section>
