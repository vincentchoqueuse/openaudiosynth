// theme.svelte.js — the page's light / dark theme: "auto" follows the system, "light" and "dark" force it (the
// data-theme attribute app.css reads); the choice stays in this browser and applies from the first screen.
const KEY = 'synth.theme', MODES = ['auto', 'light', 'dark'];

function apply(mode) {
  if (typeof document === 'undefined') return;
  if (mode === 'auto') delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = mode;
}

class Theme {
  mode = $state((() => { try { const m = localStorage.getItem(KEY); return MODES.includes(m) ? m : 'auto'; } catch { return 'auto'; } })());
  constructor() { apply(this.mode); }
  next = () => {
    this.mode = MODES[(MODES.indexOf(this.mode) + 1) % MODES.length];
    apply(this.mode);
    try { localStorage.setItem(KEY, this.mode); } catch { /* storage unavailable */ }
  };
}

export const theme = new Theme();
