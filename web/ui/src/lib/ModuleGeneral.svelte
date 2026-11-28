<!-- ModuleGeneral.svelte — the General tab of the side panel: a module as a record (FieldTable): what it is, what it
     does (about.js), where its signal comes from and goes, or what it moves and what moves it, whether it is on;
     then each of its settings with what it does (the descriptor's help text), in the same kind of table. -->
<script>
  import FieldTable from './FieldTable.svelte';
  import { about } from './about.js';
  import Section from './Section.svelte';
  let { kind, row, fields = [], params = [] } = $props(); // fields: [[name, value]] beyond what it does; params: [{d, label}]
  const all = $derived([['What it does', about(kind, row) || '—'], ...fields]);
  const help = $derived(params.map(({ d, label }) => [label, d.help || '—']));
</script>

<Section icon="summary" title="About"><FieldTable label="General" fields={all} /></Section>
{#if help.length}<Section icon="sliders" title="Its settings" note="{help.length}"><FieldTable label="Its settings" head={['Setting', 'What it does']} fields={help} /></Section>{/if}
