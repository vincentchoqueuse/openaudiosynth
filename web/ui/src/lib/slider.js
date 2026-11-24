// slider.js — what every value slider of the page shares: precise(), a Svelte action on an <input type=range>
// that makes it exact to set (Shift while dragging moves ten times less; the arrows step 1 % of the travel, with
// Shift 0.1 %, Page Up / Down 10 %; a parameter of whole numbers steps one at a time), and parseValue(), what a
// typed value means for a parameter ("440", "1.2k", "1,2 kHz", "30 ms", "2 s" for one in seconds or hertz).
// The action reads and writes the slider's position (its own min..max) through opt: { value(): the position now,
// set(position), step: the arrows' step in position (default 1 % of the travel) }.
export function precise(node, opt) {
  let o = opt, shift = false, last = null, anchor = null;
  const lo = () => +node.min, hi = () => +node.max;
  const clamp = (n) => Math.min(hi(), Math.max(lo(), n));
  const down = (e) => { shift = e.shiftKey; last = +node.value; anchor = null; };
  const move = (e) => { shift = e.shiftKey; };
  function input() {
    const raw = +node.value;
    if (shift) { // from where Shift went down, a tenth of the way the pointer travels
      anchor ??= { raw: last ?? raw, at: o.value() };
      o.set(clamp(anchor.at + (raw - anchor.raw) * 0.1));
    } else {
      anchor = null;
      o.set(raw);
    }
    last = raw;
  }
  function key(e) {
    const dir = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.code];
    if (!dir || e.metaKey || e.ctrlKey || e.altKey) return;
    e.preventDefault();
    const whole = o.step !== undefined; // a step given is the smallest there is (a whole number)
    const step = o.step ?? (hi() - lo()) * 0.01;
    o.set(clamp(o.value() + dir * step * (e.shiftKey && !whole ? 0.1 : 1)));
  }
  node.addEventListener('pointerdown', down);
  node.addEventListener('pointermove', move);
  node.addEventListener('input', input);
  node.addEventListener('keydown', key);
  return {
    update(next) { o = next; },
    destroy() {
      node.removeEventListener('pointerdown', down);
      node.removeEventListener('pointermove', move);
      node.removeEventListener('input', input);
      node.removeEventListener('keydown', key);
    },
  };
}

// A typed value in the parameter's unit, or null when it is not a number. A unit typed wins over the one shown:
// ms and s for a time, Hz and kHz (or a bare k) for a frequency.
export function parseValue(d, text) {
  const m = /^\s*([+\-−]?\d*[.,]?\d+(?:e[+-]?\d+)?)\s*([a-zµ%]*)/i.exec(text ?? '');
  if (!m) return null;
  let v = parseFloat(m[1].replace(',', '.').replace('−', '-'));
  const u = m[2].toLowerCase();
  if (!isFinite(v)) return null;
  if (d.unit === 's' && u === 'ms') v /= 1000;
  if (d.unit === 'Hz' && (u === 'k' || u === 'khz')) v *= 1000;
  v = Math.min(d.max, Math.max(d.min, v));
  return d.curve === 'int' ? Math.round(v) : v;
}
