// test_seq_export.mjs — the web sequencer's clip format end to end: the JSON tab's document (one note per line) and
// back exactly, its error messages, the earlier step grids turned into notes, and the song export (inline patch,
// one track per pitch) that tools/validate.py accepts, a random acid line's shape, and synth_render --song renders without error, not silent.
// Usage: node web/test_seq_export.mjs [synth_render [synth_catalog [python]]]
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { buildSong, toDoc, docText, fromDoc, fromCells, phraseToNotes, acidLine } from './ui/src/lib/seqExport.js';

const render = process.argv[2] ?? 'build-host/host/synth_render';
const catalog = process.argv[3] ?? 'build-host/host/synth_catalog';
const python = process.argv[4] ?? ['.venv/bin/python3', 'python3'].find((p) => { try { execFileSync(p, ['-c', '1']); return true; } catch { return false; } });
const fail = (m) => { console.log('FAILED: ' + m); process.exit(1); };

{ // a random acid line: on the sixteenths, within the pool give or take an octave, accents, slides into a next note
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
  const pool = [36, 39, 41, 43, 46, 48], line = acidLine(pool, 4, 0.25, rnd);
  const starts = new Set(line.map((n) => n.start));
  const okPitch = line.every((n) => pool.some((p) => [p - 12, p, p + 12].includes(n.pitch)));
  const slides = line.filter((n) => n.length > 0.25);
  if (line.length < 8 || !line.every((n) => n.start % 0.25 === 0) || !okPitch || !line.some((n) => n.velocity === 120)
      || !slides.length || !slides.every((n) => starts.has(n.start + 0.25)))
    fail('acidLine: ' + JSON.stringify(line));
}

// a clip: a long note, an accent, a soft one, a note off the grid, one past the loop's turn
const clip = { name: 'riff', bpm: 90, length: 8, loop: true, grid: 0.5, root: 4, scale: 'minor', notes: [
  { pitch: 52, start: 0, length: 1.5, velocity: 100 }, { pitch: 55, start: 2, length: 0.5, velocity: 127 },
  { pitch: 59, start: 3, length: 0.5, velocity: 50 }, { pitch: 64, start: 4.3, length: 1, velocity: 90 },
  { pitch: 62, start: 7, length: 2, velocity: 100 }] };

// the JSON tab's document: one note per line, and back exactly; a wrong document says what is wrong
const text = docText(clip);
if (!text.includes('{ "pitch": 52, "start": 0, "length": 1.5, "velocity": 100 }') || toDoc(clip).key !== 'E minor') fail('docText: ' + text);
const back = fromDoc(JSON.parse(text));
const norm = (ns) => JSON.stringify([...ns].sort((a, b) => a.start - b.start));
if (norm(back.notes) !== norm(clip.notes) || back.bpm !== 90 || back.length !== 8 || back.grid !== 0.5 || back.root !== 4) fail('fromDoc round trip');
const doc = JSON.parse(text);
for (const [bad, why] of [[{ ...doc, notes: [{ pitch: 130, start: 0, length: 1 }] }, 'pitch'], [{ ...doc, notes: [{ pitch: 60, start: -1, length: 1 }] }, 'start'],
  [{ ...doc, notes: [{ pitch: 60, start: 0, length: 0 }] }, 'length'], [{ ...doc, notes: [{ pitch: 60, start: 0, length: 1, velocity: 200 }] }, 'velocity'],
  [{ ...doc, grid: '1/5' }, '"grid"'], [{ ...doc, key: 'H minor' }, '"key"'], [{ ...doc, loop: 'yes' }, '"loop"']]) {
  try { fromDoc(bad); fail('fromDoc accepted ' + JSON.stringify(bad)); } catch (e) { if (!e.message.includes(why)) fail(`fromDoc: "${e.message}", expected "${why}"`); }
}

// a note past the clip's end (the clip was shortened) stays: it does not play, the clip still loads
if (fromDoc({ ...doc, length: 4 }).notes.length !== clip.notes.length) fail('fromDoc dropped the notes past the end');

// the earlier step grid becomes notes (held steps lengthen them; accent 127, soft 50)
const old = fromCells({ '52:0': 'on', '52:1': 'tie', '52:2': 'tie', '55:4': 'acc', '59:6': 'soft' }, 0.5, 16);
if (norm(old) !== norm([{ pitch: 52, start: 0, length: 1.5, velocity: 100 }, { pitch: 55, start: 2, length: 0.5, velocity: 127 },
  { pitch: 59, start: 3, length: 0.5, velocity: 50 }])) fail('fromCells: ' + JSON.stringify(old));

// a preset's audition phrase as notes: chords, holds, rests, accent and soft
const ph = phraseToNotes('A2+E3 _ _ - C3! _ D3?', 4, 0.8);
if (ph.length !== 28 || norm(ph.notes) !== norm([{ pitch: 45, start: 0, length: 11.2, velocity: 100 }, { pitch: 52, start: 0, length: 11.2, velocity: 100 },
  { pitch: 48, start: 16, length: 7.2, velocity: 127 }, { pitch: 50, start: 24, length: 3.2, velocity: 50 }])) fail('phraseToNotes: ' + JSON.stringify(ph));

// export: one track per pitch on the clip's grid, validated by tools/validate.py and rendered by synth_render
const { audition, description, ...patch } = JSON.parse(readFileSync('patches/micro_low_pole.json', 'utf8'));
const song = buildSong({ ...clip, name: 'seq_export_test' }, patch, 2);
if (song.tracks.length !== 5 || song.length_beats !== 16 || !song.tracks.some((t) => t.notes.startsWith('E3 _ _ -'))) fail('song shape: ' + JSON.stringify(song.tracks.map((t) => t.notes)));

const dir = mkdtempSync(join(tmpdir(), 'seq-export-'));
const file = join(dir, 'seq_export_test.json');
writeFileSync(file, JSON.stringify(song, null, 2));
try {
  execFileSync(python, ['tools/validate.py', '--catalog-cmd', catalog, file], { stdio: 'pipe' });
} catch (e) { fail('validate.py: ' + (e.stdout?.toString() ?? '') + (e.stderr?.toString() ?? '')); }
const out = execFileSync(render, ['--song', file, '--out', join(dir, 'x.wav')], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
const m = out.match(/peak ([-0-9.]+) dBFS, rms ([-0-9.]+) dBFS/);
if (!m || +m[2] < -60) fail('render: ' + out);
console.log(`ok: ${song.tracks.length} tracks, validated, rendered (${out.trim().split('(')[1]}`);
