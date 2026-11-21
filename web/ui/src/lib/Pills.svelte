<!-- Pills.svelte — a row of pills, one of them on (filled with the accent): the views of the top bar (size "lg"),
     the Synth view's tabs ("md", with an icon) and the drawn choices of a waveform or a shape ("icon": a drawing,
     its name in the tooltip). `tabs` makes it a tab list (pages), else a group of buttons (a value).
     items: [{id, label, icon? (a name in icons.js), draw? (a path of its own)}]. -->
<script>
  import Icon from './Icon.svelte';
  let { items, value, onpick, size = 'md', tabs = false, label = '' } = $props();
</script>

<div class="pills {size}" role={tabs ? 'tablist' : 'group'} aria-label={label}>
  {#each items as it (it.id)}
    <button role={tabs ? 'tab' : undefined} aria-selected={tabs ? value === it.id : undefined}
            aria-pressed={tabs ? undefined : value === it.id} class:on={value === it.id} onclick={() => onpick(it.id)}
            title={size === 'icon' ? it.label : undefined} aria-label={size === 'icon' ? it.label : undefined}>
      {#if it.icon || it.draw}<Icon name={it.icon} d={it.draw} size={size === 'md' ? 13 : 16} weight={size === 'icon' ? 1.4 : 1.5} />{/if}
      {#if size !== 'icon'}{it.label}{/if}
    </button>
  {/each}
</div>
