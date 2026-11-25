<!-- InstrumentTable.svelte — the Instrument page: the instrument's stages as a table, in the order its signal flows
     (sources, filters, amp, then its own modulators and the voice): each its name, its role, its switch when it
     can be switched off, the line of its main settings; Configure opens all of them in the side panel. -->
<script>
  import DataTable from './DataTable.svelte';
  import Switch from './Switch.svelte';
  import { session as s } from './session.svelte.js';
  import { engineStages, summary, stageOn, toggleStage, ROLE } from './stages.js';

  const rows = $derived(engineStages(s));
  const columns = [{ id: 'name', label: 'Stage', w: '22%' }, { id: 'role', label: 'Role', w: '12%', cls: 'muted' },
                   { id: 'on', label: 'On', w: '56px' }, { id: 'sum', label: 'Settings', cls: 'sum' }];
</script>

<DataTable label="Stages of the instrument" {columns} {rows} selected={s.panel?.kind === 'stage' ? s.panel.id : null}
           onopen={(r) => s.openPanel('stage', r.key)}>
  {#snippet cell(r, c)}
    {#if c.id === 'name'}<span class="strong">{r.title}</span>
    {:else if c.id === 'role'}{ROLE[r.st?.kind] ?? ''}
    {:else if c.id === 'on'}{#if r.st?.enable}<Switch on={stageOn(s, r.st)} onchange={() => toggleStage(s, r.st)} name="{r.title} on/off" />{/if}
    {:else}<span class:off={!stageOn(s, r.st)}>{stageOn(s, r.st) ? summary(s, r) : 'off'}</span>{/if}
  {/snippet}
</DataTable>
