// noteOps.js — the piano roll's note operations, as pure functions: each takes the clip's notes ({id, pitch, start,
// length, velocity}, beats), the ids it acts on (a Set) and the clip ({length, grid}), and returns the new notes
// (the others untouched, the order kept). Shared by the Notes menu and its keys; tested by web/test_note_ops.mjs.
// Operations on a selection treat it as one phrase: reverse and speed work over its own span.

const clampPitch = (p) => Math.min(127, Math.max(0, Math.round(p)));
const clampVel = (v) => Math.min(127, Math.max(1, Math.round(v)));
const eps = 1e-9;
const map = (notes, ids, fn) => notes.map((n) => (ids.has(n.id) ? { ...n, ...fn(n) } : n));
const span = (notes, ids) => {
  const sel = notes.filter((n) => ids.has(n.id));
  return { sel, a: Math.min(...sel.map((n) => n.start)), b: Math.max(...sel.map((n) => n.start + n.length)) };
};

// pitch
export const transpose = (notes, ids, semis) => map(notes, ids, (n) => ({ pitch: clampPitch(n.pitch + semis) }));
// upside down around the middle of the selection's range (the highest note becomes the lowest)
export const invertPitch = (notes, ids) => {
  const { sel } = span(notes, ids);
  const lo = Math.min(...sel.map((n) => n.pitch)), hi = Math.max(...sel.map((n) => n.pitch));
  return map(notes, ids, (n) => ({ pitch: clampPitch(lo + hi - n.pitch) }));
};

// time
// starts toward the nearest grid line by `strength` (1 = onto it, 0.5 = halfway), within the clip
export const quantize = (notes, ids, { grid, length }, strength = 1) =>
  map(notes, ids, (n) => {
    const target = Math.round(n.start / grid) * grid;
    return { start: Math.min(Math.max(0, n.start + (target - n.start) * strength), Math.max(0, length - grid)) };
  });
// ends onto the grid too (lengths rounded to a whole number of steps, at least one)
export const quantizeEnds = (notes, ids, { grid }) =>
  map(notes, ids, (n) => ({ length: Math.max(grid, Math.round((n.start + n.length) / grid) * grid - n.start) }));
// each note lasts until the next start (any pitch, later), the last until the clip's end
export const legato = (notes, ids, { length }) => {
  const starts = [...new Set(notes.map((n) => n.start))].sort((x, y) => x - y);
  return map(notes, ids, (n) => ({ length: (starts.find((t) => t > n.start + eps) ?? length) - n.start }));
};
// each note shortened to a fraction of its length (staccato), at least a sixteenth of a beat
export const staccato = (notes, ids, f = 0.5) => map(notes, ids, (n) => ({ length: Math.max(1 / 16, n.length * f) }));
// the phrase backwards: a note ending at the span's end starts at its beginning
export const reverse = (notes, ids) => {
  const { a, b } = span(notes, ids);
  return map(notes, ids, (n) => ({ start: a + b - (n.start + n.length) }));
};
// faster (f = 0.5: double speed) or slower (f = 2: half speed) from the phrase's first note; what would start past
// the clip's end is dropped
export const speed = (notes, ids, f, { length }) => {
  const { a } = span(notes, ids);
  return map(notes, ids, (n) => ({ start: a + (n.start - a) * f, length: n.length * f })).filter((n) => !ids.has(n.id) || n.start < length - eps);
};
// small random shifts: timing up to +/- `time` beats (never before 0), velocity up to +/- `vel`
export const humanize = (notes, ids, rnd = Math.random, time = 0.02, vel = 10) =>
  map(notes, ids, (n) => ({ start: Math.max(0, n.start + (rnd() * 2 - 1) * time), velocity: clampVel(n.velocity + (rnd() * 2 - 1) * vel) }));

// velocity
export const setVelocity = (notes, ids, v) => map(notes, ids, () => ({ velocity: clampVel(v) }));
export const scaleVelocity = (notes, ids, f) => map(notes, ids, (n) => ({ velocity: clampVel(n.velocity * f) }));
// a crescendo (from < to) or a decrescendo across the phrase, by start time
export const rampVelocity = (notes, ids, from, to) => {
  const { a, sel } = span(notes, ids), last = Math.max(...sel.map((n) => n.start));
  return map(notes, ids, (n) => ({ velocity: clampVel(last > a ? from + ((to - from) * (n.start - a)) / (last - a) : to) }));
};

// structure
// each note cut at the grid lines it crosses (newId() gives the pieces' ids; the first piece keeps the note's id)
export const split = (notes, ids, { grid }, newId) => {
  const out = [];
  for (const n of notes) {
    if (!ids.has(n.id)) { out.push(n); continue; }
    let t = n.start, first = true;
    const end = n.start + n.length;
    while (t < end - eps) {
      const next = Math.min(end, (Math.floor(t / grid + eps) + 1) * grid);
      out.push({ ...n, id: first ? n.id : newId(), start: t, length: next - t });
      first = false;
      t = next;
    }
  }
  return out;
};
// notes of one pitch that touch or overlap become one (the earliest keeps its id and velocity)
export const glue = (notes, ids) => {
  const sel = notes.filter((n) => ids.has(n.id)).sort((x, y) => x.pitch - y.pitch || x.start - y.start);
  const gone = new Set(), grown = new Map();
  for (let i = 0; i < sel.length; i++) {
    const head = sel[i];
    let end = head.start + head.length;
    while (i + 1 < sel.length && sel[i + 1].pitch === head.pitch && sel[i + 1].start <= end + eps) {
      end = Math.max(end, sel[i + 1].start + sel[i + 1].length);
      gone.add(sel[++i].id);
    }
    grown.set(head.id, end - head.start);
  }
  return notes.filter((n) => !gone.has(n.id)).map((n) => (grown.has(n.id) ? { ...n, length: grown.get(n.id) } : n));
};

// selections (sets of ids)
export const invertSelection = (notes, ids) => new Set(notes.filter((n) => !ids.has(n.id)).map((n) => n.id));
export const samePitch = (notes, ids) => {
  const pitches = new Set(notes.filter((n) => ids.has(n.id)).map((n) => n.pitch));
  return new Set(notes.filter((n) => pitches.has(n.pitch)).map((n) => n.id));
};
