<!-- Seg.svelte — a segmented control: a mode or a value among a few words (one shot / loop held / loop free, Hz /
     beats, Part / Each note…). `wide` fills its cell (a parameter row); a `dot` marks an item in use.
     items: [{id, label, title?, dot?}]. -->
<script>
  let { items, value, onpick, wide = false, label = '' } = $props();
</script>

<div class="seg" class:wide role="group" aria-label={label}>
  {#each items as it (it.id)}
    <button class:on={value === it.id} aria-pressed={value === it.id} title={it.title} onclick={() => onpick(it.id)}>
      {it.label}{#if it.dot}<span class="dot" title="in use"></span>{/if}
    </button>
  {/each}
</div>
