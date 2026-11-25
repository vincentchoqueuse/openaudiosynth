<!-- Switch.svelte — every on / off of the page: a parameter, a stage of the signal chain, the clip's loop, an LFO's
     restart. `disabled`: shown but fixed (a stage always on). With a `label`, the words come first (in a toolbar); without, the row or the card says what it is. -->
<script>
  let { on, onchange, label = '', name = label, title = '', disabled = false } = $props();
</script>

{#if label}
  <span class="switch" {title}>{label}<button class="onoff" class:on role="switch" aria-checked={on} aria-label={name} {disabled} onclick={() => onchange(!on)}></button></span>
{:else}
  <button class="onoff" class:on role="switch" aria-checked={on} aria-label={name} {disabled} title={title || (on ? 'on' : 'off')} onclick={() => onchange(!on)}></button>
{/if}
