<!-- Seg.svelte — a segmented control: a mode or a value among a few words (one shot / loop held / loop free, Hz /
     beats, Part / Each note…). `wide` fills its cell (a parameter row).
     items: [{id, label, title?}]. -->
<script>
  let { items, value, onpick, wide = false, label = '' } = $props();
</script>

<div class="seg" class:wide role="group" aria-label={label}>
  {#each items as it (it.id)}
    <button class:on={value === it.id} aria-pressed={value === it.id} title={it.title} onclick={() => onpick(it.id)}>
      {it.label}
    </button>
  {/each}
</div>
