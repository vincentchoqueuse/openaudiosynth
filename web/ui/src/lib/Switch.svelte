<!-- Switch.svelte — every on / off of the page: a parameter, a stage of the signal chain, the clip's loop, an LFO's
     restart. With a `label`, the words come first (in a toolbar); without, the row or the card says what it is. -->
<script>
  let { on, onchange, label = '', name = label, title = '' } = $props();
</script>

{#if label}
  <span class="switch" {title}>{label}<button class="onoff" class:on role="switch" aria-checked={on} aria-label={name} onclick={() => onchange(!on)}></button></span>
{:else}
  <button class="onoff" class:on role="switch" aria-checked={on} aria-label={name} title={title || (on ? 'on' : 'off')} onclick={() => onchange(!on)}></button>
{/if}
