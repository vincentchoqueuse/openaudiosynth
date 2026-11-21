// seqExport.js — the sequence as a MIDI clip, pure functions shared by the page and web/test_seq_export.mjs: notes
// {pitch (MIDI 0..127), start and length in beats, velocity 1..127} over a length that is also the loop; the
// JSON document of the JSON tab (toDoc / fromDoc, with messages that say what is wrong); the scale shown by the
// piano roll; the old grid sequences turned into notes; and a self-contained song (songs/ format) for export.

export const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const SCALES = {
  minor: [0, 2, 3, 5, 7, 8, 10], phrygian: [0, 1, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10],
  'minor pentatonic': [0, 3, 5, 7, 10], 'harmonic minor': [0, 2, 3, 5, 7, 8, 11], major: [0, 2, 4, 5, 7, 9, 11],
  lydian: [0, 2, 4, 6, 7, 9, 11], chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
};
// snap values of the piano roll, in beats
export const GRIDS = [['1/32', 0.125], ['1/16', 0.25], ['1/8', 0.5], ['1/4', 1], ['1/2', 2], ['1 bar', 4]];

export const noteName = (n) => NAMES[n % 12] + (Math.floor(n / 12) - 1);
export const inScale = (n, root, scale) => (SCALES[scale] ?? SCALES.chromatic).includes(((n - root) % 12 + 12) % 12);

// MIDI notes of the scale from octave over span octaves, high notes first (where random notes are drawn)
export function scaleRows(root, scale, octave, span) {
  const lo = 12 * (octave + 1) + root, out = [];
  for (let o = 0; o < span; o++) for (const i of SCALES[scale] ?? SCALES.minor) out.push(lo + 12 * o + i);
  out.push(lo + 12 * span);
  return out.reverse();
}

const r6 = (x) => Math.round(x * 1e6) / 1e6;

// the document: settings, then one note per line (readable and diffable)
export function toDoc(s) {
  return {
    name: s.name || 'untitled', bpm: s.bpm, length: s.length, loop: s.loop,
    grid: (GRIDS.find((g) => g[1] === s.grid) ?? GRIDS[1])[0], key: `${NAMES[s.root]} ${s.scale}`,
    notes: [...s.notes].sort((a, b) => a.start - b.start || b.pitch - a.pitch)
      .map((n) => ({ pitch: n.pitch, start: r6(n.start), length: r6(n.length), velocity: n.velocity })),
  };
}
export function docText(s) {
  const d = toDoc(s), { notes, ...head } = d;
  const top = JSON.stringify(head, null, 2).slice(0, -2); // without the closing brace
  const lines = notes.map((n) => `    { "pitch": ${n.pitch}, "start": ${n.start}, "length": ${n.length}, "velocity": ${n.velocity} }`);
  return `${top},\n  "notes": [${lines.length ? '\n' + lines.join(',\n') + '\n  ' : ''}]\n}\n`;
}

// a document back into a sequence; throws an Error whose message says what is wrong and where
export function fromDoc(d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) throw new Error('a sequence is an object {bpm, length, notes…}');
  const num = (k, lo, hi, def) => {
    const v = d[k] ?? def;
    if (typeof v !== 'number' || !(v >= lo && v <= hi)) throw new Error(`"${k}": a number from ${lo} to ${hi}`);
    return v;
  };
  const grid = GRIDS.find((g) => g[0] === (d.grid ?? '1/16'));
  if (!grid) throw new Error(`"grid": one of ${GRIDS.map((g) => g[0]).join(', ')}`);
  const key = /^([A-G]#?) (.+)$/.exec(String(d.key ?? 'E minor'));
  if (!key || !SCALES[key[2]]) throw new Error(`"key": a root and a scale, e.g. "E minor" (scales: ${Object.keys(SCALES).join(', ')})`);
  if (d.loop !== undefined && typeof d.loop !== 'boolean') throw new Error('"loop": true or false');
  const s = {
    name: typeof d.name === 'string' && d.name !== 'untitled' ? d.name : '', bpm: num('bpm', 20, 300, 96),
    length: num('length', 0.25, 256, 8), loop: d.loop ?? true, grid: grid[1], root: NAMES.indexOf(key[1]), scale: key[2],
    notes: [],
  };
  const notes = d.notes ?? [];
  if (!Array.isArray(notes)) throw new Error('"notes": a list [{pitch, start, length, velocity}]');
  notes.forEach((n, i) => {
    const at = `notes[${i}]`;
    if (!n || typeof n !== 'object') throw new Error(`${at}: {pitch, start, length, velocity}`);
    if (!Number.isInteger(n.pitch) || n.pitch < 0 || n.pitch > 127) throw new Error(`${at}.pitch: a MIDI note 0..127 (60 = C4)`);
    if (typeof n.start !== 'number' || !(n.start >= 0) || n.start >= s.length) throw new Error(`${at}.start: beats from 0 to below the length (${s.length})`);
    if (typeof n.length !== 'number' || !(n.length > 0) || n.length > 256) throw new Error(`${at}.length: beats above 0`);
    const v = n.velocity ?? 100;
    if (!Number.isInteger(v) || v < 1 || v > 127) throw new Error(`${at}.velocity: 1..127`);
    s.notes.push({ pitch: n.pitch, start: n.start, length: n.length, velocity: v });
  });
  return s;
}

// a sequence of the earlier step grid ({cells: {"note:step": on|acc|soft|tie}, stepLen, steps}) as notes
export function fromCells(cells, stepLen, steps) {
  const VEL = { on: 100, acc: 127, soft: 50 }, notes = [];
  for (const [k, c] of Object.entries(cells ?? {})) {
    const [pitch, step] = k.split(':').map(Number);
    if (!VEL[c]) continue;
    let len = 1;
    while (cells[`${pitch}:${step + len}`] === 'tie' && step + len < steps) len++;
    notes.push({ pitch, start: step * stepLen, length: len * stepLen, velocity: VEL[c] });
  }
  return notes;
}

/* Song for synth_render --song (songs/ format): the clip played `loops` times with the sound inline. Phrases have
 * steps, so the notes are laid on the clip's grid (a start off the grid moves to the nearest step); one track per
 * pitch, a note held with '_'; velocity 110 and above is an accent ('!'), 64 and below soft ('?'). */
export function buildSong(s, patch, loops = 4) {
  const step = s.grid, steps = Math.max(1, Math.round(s.length / step));
  const byPitch = new Map();
  for (const n of s.notes) {
    const a = Math.min(steps - 1, Math.round(n.start / step)), len = Math.max(1, Math.round(n.length / step));
    if (!byPitch.has(n.pitch)) byPitch.set(n.pitch, Array(steps).fill('-'));
    const t = byPitch.get(n.pitch);
    t[a] = noteName(n.pitch) + (n.velocity >= 110 ? '!' : n.velocity <= 64 ? '?' : '');
    for (let k = 1; k < len && a + k < steps; k++) t[a + k] = '_';
  }
  const rows = [...byPitch].sort((a, b) => a[0] - b[0]).map(([, t]) => t.join(' '));
  return {
    version: 1,
    name: s.name,
    description: `Clip exported from the web sequencer: ${s.notes.length} note(s) over ${s.length} beats at ${s.bpm} BPM, ` +
      `played ${loops} times, on the sound "${patch.name}" (inline patch).`,
    bpm: s.bpm,
    length_beats: s.length * loops,
    tail_s: 8,
    tracks: rows.map((notes) => ({ patch, notes, step, repeat: loops, gate: 1, velocity: 100, reverb_send: 0 })),
    master: { fade_in_s: 0, fade_out_s: 4 },
  };
}
