<!-- ShortcutsDialog.svelte — every shortcut of the page on one sheet, opened with ? (or the ? in the top bar),
     closed with Escape or a click outside. -->
<script>
  import { session as s } from './session.svelte.js';
  const GROUPS = [
    ['Anywhere', [['Space', 'play / stop the clip'], ['⌘Z / ⇧⌘Z', 'undo / redo'], ['?', 'this sheet'], ['Esc', 'close a sheet or a menu']]],
    ['Computer keyboard', [['Z S X D C V…', 'notes, bottom row from C'], ['Q 2 W 3 E…', 'notes, an octave up'], ['← →', 'octave down / up']]],
    ['Piano roll (when the Sequence view is shown)', [['click, drag', 'draw a note, stretch it'], ['⌘-drag', 'select with a rectangle'],
      ['Shift-click', 'add to the selection'], ['drag a note / its edge', 'move / stretch the selection'], ['Alt-drag', 'move a copy'],
      ['click the ruler', 'insertion point: where Paste lands'], ['double-click, right-click', 'delete a note'],
      ['Delete', 'delete the selection'], ['⌘A ⌘C ⌘X ⌘V', 'select all, copy, cut, paste (the clipboard stays across sounds)'],
      ['⌘D', 'duplicate after the selection'], ['⌘L', 'legato: each note until the next'], ['⌘U', 'quantize'],
      ['↑ ↓ (Shift)', 'the selection a semitone (an octave)'], ['← → (Alt)', 'the selection a grid step (finer)'],
      ['Shift ← →', 'shorten / lengthen the selection'], ['Edit, Pitch, Time, Velocity', 'the menus above the roll: on the selection, or every note'], ['Shift while dragging', 'finer than the grid'], ['velocity lane', 'drag on a bar']]],
    ['Controls', [['double-click a label or slider', 'back to the sound\'s value'], ['click a block of Structure', 'open its settings']]],
  ];
</script>

<div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.shortcutsOpen = false)}>
  <div class="dialog sheet" role="dialog" aria-modal="true" aria-label="Shortcuts">
    <div class="dhead"><h2>Shortcuts</h2><button class="btn ghost" onclick={() => (s.shortcutsOpen = false)}>Close</button></div>
    {#each GROUPS as [title, rows]}
      <h3>{title}</h3>
      <dl>{#each rows as [k, what]}<dt class="mono">{k}</dt><dd>{what}</dd>{/each}</dl>
    {/each}
  </div>
</div>

<style>
  .sheet { width: min(620px, 94vw); max-height: 86vh; overflow-y: auto; }
  .dhead { display: flex; align-items: center; justify-content: space-between; }
  .dhead h2 { margin: 0; }
  dl { display: grid; grid-template-columns: 15em 1fr; gap: 5px 14px; margin: 0; font-size: 12.5px; }
  dt { color: var(--text); }
  dd { margin: 0; color: var(--muted); }
</style>
