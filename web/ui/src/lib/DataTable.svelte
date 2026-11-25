<!-- DataTable.svelte — every list of the Synth view as one kind of table (stages of the instrument, effects,
     modulators, routes, macros): a head, one row per item with a few columns that say what it is and what it does,
     and a Configure button (the cog) at the end that opens the item in the side panel (SidePanel.svelte). A click
     anywhere on a row opens it too, but for its own controls (a switch, a list). Rows may carry a `group`: a heading
     row is drawn where it changes. The row open in the panel is lit.
     columns: [{id, label, w? (a CSS width), cls?}]; rows: [{key, group?, dim?, open? (false: no Configure)}];
     cell(row, column) draws a cell. With `drag` ({can(row), move(from, to)}), the rows it allows are dragged by their
     handle (⠿, in the first cell) onto one another to reorder them. -->
<script>
  import Icon from './Icon.svelte';
  let { columns, rows, cell, selected = null, onopen, label = '', empty = '', drag = null } = $props();
  let from = $state(null), over = $state(null); // the row dragged, the row under it
  const control = (e) => e.target.closest('button, input, select, label, a, .select, .grip, [role="switch"]');
  function click(e, r) { if (r.open !== false && !control(e)) onopen(r); }
</script>

<table class="dt" aria-label={label}>
  <colgroup>{#each columns as c (c.id)}<col style={c.w ? `width:${c.w}` : ''} />{/each}<col style="width:44px" /></colgroup>
  <thead><tr>{#each columns as c (c.id)}<th class={c.cls ?? ''}>{c.label}</th>{/each}<th></th></tr></thead>
  <tbody>
    {#each rows as r, i (r.key)}
      {#if r.group && r.group !== rows[i - 1]?.group}
        <tr class="dt-group"><td colspan={columns.length + 1}>{r.group}</td></tr>
      {/if}
      {@const dr = !!drag?.can(r)}
      <tr class:sel={selected === r.key} class:dim={r.dim} class:openable={r.open !== false} class:over={over === r.key && from?.key !== r.key}
          class:dragging={from?.key === r.key} onclick={(e) => click(e, r)}
          ondragover={(e) => { if (from && dr) { e.preventDefault(); over = r.key; } }}
          ondragleave={() => { if (over === r.key) over = null; }}
          ondrop={(e) => { e.preventDefault(); if (from && dr) drag.move(from, r); from = over = null; }}>
        {#each columns as c, ci (c.id)}<td class={c.cls ?? ''}>
          {#if ci === 0 && dr}<span class="grip" draggable="true" title="Drag to move it in the chain" role="button" tabindex="-1" aria-label="Move"
                ondragstart={(e) => { from = r; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', r.key); }}
                ondragend={() => (from = over = null)}>⠿</span>{/if}{@render cell(r, c)}</td>{/each}
        <td class="dt-cog">
          {#if r.open !== false}
            <button class="cog" onclick={() => onopen(r)} aria-label="Configure" title="Configure"><Icon name="settings" size={14} /></button>
          {/if}
        </td>
      </tr>
    {/each}
    {#if !rows.length && empty}<tr class="dt-empty"><td colspan={columns.length + 1}>{empty}</td></tr>{/if}
  </tbody>
</table>
