<!-- FieldTable.svelte — the General tab of the Synth view and of the side panel: a record shown as a table of fields,
     each a name and its value (a value given as {value, set} is edited in place), in the same table style as every list of the page (DataTable, `.dt`). `head` names the
     two columns; a child, when given, is drawn as a last row across the table (the signal path of the sound). -->
<script>
  let { fields, head = ['Field', 'Value'], label = '', children = null } = $props(); // fields: [[name, value]]
</script>

<table class="dt kv" aria-label={label}>
  <colgroup><col style="width:28%" /><col /></colgroup>
  <thead><tr><th>{head[0]}</th><th>{head[1]}</th></tr></thead>
  <tbody>
    {#each fields as [k, v] (k)}
      <tr><td class="strong">{k}</td>
        <td>{#if v && typeof v === 'object'}<textarea class="sp-input" rows="3" value={v.value} placeholder={v.placeholder ?? ''} aria-label={k} oninput={(e) => v.set(e.target.value)}></textarea>{:else}{v}{/if}</td></tr>
    {/each}
    {#if children}<tr class="kv-wide"><td colspan="2">{@render children()}</td></tr>{/if}
  </tbody>
</table>
