// seqExport.js — pure functions of the step sequencer, shared by the UI and web/test_seq_export.mjs: the rows of
// scale notes, one phrase per row in the audition syntax (docs/patch_format.md: note, '!' accent, '?' soft,
// '_' held, '-' rest), and a self-contained song (songs/ format) with the sound as an inline patch, one track per
// row so every note keeps its own length.

export const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const SCALES = {
  minor: [0, 2, 3, 5, 7, 8, 10], phrygian: [0, 1, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10],
  'minor pentatonic': [0, 3, 5, 7, 10], 'harmonic minor': [0, 2, 3, 5, 7, 8, 11], major: [0, 2, 4, 5, 7, 9, 11],
  lydian: [0, 2, 4, 6, 7, 9, 11], chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
};

export const noteName = (n) => NAMES[n % 12] + (Math.floor(n / 12) - 1);

// MIDI notes of the grid rows, high notes first
export function scaleRows(root, scale, octave, span) {
  const lo = 12 * (octave + 1) + root, out = [];
  for (let o = 0; o < span; o++) for (const i of SCALES[scale] ?? SCALES.minor) out.push(lo + 12 * o + i);
  out.push(lo + 12 * span);
  return out.reverse();
}

// cells: {"note:step": 'on' | 'acc' | 'soft' | 'tie'} -> phrases of the rows that play, low notes first
export function phrases(cells, notes, steps) {
  const out = [];
  for (const n of [...notes].sort((a, b) => a - b)) {
    let any = false;
    const t = [];
    for (let s = 0; s < steps; s++) {
      const c = cells[`${n}:${s}`];
      if (c === 'tie' && s > 0 && t[s - 1] !== '-') t.push('_');
      else if (c) { t.push(noteName(n) + (c === 'acc' ? '!' : c === 'soft' ? '?' : '')); any = true; }
      else t.push('-');
    }
    if (any) out.push(t.join(' '));
  }
  return out;
}

// the notes that cells use (also those outside the current scale view)
export const usedNotes = (cells) => [...new Set(Object.keys(cells).map((k) => +k.split(':')[0]))];

/* Song for synth_render --song: s = {name, bpm, steps, stepLen, gate, swing, cells, notes, loops} (notes: the
 * grid rows, default every note used), patch = an inline patch object ({version, engine, name, params}).
 * Each played row is one track of the same patch. */
export function buildSong(s, patch) {
  const rows = phrases(s.cells, s.notes ?? usedNotes(s.cells), s.steps);
  const loops = Math.max(1, s.loops ?? 4);
  return {
    version: 1,
    name: s.name,
    description: `Step sequence exported from the web sequencer: ${rows.length} row(s), ${s.steps} steps of ` +
      `${s.stepLen} beat at ${s.bpm} BPM, played ${loops} times, on the sound "${patch.name}" (inline patch).`,
    bpm: s.bpm,
    length_beats: s.steps * s.stepLen * loops,
    tail_s: 8,
    tracks: rows.map((notes) => ({
      patch, notes, step: s.stepLen, repeat: loops, gate: s.gate, swing: s.swing, velocity: 100, reverb_send: 0,
    })),
    master: { fade_in_s: 0, fade_out_s: 4 },
  };
}
