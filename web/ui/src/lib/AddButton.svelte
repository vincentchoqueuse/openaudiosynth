<!-- AddButton.svelte — the one way a panel's section adds a link of modulation, under its table: "+ Add", a button
     when there is one way to add, a menu of them when there are several (a route on the whole sound or on each
     note, a slot of the instrument's matrix). items: [{label, run, disabled?}]. -->
<script>
  import Icon from './Icon.svelte';
  import Menu from './Menu.svelte';
  let { items } = $props();
  const live = $derived(items.filter((it) => !it.disabled));
</script>

<div class="addrow">
  {#if items.length > 1 && live.length > 1}<Menu label="Add" icon="plus" {items} />
  {:else}{@const it = live[0] ?? items[0]}
    <button class="btn ghost" onclick={() => it?.run()} disabled={!live.length} title={it?.label}><Icon name="plus" size={12} /> Add</button>
  {/if}
</div>

<style>
  .addrow { margin-top: 8px; display: flex; }
  .addrow :global(.btn) { display: inline-flex; align-items: center; gap: 6px; padding: 0 12px; font-size: 12px; }
</style>
