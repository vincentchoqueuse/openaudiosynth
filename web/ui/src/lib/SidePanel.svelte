<!-- SidePanel.svelte — the panel that opens on the right of the Synth view when a row's Configure is pressed: a head
     (Back when it was opened from another panel's Edit; the item's name, what it is; the previous and next rows of the table, and close, as icon buttons),
     then its content (everything to set, then About, folded),
     scrolling on its own; a body may end with a footer of actions (`.sp-foot`), which stays in view. It lies over the
     page, which stays playable beside it, between the top bar and the keyboard (measured from them). Escape closes it. -->
<script>
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  let { title, sub = '', onclose, onprev = null, onnext = null, onback = null, backTo = '', wide = false, children } = $props();
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
    {#if onback}<button class="iconbtn sp-back" onclick={onback} aria-label="Back to {backTo}" title="Back to {backTo}"><Icon name="back" size={14} /></button>{/if}
    <div class="sp-title"><h2>{title}</h2>{#if sub}<span class="sp-sub">{sub}</span>{/if}</div>
    <span class="sp-nav">
      <button class="iconbtn" onclick={onprev} disabled={!onprev} aria-label="Previous" title="Previous row"><Icon name="up" size={14} /></button>
      <button class="iconbtn" onclick={onnext} disabled={!onnext} aria-label="Next" title="Next row"><Icon name="down" size={14} /></button>
      <button class="iconbtn" onclick={onclose} aria-label="Close" title="Close (Escape)"><Icon name="close" size={13} /></button>
    </span>
  </div>
  <div class="sp-body">{@render children()}</div>
</aside>
