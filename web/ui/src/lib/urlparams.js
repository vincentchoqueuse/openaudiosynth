// urlparams.js — what the page's address asks for: ?sound=NAME opens that preset, ?q=TEXT fills the library's
// search, ?view=sequence|synth|code opens that view (e.g. /?sound=drone_glacial&view=synth). Read once at load;
// soundUrl() gives the address of a sound, for sharing, and the address bar follows the sound playing.
const read = () => {
  try { return new URLSearchParams(location.search); } catch { return new URLSearchParams(); }
};
const p = read();
const VIEWS = { sequence: 'clip', synth: 'synth', code: 'code' };

export const urlParams = {
  sound: p.get('sound')?.replace(/\.json$/, '') || null,
  q: p.get('q') || '',
  view: VIEWS[p.get('view')] ?? null,
};

// the page's address with this sound (other parameters kept)
export function soundUrl(name) {
  const u = new URL(location.href);
  if (name) u.searchParams.set('sound', name);
  else u.searchParams.delete('sound');
  return u.toString();
}

// the address bar follows the sound playing, without a new history entry
export function showSound(name) {
  try { history.replaceState(null, '', soundUrl(name)); } catch { /* not in a page */ }
}
