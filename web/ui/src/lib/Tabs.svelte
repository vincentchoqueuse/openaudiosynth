<!-- Tabs.svelte — the small underlined tabs of an editor (its own views: Names / Assignments, the instrument /
     Effects…). Every pill of the Synth view has a row of them, even with a single tab. items: [{id, label}]. -->
<script>
  let { items, value, onpick, label = '' } = $props();
</script>

<div class="tabs" role="tablist" aria-label={label}>
  {#each items as it (it.id)}
    <button role="tab" aria-selected={value === it.id} class:on={value === it.id} onclick={() => onpick(it.id)}>{it.label}</button>
  {/each}
</div>
