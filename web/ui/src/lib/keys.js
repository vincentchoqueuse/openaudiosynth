// keys.js — which keyboard events belong to a control rather than to the app's shortcuts: typing in a text field
// or a menu keeps every key; a slider or a checkbox only keeps the keys that operate it (arrows, Home / End, Space
// on a checkbox); a focused button keeps Space and Enter (its own click).
const TEXT_TYPES = new Set(['text', 'search', 'number', 'email', 'url', 'password', 'tel']);

export function keyIsForControl(e) {
  const t = e.target, tag = t?.tagName;
  if (tag === 'TEXTAREA' || tag === 'SELECT' || t?.isContentEditable) return true;
  if (tag === 'INPUT') {
    if (TEXT_TYPES.has(t.type)) return true;
    if (t.type === 'range') return /^(Arrow|Home|End|Page)/.test(e.code);
    return e.code === 'Space'; // checkbox, radio
  }
  if (tag === 'BUTTON') return e.code === 'Space' || e.code === 'Enter';
  return false;
}
