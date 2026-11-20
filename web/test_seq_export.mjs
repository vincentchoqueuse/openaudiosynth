// test_seq_export.mjs — the web sequencer's song export end to end: a grid (with held notes, accents and soft
// notes) and a patch become a self-contained song (inline patch, one track per row) that tools/validate.py
// accepts and synth_render --song renders without error and not silent.
// Usage: node web/test_seq_export.mjs [synth_render [synth_catalog [python]]]
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { buildSong, phrases, scaleRows } from './ui/src/lib/seqExport.js';

const render = process.argv[2] ?? 'build-host/host/synth_render';
const catalog = process.argv[3] ?? 'build-host/host/synth_catalog';
const python = process.argv[4] ?? ['.venv/bin/python3', 'python3'].find((p) => { try { execFileSync(p, ['-c', '1']); return true; } catch { return false; } });
const fail = (m) => { console.log('FAILED: ' + m); process.exit(1); };

const rows = scaleRows(4, 'minor', 3, 2); // E minor, E3..E5
const cells = { '52:0': 'on', '52:1': 'tie', '52:2': 'tie', '55:4': 'acc', '59:6': 'soft', '64:8': 'on', '64:9': 'tie', '62:12': 'on' };
const ph = phrases(cells, rows, 16);
if (ph.length !== 5 || !ph[0].startsWith('E3 _ _ -') || !ph.some((p) => p.includes('G3!')) || !ph.some((p) => p.includes('B3?')))
  fail('phrases: ' + JSON.stringify(ph));

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
