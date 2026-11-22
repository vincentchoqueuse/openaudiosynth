// test_note_ops.mjs — the piano roll's note operations (web/ui/src/lib/noteOps.js) on small clips: pitch, time,
// velocity, split and glue, selections; the notes outside the selection never change.
// Usage: node web/test_note_ops.mjs
import * as op from './ui/src/lib/noteOps.js';

let failed = 0;
const eq = (got, want, what) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) { console.log(`FAILED: ${what}\n  got  ${g}\n  want ${w}`); failed++; }
};
const N = (id, pitch, start, length, velocity = 100) => ({ id, pitch, start, length, velocity });
const clip = { length: 8, grid: 0.25 };
const notes = [N(1, 60, 0, 0.5, 80), N(2, 64, 1, 0.5, 100), N(3, 67, 2, 1, 120), N(4, 48, 4, 1, 90)];
const sel = new Set([1, 2, 3]);
const f = (ns, k) => ns.map((n) => n[k]);

eq(f(op.transpose(notes, sel, 12), 'pitch'), [72, 76, 79, 48], 'transpose an octave, note 4 untouched');
eq(f(op.transpose(notes, sel, 100), 'pitch'), [127, 127, 127, 48], 'transpose clamps at 127');
eq(f(op.invertPitch(notes, sel), 'pitch'), [67, 63, 60, 48], 'invert around the range (60..67)');
eq(f(op.quantize([N(1, 60, 0.1, 1), N(2, 60, 0.9, 1)], new Set([1, 2]), clip), 'start'), [0, 1], 'quantize onto the grid');
eq(f(op.quantize([N(1, 60, 0.1, 1)], new Set([1]), clip, 0.5), 'start'), [0.05], 'quantize at 50 %');
eq(f(op.quantizeEnds([N(1, 60, 0, 0.6)], new Set([1]), clip), 'length'), [0.5], 'quantize the ends');
eq(f(op.legato(notes, sel, clip), 'length'), [1, 1, 2, 1], 'legato: until the next start (note 4 at 4)');
eq(f(op.staccato(notes, sel, 0.5), 'length'), [0.25, 0.25, 0.5, 1], 'staccato halves');
eq(f(op.reverse(notes, sel), 'start'), [2.5, 1.5, 0, 4], 'reverse over the span 0..3');
eq(f(op.reverse(notes, sel), 'pitch'), [60, 64, 67, 48], 'reverse keeps the pitches');
eq(op.speed(notes, sel, 0.5, clip).map((n) => [n.start, n.length]), [[0, 0.25], [0.5, 0.25], [1, 0.5], [4, 1]], 'double speed');
eq(op.speed(notes, new Set([3, 4]), 4, clip).map((n) => n.id), [1, 2, 3], 'a quarter speed drops what passes the clip end (note 4 at 2 + 2 x 4 = 10)');
eq(f(op.setVelocity(notes, sel, 200), 'velocity'), [127, 127, 127, 90], 'set velocity clamps');
eq(f(op.scaleVelocity(notes, sel, 0.5), 'velocity'), [40, 50, 60, 90], 'scale velocity');
eq(f(op.rampVelocity(notes, sel, 40, 120), 'velocity'), [40, 80, 120, 90], 'crescendo by start time');
{
  let r = 0.75; // a fixed "random": +0.5 of the range
  const h = op.humanize(notes, sel, () => r, 0.02, 10);
  eq(h.map((n) => [+n.start.toFixed(3), n.velocity]), [[0.01, 85], [1.01, 105], [2.01, 125], [4, 90]], 'humanize');
}
{
  let id = 100;
  const s = op.split([N(1, 60, 0.1, 0.6), N(2, 62, 0, 1)], new Set([1]), clip, () => id++);
  eq(s.map((n) => [n.id, +n.start.toFixed(3), +n.length.toFixed(3)]), [[1, 0.1, 0.15], [100, 0.25, 0.25], [101, 0.5, 0.2], [2, 0, 1]], 'split at the grid');
}
{
  const g = op.glue([N(1, 60, 0, 1, 90), N(2, 60, 1, 1), N(3, 60, 3, 1), N(4, 62, 0.5, 1)], new Set([1, 2, 3, 4]));
  eq(g.map((n) => [n.id, n.start, n.length, n.velocity]), [[1, 0, 2, 90], [3, 3, 1, 100], [4, 0.5, 1, 100]], 'glue touching notes of one pitch');
}
eq([...op.invertSelection(notes, sel)], [4], 'invert the selection');
eq([...op.samePitch([N(1, 60, 0, 1), N(2, 62, 1, 1), N(3, 60, 2, 1)], new Set([1]))], [1, 3], 'select the same pitch');

if (failed) process.exit(1);
console.log('note operations: all good');
