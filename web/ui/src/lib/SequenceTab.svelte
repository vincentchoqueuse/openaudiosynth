<!-- SequenceTab.svelte — the clip in the Clip view's Sequencer (it belongs to the sound: Save in the Menu keeps
     both): a first toolbar (loop, position; the tempo is in the top bar; Random, Clear, the loops of an exported song), then the piano roll with its toolbar (the clip's length,
     grid, key and octave on the left, the editing tools on the right). -->
<script>
  import PianoRoll from './PianoRoll.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, GRIDS } from './seqExport.js';
  const BARS = [1, 2, 4, 8, 16], OCTAVES = [1, 2, 3, 4, 5];
</script>

<div class="toolbar">
  <Switch label="Loop" on={q.loop} onchange={(v) => (q.loop = v)} title={q.loop ? `the clip repeats every ${q.length} beats` : 'the clip plays once'} />
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <span class="spacer"></span>
  <button class="ghost" onclick={q.random}>Random</button>
  <button class="ghost" onclick={q.clear}>Clear</button>
  <label>Song <Select bind:value={q.loops} label="Times the clip plays in the exported song (File, Export song)" options={LOOPS.map((n) => ({ value: n, label: `× ${n}` }))} /></label>
</div>
{#if q.message}<p class="message">{q.message}</p>{/if}
<PianoRoll>
  {#snippet settings()}
    <label>Length <Select value={q.length} onchange={(v) => (q.length = v)} label="Length"
      options={[...(BARS.some((b) => b * 4 === q.length) ? [] : [{ value: q.length, label: `${q.length} beats` }]),
                ...BARS.map((b) => ({ value: b * 4, label: `${b} bar${b > 1 ? 's' : ''}` }))]} /></label>
    <label>Grid <Select bind:value={q.grid} label="Grid" options={GRIDS.map(([l, v]) => ({ value: v, label: l }))} /></label>
    <label>Key <Select bind:value={q.root} label="Key" options={NAMES.map((n, i) => ({ value: i, label: n }))} />
      <Select bind:value={q.scale} label="Scale" options={Object.keys(SCALES).map((sc) => ({ value: sc, label: sc }))} /></label>
    <label>Octave <Select bind:value={q.octave} label="Octave" options={OCTAVES.map((o) => ({ value: o, label: String(o) }))} /></label>
  {/snippet}
</PianoRoll>
<p class="hint">Select notes, then ⌘C / ⌘V (the insertion point: click the ruler), arrows to move, Shift + arrows to resize; every shortcut: press ?</p>

<style>
  .pos { color: var(--text); font-size: 13px; min-width: 4.5em; }
</style>
