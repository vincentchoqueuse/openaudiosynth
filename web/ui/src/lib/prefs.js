// prefs.js — a remembered choice of the page (the view open, the live display, the code shown), kept in this
// browser; a browser that refuses storage simply forgets.
export function remembered(key, def) {
  try { const v = localStorage.getItem(key); return v === null ? def : JSON.parse(v); } catch { return def; }
}
export function remember(key, v) {
  try { localStorage.setItem(key, JSON.stringify(v)); } catch { /* storage unavailable */ }
}
