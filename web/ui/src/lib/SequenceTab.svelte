<!-- SequenceTab.svelte — the clip in the Clip view's Sequencer (it belongs to the sound: Save in the Menu keeps
     both): a toolbar (loop, position, grid; Init, Randomize, Settings; the tempo is in the top bar), then the piano roll
     with its editing tools. What is set once goes in the Clip settings dialog: the clip's length, its key and scale,
     the octave of Randomize, the loops of an exported song. -->
<script>
  import PianoRoll from './PianoRoll.svelte';
  import Switch from './Switch.svelte';
  import Select from './Select.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { sequence as q, LOOPS } from './sequence.svelte.js';
  import { NAMES, SCALES, GRIDS } from './seqExport.js';
  const BARS = [1, 2, 4, 8, 16], OCTAVES = [1, 2, 3, 4, 5];
  const summary = $derived(`${q.length} beats, ${NAMES[q.root]} ${q.scale}, octave ${q.octave}`);
</script>

<div class="toolbar">
  <Switch label="Loop" on={q.loop} onchange={(v) => (q.loop = v)} title={q.loop ? `the clip repeats every ${q.length} beats` : 'the clip plays once'} />
  <span class="pos mono" title="bar . beat . sixteenth">{q.position}</span>
  <label>Grid <Select bind:value={q.grid} label="Grid" options={GRIDS.map(([l, v]) => ({ value: v, label: l }))} /></label>
  <span class="spacer"></span>
  <button class="tool" onclick={q.clear} title="Clear the clip"><Icon name="init" size={13} /> Init</button>
  <button class="tool" onclick={q.random} title="New random notes in the clip, in its scale"><Icon name="dice" size={13} /> Randomize</button>
  <button class="tool" onclick={() => s.loadDemo()} disabled={!s.current} title="The sound's demo clip in the sequencer (clips/{s.current || 'its name.json'})"><Icon name="sequence" size={13} /> Demo</button>
  <button class="ghost set" onclick={() => (s.clipSettingsOpen = true)} title="Clip settings: {summary}"><Icon name="settings" weight={1.2} /> Settings</button>
</div>
{#if q.message}<p class="message">{q.message}</p>{/if}
<PianoRoll />
{#if s.clipSettingsOpen}
  <div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.clipSettingsOpen = false)}>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Clip settings">
      <div class="dhead"><h2>Clip settings</h2><button class="btn ghost" onclick={() => (s.clipSettingsOpen = false)}>Close</button></div>
      <label>Length <Select value={q.length} onchange={(v) => (q.length = v)} label="Length"
        options={[...(BARS.some((b) => b * 4 === q.length) ? [] : [{ value: q.length, label: `${q.length} beats` }]),
                  ...BARS.map((b) => ({ value: b * 4, label: `${b} bar${b > 1 ? 's' : ''}` }))]} /></label>
      <label>Key <span class="pair"><Select bind:value={q.root} label="Key" options={NAMES.map((n, i) => ({ value: i, label: n }))} />
        <Select bind:value={q.scale} label="Scale" options={Object.keys(SCALES).map((sc) => ({ value: sc, label: sc }))} /></span>
        <small>The notes outside the scale are shaded in the piano roll; Randomize stays in it.</small></label>
      <label>Octave <Select bind:value={q.octave} label="Octave" options={OCTAVES.map((o) => ({ value: o, label: String(o) }))} />
        <small>Where the piano roll opens and where Randomize writes its notes.</small></label>
      <label>Song <Select bind:value={q.loops} label="Times the clip plays in the exported song (Menu, Export song)" options={LOOPS.map((n) => ({ value: n, label: `× ${n}` }))} />
        <small>Times the clip plays in the exported song (Menu, Export song).</small></label>
    </div>
  </div>
{/if}
<p class="hint">Select notes, then ⌘C / ⌘V (the insertion point: click the ruler), arrows to move, Shift + arrows to resize; every shortcut: press ?</p>

<style>
  .pos { color: var(--text); font-size: 13px; min-width: 4.5em; }
  .set { display: inline-flex; align-items: center; gap: 6px; }
  .dhead { display: flex; align-items: center; justify-content: space-between; }
  .pair { display: flex; gap: 8px; }
  small { color: var(--muted); font-size: 11.5px; }
</style>
