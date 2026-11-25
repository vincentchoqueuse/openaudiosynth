<!-- MacroTable.svelte — the Macros page: the eight macros as a table, each its name, where it stands, the MIDI CC
     that turns it and the settings it moves; Configure opens it in the side panel (MacroPanel). They are played
     above the keyboard (MacroStrip); Wander, which moves them on its own, is a modulator. -->
<script>
  import DataTable from './DataTable.svelte';
  import { session as s } from './session.svelte.js';
  import { human } from './naming.js';

  const rows = $derived(s.macroNames.map((name, k) => {
    const targets = s.macroMaps.filter((r) => r.macro === k && r.dst).map((r) => human(r.dst.name));
    return { key: 'm' + k, k, name, targets, dim: !name && !targets.length };
  }));
  const columns = [{ id: 'num', label: '#', w: '40px', cls: 'muted mono' }, { id: 'name', label: 'Macro', w: '20%' },
                   { id: 'value', label: 'Value', w: '16%' }, { id: 'cc', label: 'CC', w: '60px', cls: 'muted mono' }, { id: 'moves', label: 'Moves', cls: 'sum' }];
</script>

<DataTable label="Macros" {columns} {rows} selected={s.panel?.kind === 'macro' ? s.panel.id : null} onopen={(r) => s.openPanel('macro', r.key)}>
  {#snippet cell(r, c)}
    {#if c.id === 'num'}{r.k + 1}
    {:else if c.id === 'name'}<span class="strong">{s.macroName(r.k)}</span>
    {:else if c.id === 'value'}<span class="dt-bar"><span style="width:{s.getP(`macro${r.k + 1}`) * 100}%"></span></span>
    {:else if c.id === 'cc'}{Math.round(s.getP(`macro${r.k + 1}_cc`)) || '—'}
    {:else}<span class:off={!r.targets.length}>{r.targets.length ? r.targets.join(', ') : 'nothing yet'}</span>{/if}
  {/snippet}
</DataTable>
