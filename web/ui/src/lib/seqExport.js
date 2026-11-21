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

/* ---- the sequence as a readable document (the panel's JSON view) ----
 * {name, bpm, step: "1/8", steps, gate, swing, key: "E minor", octave, range,
 *  rows: {"B4": "x... X... o__. ...."}}  rows high to low, only those that play, steps in groups of four;
 * x note, X accent, o soft, _ held (after a note), . rest; spaces and | are free. */
export const STEP_LENGTHS = [['1/32', 0.125], ['1/16', 0.25], ['1/8', 0.5], ['1/4', 1], ['1/2', 2], ['1 bar', 4]];
export const STEP_COUNTS = [8, 12, 16, 24, 32];
const CELL_CHAR = { on: 'x', acc: 'X', soft: 'o', tie: '_' };
const CHAR_CELL = { x: 'on', X: 'acc', o: 'soft', _: 'tie' };

export function toDoc(s) {
  const rows = {};
  for (const n of scaleRows(s.root, s.scale, s.octave, s.span)) {
    const t = [];
    let any = false;
    for (let k = 0; k < s.steps; k++) {
      const c = s.cells[`${n}:${k}`];
      if (c) any = true;
      t.push((k && k % 4 === 0 ? ' ' : '') + (CELL_CHAR[c] ?? '.'));
    }
    if (any) rows[noteName(n)] = t.join('');
  }
  return {
    name: s.name || 'untitled', bpm: s.bpm, step: (STEP_LENGTHS.find((l) => l[1] === s.stepLen) ?? ['1/8'])[0],
    steps: s.steps, gate: s.gate, swing: s.swing, key: `${NAMES[s.root]} ${s.scale}`, octave: s.octave, range: s.span, rows,
  };
}

// note name -> MIDI ("C#4" -> 61), -1 if not one
function noteNumber(t) {
  const m = /^([A-G])(#?)(-?\d)$/.exec(String(t));
  return m ? 12 * (+m[3] + 1) + NAMES.indexOf(m[1] + m[2]) : -1;
}

// a document back into the sequencer's state; throws an Error whose message says what is wrong and where
export function fromDoc(d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) throw new Error('a sequence is an object {bpm, step, key, rows…}');
  const num = (k, lo, hi, def) => {
    const v = d[k] ?? def;
    if (typeof v !== 'number' || !(v >= lo && v <= hi)) throw new Error(`"${k}": a number from ${lo} to ${hi}`);
    return v;
  };
  const step = STEP_LENGTHS.find((l) => l[0] === (d.step ?? '1/8'));
  if (!step) throw new Error(`"step": one of ${STEP_LENGTHS.map((l) => l[0]).join(', ')}`);
  const steps = d.steps ?? 16;
  if (!STEP_COUNTS.includes(steps)) throw new Error(`"steps": one of ${STEP_COUNTS.join(', ')}`);
  const key = /^([A-G]#?) (.+)$/.exec(String(d.key ?? 'E minor'));
  if (!key || !SCALES[key[2]]) throw new Error(`"key": a root and a scale, e.g. "E minor" (scales: ${Object.keys(SCALES).join(', ')})`);
  const s = {
    name: typeof d.name === 'string' ? d.name : '', bpm: num('bpm', 20, 300, 80), stepLen: step[1], steps,
    gate: num('gate', 0.05, 1, 0.85), swing: num('swing', 0, 0.5, 0), root: NAMES.indexOf(key[1]), scale: key[2],
    octave: num('octave', 1, 5, 3), span: num('range', 1, 3, 2), cells: {},
  };
  if (!Number.isInteger(s.octave) || !Number.isInteger(s.span)) throw new Error('"octave" and "range": whole numbers');
  const grid = scaleRows(s.root, s.scale, s.octave, s.span);
  const rows = d.rows ?? {};
  if (typeof rows !== 'object' || Array.isArray(rows)) throw new Error('"rows": an object {"E4": "x . . ."}');
  for (const [name, line] of Object.entries(rows)) {
    const n = noteNumber(name);
    if (n < 0) throw new Error(`rows: "${name}" is not a note (E4, F#3…)`);
    if (!grid.includes(n)) throw new Error(`rows: ${name} is not in ${d.key ?? 'E minor'} from octave ${s.octave} over ${s.span} octave(s)`);
    const cells = String(line).replace(/[\s|]/g, '');
    if (cells.length !== steps) throw new Error(`rows: ${name} has ${cells.length} steps, "steps" is ${steps}`);
    for (let k = 0; k < steps; k++) {
      const c = cells[k];
      if (c === '.') continue;
      if (!CHAR_CELL[c]) throw new Error(`rows: ${name}, step ${k + 1}: "${c}" (x note, X accent, o soft, _ held, . rest)`);
      if (c === '_' && (k === 0 || cells[k - 1] === '.')) throw new Error(`rows: ${name}, step ${k + 1}: "_" holds a note, it must follow one`);
      s.cells[`${n}:${k}`] = CHAR_CELL[c];
    }
  }
  return s;
}
