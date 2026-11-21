// test_seq_export.mjs — the web sequencer's song export end to end: a grid (with held notes, accents and soft
// notes) and a patch become a self-contained song (inline patch, one track per row) that tools/validate.py
// accepts and synth_render --song renders without error and not silent.
// Usage: node web/test_seq_export.mjs [synth_render [synth_catalog [python]]]
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { buildSong, phrases, scaleRows, toDoc, fromDoc } from './ui/src/lib/seqExport.js';

const render = process.argv[2] ?? 'build-host/host/synth_render';
const catalog = process.argv[3] ?? 'build-host/host/synth_catalog';
const python = process.argv[4] ?? ['.venv/bin/python3', 'python3'].find((p) => { try { execFileSync(p, ['-c', '1']); return true; } catch { return false; } });
const fail = (m) => { console.log('FAILED: ' + m); process.exit(1); };

const rows = scaleRows(4, 'minor', 3, 2); // E minor, E3..E5
const cells = { '52:0': 'on', '52:1': 'tie', '52:2': 'tie', '55:4': 'acc', '59:6': 'soft', '64:8': 'on', '64:9': 'tie', '62:12': 'on' };
const ph = phrases(cells, rows, 16);
if (ph.length !== 5 || !ph[0].startsWith('E3 _ _ -') || !ph.some((p) => p.includes('G3!')) || !ph.some((p) => p.includes('B3?')))
  fail('phrases: ' + JSON.stringify(ph));

// the panel's JSON view: the grid as a document and back, exactly; a wrong document says what is wrong
const seq = { name: 'riff', bpm: 90, stepLen: 0.5, steps: 16, gate: 0.8, swing: 0.1, root: 4, scale: 'minor', octave: 3, span: 2, cells };
const doc = toDoc(seq);
if (doc.rows.E3 !== 'x__. .... .... ....' || doc.key !== 'E minor' || doc.step !== '1/8') fail('toDoc: ' + JSON.stringify(doc));
const back = fromDoc(JSON.parse(JSON.stringify(doc)));
const sorted = (o) => JSON.stringify(Object.entries(o).sort());
if (sorted(back.cells) !== sorted(cells) || back.bpm !== 90 || back.stepLen !== 0.5 || back.root !== 4) fail('fromDoc round trip');
for (const [bad, why] of [[{ ...doc, rows: { F4: 'x... .... .... ....' } }, 'not in E minor'], [{ ...doc, rows: { E3: '_... .... .... ....' } }, 'must follow'],
  [{ ...doc, rows: { E3: 'x...' } }, 'has 4 steps'], [{ ...doc, step: '1/5' }, '"step"'], [{ ...doc, key: 'H minor' }, '"key"']]) {
  try { fromDoc(bad); fail('fromDoc accepted ' + JSON.stringify(bad.rows ?? bad)); } catch (e) { if (!e.message.includes(why)) fail(`fromDoc: "${e.message}", expected "${why}"`); }
}

const { audition, description, ...patch } = JSON.parse(readFileSync('patches/sine_duel_a.json', 'utf8'));
const song = buildSong({ name: 'seq_export_test', bpm: 90, steps: 16, stepLen: 0.5, gate: 0.8, swing: 0.1, cells, notes: rows, loops: 2 }, patch);
if (song.tracks.length !== 5 || song.length_beats !== 16) fail('song shape');

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
