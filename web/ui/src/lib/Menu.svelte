<!-- Menu.svelte — a button that opens a menu below it: items {label, run, kbd?, disabled?} (run closes the menu
     first), links {label, href, kbd?}, section headings {heading} and separators ('-'). Closed by a click outside,
     Escape or a choice; the arrows move between items. It fits in the area that scrolls it (opening upward when
     there is more room above). One look for every menu of the page (the top bar's, the
     piano roll's). -->
<script>
  import Icon from './Icon.svelte';
  let { label, icon = null, items = [], align = 'left', title = '' } = $props();
  let open = $state(false), box, maxH = $state(400), up = $state(false);
  // the space left in the area that scrolls (and clips) the menu, below the button, or above it when that is larger
  function room() {
    let el = box.parentElement;
    while (el && el !== document.body && !/(auto|scroll|hidden)/.test(getComputedStyle(el).overflowY)) el = el.parentElement;
    const area = el && el !== document.body ? el.getBoundingClientRect() : { top: 0, bottom: window.innerHeight };
    const r = box.getBoundingClientRect(), below = area.bottom - r.bottom - 12, above = r.top - area.top - 12;
    up = above > below && below < 320;
    maxH = Math.max(120, up ? above : below);
  }

  function choose(it) { open = false; it.run?.(); }
  function toggle() {
    open = !open;
    if (open) room();
    if (open) queueMicrotask(() => box?.querySelector('[role=menuitem]:not(:disabled)')?.focus({ preventScroll: true }));
  }
  function nav(e) {
    if (!open) return;
    if (e.key === 'Escape') { open = false; e.stopPropagation(); return; }
    if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
    e.preventDefault();
    e.stopPropagation();
    const all = [...box.querySelectorAll('[role=menuitem]:not(:disabled)')], i = all.indexOf(document.activeElement);
    all[(i + (e.key === 'ArrowDown' ? 1 : all.length - 1)) % all.length]?.focus();
  }
</script>

<svelte:window onpointerdown={(e) => { if (open && !box?.contains(e.target)) open = false; }} />

<div class="menuwrap" bind:this={box} onkeydown={nav} role="presentation">
  <button class="btn ghost opener" onclick={toggle} aria-haspopup="menu" aria-expanded={open} {title}>
    {#if icon}<Icon name={icon} size={14} />{/if}{label}<span class="caret" aria-hidden="true">▾</span>
  </button>
  {#if open}
    <div class="menu" class:right={align === 'right'} class:up role="menu" aria-label={label} style="max-height:{maxH}px">
      {#each items as it}
        {#if it === '-'}<hr />
        {:else if it.heading}<div class="heading">{it.heading}</div>
        {:else if it.href}<a role="menuitem" href={it.href} target="_blank" rel="noopener noreferrer" onclick={() => (open = false)}>{it.label}{#if it.kbd}<kbd>{it.kbd}</kbd>{/if}</a>
        {:else}<button role="menuitem" disabled={it.disabled} onclick={() => choose(it)}>{it.label}{#if it.kbd}<kbd>{it.kbd}</kbd>{/if}</button>
        {/if}
      {/each}
    </div>
  {/if}
</div>

<style>
  .menuwrap { position: relative; }
  .opener { display: inline-flex; align-items: center; gap: 7px; }
  .caret { font-size: 9px; color: var(--muted); margin-left: -2px; }
  .menu { position: absolute; top: calc(100% + 6px); left: 0; z-index: 6; min-width: 220px; overflow-y: auto;
    display: grid; padding: 5px; background: var(--surface); border: 1px solid var(--line); border-radius: 10px;
    box-shadow: 0 8px 28px rgba(0, 0, 0, .25); }
  .menu.right { left: auto; right: 0; }
  .menu.up { top: auto; bottom: calc(100% + 6px); }
  .menu button, .menu a { display: flex; align-items: center; gap: 12px; text-align: left; padding: 6px 10px; border: 0;
    border-radius: 6px; background: none; font-size: 12.5px; color: var(--text); text-decoration: none; white-space: nowrap; }
  .menu button:hover:not(:disabled), .menu a:hover, .menu button:focus-visible, .menu a:focus-visible { background: var(--faint); outline: none; }
  .menu button:disabled { color: var(--muted); opacity: .6; }
  .menu hr { border: 0; border-top: 1px solid var(--line); margin: 4px 2px; }
  .menu .heading { padding: 7px 10px 3px; font: 10.5px var(--mono); letter-spacing: .04em; text-transform: uppercase; color: var(--muted); }
  .menu kbd { margin-left: auto; padding-left: 16px; font: 11px var(--mono); color: var(--muted); }
</style>
