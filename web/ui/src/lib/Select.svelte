<!-- Select.svelte — the page's choice menu, in place of the native <select> (whose look macOS imposes): a field that
     shows the chosen label and opens a list with the same look as Menu.svelte. Options are {value, label, disabled?}
     and section headings {heading}; `value` is bindable and `onchange(value)` is called on a choice. The list is
     placed in the window (fixed), below the field or above it when there is more room, and scrolled to the choice;
     arrows, Home/End, Enter, Escape and typing the start of a label work as in a native menu. -->
<script>
  let { value = $bindable(), options = [], onchange = null, disabled = false, label = '', title = '', class: cls = '' } = $props();
  let open = $state(false), field, list, active = $state(-1), place = $state(''), typed = '', typedAt = 0;

  const picks = $derived(options.map((o, i) => ({ o, i })).filter(({ o }) => !o.heading && !o.disabled).map(({ i }) => i));
  const current = $derived(options.find((o) => !o.heading && o.value === value));

  function show() {
    if (disabled) return;
    const r = field.getBoundingClientRect(), below = innerHeight - r.bottom - 12, above = r.top - 12;
    const up = above > below && below < 260, h = Math.max(120, Math.min(360, up ? above : below));
    const w = Math.max(r.width, 140), left = Math.min(r.left, innerWidth - w - 8);
    place = `left:${Math.max(8, left)}px;min-width:${w}px;max-height:${h}px;` +
            (up ? `bottom:${innerHeight - r.top + 4}px` : `top:${r.bottom + 4}px`);
    active = options.indexOf(current);
    if (active < 0) active = picks[0] ?? -1;
    open = true;
    queueMicrotask(() => reveal(true));
  }
  function close(refocus = true) { open = false; if (refocus) field?.focus({ preventScroll: true }); }
  function choose(i) {
    const o = options[i];
    if (!o || o.heading || o.disabled) return;
    close();
    if (o.value === value) return;
    value = o.value;
    onchange?.(o.value);
  }
  // scrolls the list alone (scrollIntoView would scroll the page too, which closes the list)
  function reveal(center) {
    const el = list?.querySelector('.active');
    if (!el) return;
    const top = el.offsetTop, bottom = top + el.offsetHeight, h = list.clientHeight;
    if (center) list.scrollTop = top - (h - el.offsetHeight) / 2;
    else if (top < list.scrollTop) list.scrollTop = top - 5;
    else if (bottom > list.scrollTop + h) list.scrollTop = bottom - h + 5;
  }
  function move(to) {
    if (!picks.length) return;
    active = to;
    queueMicrotask(() => reveal(false));
  }
  function key(e) {
    if (disabled) return;
    const k = e.key, at = picks.indexOf(active);
    if (!open) {
      if (k === 'ArrowDown' || k === 'ArrowUp' || k === 'Enter' || k === ' ') { e.preventDefault(); show(); }
      return;
    }
    if (k === 'Escape' || k === 'Tab') { if (k === 'Escape') e.preventDefault(); e.stopPropagation(); close(k === 'Escape'); }
    else if (k === 'ArrowDown') { e.preventDefault(); move(picks[Math.min(picks.length - 1, at + 1)]); }
    else if (k === 'ArrowUp') { e.preventDefault(); move(picks[Math.max(0, at - 1)]); }
    else if (k === 'Home' || k === 'PageUp') { e.preventDefault(); move(picks[0]); }
    else if (k === 'End' || k === 'PageDown') { e.preventDefault(); move(picks[picks.length - 1]); }
    else if (k === 'Enter' || k === ' ') { e.preventDefault(); choose(active); }
    else if (k.length === 1 && !e.metaKey && !e.ctrlKey) {
      // type-ahead: the first option whose label starts with what was typed in the last second
      const now = performance.now();
      typed = (now - typedAt < 1000 ? typed : '') + k.toLowerCase();
      typedAt = now;
      const hit = picks.find((i) => String(options[i].label).toLowerCase().startsWith(typed));
      if (hit !== undefined) move(hit);
    }
    if (open) e.stopPropagation(); // the page's shortcuts (piano roll, keyboard) stay out of an open list
  }
</script>

<svelte:window
  onpointerdown={(e) => { if (open && !field?.contains(e.target) && !list?.contains(e.target)) close(false); }}
  onscrollcapture={(e) => { if (open && !list?.contains(e.target)) close(false); }}
  onresize={() => open && close(false)} />

<button bind:this={field} type="button" class="select {cls}" class:open {disabled} {title} aria-label={label || null}
        aria-haspopup="listbox" aria-expanded={open} onclick={() => (open ? close() : show())} onkeydown={key}>
  <span class="text">{current?.label ?? ''}</span><span class="caret" aria-hidden="true">▾</span>
</button>
{#if open}
  <div class="select-list" bind:this={list} role="listbox" aria-label={label || null} style={place} tabindex="-1">
    {#each options as o, i}
      {#if o.heading}<div class="heading" role="presentation">{o.heading}</div>
      {:else}
        <div role="option" aria-selected={o.value === value} aria-disabled={o.disabled || null} class:on={o.value === value}
             class:active={i === active} class:disabled={o.disabled}
             onpointerenter={() => !o.disabled && (active = i)} onpointerup={() => choose(i)}>
          <span class="check" aria-hidden="true">{o.value === value ? '✓' : ''}</span>{o.label}
        </div>
      {/if}
    {/each}
  </div>
{/if}

<style>
  .select { display: inline-flex; align-items: center; gap: 6px; min-width: 0; max-width: 100%; padding: 3px 6px 3px 8px;
    border-radius: 6px; background: var(--faint); color: var(--text); font-size: 12px; text-align: left; line-height: 1.45; }
  .select:hover:not(:disabled), .select.open { background: color-mix(in srgb, var(--faint) 70%, var(--track)); }
  .text { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .caret { flex: none; font-size: 9px; color: var(--muted); }
  .select-list { position: fixed; z-index: 50; overflow-y: auto; padding: 5px; background: var(--surface);
    border: 1px solid var(--line); border-radius: 10px; box-shadow: 0 8px 28px rgba(0, 0, 0, .25); outline: none;
    max-width: min(420px, calc(100vw - 16px)); }
  [role=option] { display: flex; align-items: center; gap: 4px; padding: 5px 10px 5px 4px; border-radius: 6px;
    font-size: 12.5px; color: var(--text); white-space: nowrap; cursor: default; user-select: none; }
  [role=option].active { background: var(--faint); }
  [role=option].on { color: var(--accent); }
  [role=option].disabled { color: var(--muted); opacity: .6; }
  .check { flex: none; width: 16px; text-align: center; font-size: 11px; }
  .heading { padding: 7px 10px 3px 24px; font: 10.5px var(--mono); letter-spacing: .04em; text-transform: uppercase; color: var(--muted); }
</style>
