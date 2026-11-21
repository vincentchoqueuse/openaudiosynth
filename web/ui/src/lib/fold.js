// fold.js — a remembered choice of the page (a section folded, a tab open), kept in this browser.
export function remembered(key, def) {
  try { const v = localStorage.getItem(key); return v === null ? def : JSON.parse(v); } catch { return def; }
}
export function remember(key, v) {
  try { localStorage.setItem(key, JSON.stringify(v)); } catch { /* storage unavailable */ }
}
