<!-- DataTable.svelte — every list of the Synth view as one kind of table (stages of the instrument, effects,
     modulators, routes, macros): a head, one row per item with a few columns that say what it is and what it does,
     and a Configure button (the cog) at the end that opens the item in the side panel (SidePanel.svelte); with a
     `detail` snippet the row unfolds in place instead (a chevron, the keys open in `expanded`). A click
     anywhere on a row opens it too, but for its own controls (a switch, a list). Rows may carry a `group`: a heading
     row is drawn where it changes. The row open in the panel is lit.
     columns: [{id, label, w? (a CSS width), cls?}]; rows: [{key, group?, dim?, open? (false: no Configure)}];
     cell(row, column) draws a cell. A column with `sort: true` has a head that sorts on a click (up, down, then the
     table's own order again); `sort` ({id, dir}) is the caller's, who orders the rows. `foot`: a line under the table. -->
<script>
  import Icon from './Icon.svelte';
  let { columns, rows, cell, selected = null, onopen, label = '', empty = '', sort = $bindable(null), foot = '', detail = null, expanded = [] } = $props();
  // with `detail`, a row unfolds in place (its key in `expanded`, onopen toggles it) and a chevron says so, not a cog
  function bySort(c) { sort = sort?.id !== c.id ? { id: c.id, dir: 1 } : sort.dir === 1 ? { id: c.id, dir: -1 } : null; }
  const control = (e) => e.target.closest('button, input, select, label, a, .select, [role="switch"]');
  function click(e, r) { if (r.open !== false && !control(e)) onopen(r); }
</script>

<table class="dt" aria-label={label}>
  <colgroup>{#each columns as c (c.id)}<col style={c.w ? `width:${c.w}` : ''} />{/each}<col style="width:44px" /></colgroup>
  <thead><tr>{#each columns as c (c.id)}<th class={c.cls ?? ''} aria-sort={sort?.id === c.id ? (sort.dir === 1 ? 'ascending' : 'descending') : null}>
    {#if c.sort}<button class="th-sort" class:on={sort?.id === c.id} onclick={() => bySort(c)} title="Sort by {(c.title ?? c.label).toLowerCase()}">{c.label}<span class="arrow">{sort?.id === c.id ? (sort.dir === 1 ? '↑' : '↓') : ''}</span></button>{:else}{c.label}{/if}</th>{/each}<th></th></tr></thead>
  <tbody>
    {#each rows as r, i (r.key)}
      {#if r.group && r.group !== rows[i - 1]?.group}
        <tr class="dt-group"><td colspan={columns.length + 1}>{r.group}</td></tr>
      {/if}
      <tr class:sel={selected === r.key} class:dim={r.dim} class:openable={r.open !== false} onclick={(e) => click(e, r)}>
        {#each columns as c (c.id)}<td class={c.cls ?? ''}>{@render cell(r, c)}</td>{/each}
        <td class="dt-cog">
          {#if r.open !== false}
            {#if detail}<button class="cog chev" class:open={expanded.includes(r.key)} onclick={() => onopen(r)} aria-expanded={expanded.includes(r.key)}
                                aria-label={expanded.includes(r.key) ? 'Fold' : 'Unfold to edit'} title={expanded.includes(r.key) ? 'Fold' : 'Edit here'}><Icon name="down" size={14} /></button>
            {:else}<button class="cog" onclick={() => onopen(r)} aria-label="Configure" title="Configure"><Icon name="settings" size={14} /></button>{/if}
          {/if}
        </td>
      </tr>
      {#if detail && expanded.includes(r.key)}<tr class="dt-detail"><td colspan={columns.length + 1}><div class="dt-unfold">{@render detail(r)}</div></td></tr>{/if}
    {/each}
    {#if !rows.length && empty}<tr class="dt-empty"><td colspan={columns.length + 1}>{empty}</td></tr>{/if}
  </tbody>
</table>
{#if foot}<p class="dt-foot">{foot}</p>{/if}
