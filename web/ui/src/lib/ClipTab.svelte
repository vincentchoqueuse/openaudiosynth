<!-- ClipTab.svelte — the Sequence view, a record with two tabs: Clip (the Sequencer: the clip, its tempo, loop and
     position, its piano roll; play / stop is in the top bar) and Code (the clip as text: CodeView). The scope is in
     the Live player. -->
<script>
  import SequenceTab from './SequenceTab.svelte';
  import Tabs from './Tabs.svelte';
  import CodeView from './CodeView.svelte';
  import { session as s } from './session.svelte.js';
  import { remembered, remember } from './prefs.js';
  let tab = $state(remembered('synth.seqtab', 'clip'));
  $effect(() => remember('synth.seqtab', tab));
</script>

{#if !s.phone}<Tabs items={[{ id: 'clip', label: 'Clip' }, { id: 'code', label: 'Code' }]} value={tab} onpick={(id) => (tab = id)} label="Sequence" />{/if}
{#if tab === 'code' && !s.phone}<CodeView doc="clip" />{/if}

{#if !s.phone}<div hidden={tab === 'code'}><SequenceTab /></div>{/if} <!-- a phone plays the clip (top bar), it does not edit it -->

