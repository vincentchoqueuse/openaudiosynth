<!-- SidePanel.svelte — the panel that opens on the right of the Synth view when a row's Configure is pressed: a head
     (the item's name, what it is, the arrows to the previous and next row of the same table, a cross), then
     everything about the item, scrolling on its own. The page stays in view and playable beside it; on a narrow
     window it covers the page. Escape closes it. Placed between the top bar and the keyboard, measured from them. -->
<script>
  import { onMount } from 'svelte';
  let { title, sub = '', onclose, onprev = null, onnext = null, wide = false, head = null, children } = $props();
  let top = $state(60), bottom = $state(0);
  function measure() {
    const bar = document.querySelector('header.topbar'), foot = document.querySelector('.app > footer');
    top = bar ? Math.max(0, bar.getBoundingClientRect().bottom) : 60;
    bottom = foot ? Math.max(0, innerHeight - foot.getBoundingClientRect().top) : 0;
  }
  onMount(() => {
    measure();
    const ro = new ResizeObserver(measure);
    for (const el of [document.querySelector('header.topbar'), document.querySelector('.app > footer')]) if (el) ro.observe(el);
    return () => ro.disconnect();
  });
  function key(e) {
    if (e.key !== 'Escape' || e.defaultPrevented) return;
    if (document.querySelector('.select-list, .scrim')) return; // a list or a dialog closes first
    onclose();
  }
</script>

<svelte:window onkeydown={key} onresize={measure} />

<aside class="sidepanel" class:wide style="top:{top}px; bottom:{bottom}px" aria-label={title}>
  <div class="sp-head">
    <div class="sp-title"><h2>{title}</h2>{#if sub}<span class="sp-sub">{sub}</span>{/if}</div>
    {#if head}{@render head()}{/if}
    <span class="sp-nav">
      <button onclick={onprev} disabled={!onprev} aria-label="Previous" title="Previous row">↑</button>
      <button onclick={onnext} disabled={!onnext} aria-label="Next" title="Next row">↓</button>
      <button onclick={onclose} aria-label="Close" title="Close (Escape)">×</button>
    </span>
  </div>
  <div class="sp-body">{@render children()}</div>
</aside>
