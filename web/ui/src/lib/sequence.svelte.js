// sequence.svelte.js — the sequence played with the current sound, a MIDI clip: notes {pitch, start, length,
// velocity} in beats over a length that is the loop (played once when the loop is off), shared by the transport
// (play, tempo, loop, position) and the Sequence tab (piano roll); the JSON tab edits the same clip (docText /
// fromDoc in seqExport.js). Saved clips stay in this browser. It plays through the worklet's sample-accurate
// player (w_notes), edits take over in place, and it keeps playing whatever the page shows.
import { untrack } from 'svelte';
import { session } from './session.svelte.js';
import { scaleRows, buildSong, docText, fromDoc, fromCells, toDoc, phraseToNotes, acidLine } from './seqExport.js';

const KEY = 'synth.sequence.v2', OLD_KEY = 'synth.sequencer.v1';
export const LOOPS = [1, 2, 4, 8, 16];
let nid = 1; // note ids for the interface (not part of the clip)
const withIds = (notes) => notes.map((n) => ({ ...n, id: nid++ }));

class Sequence {
  name = $state('');
  bpm = $state(96);
  length = $state(8); // beats: the clip, and its loop
  loop = $state(true);
  grid = $state(0.25); // snap of the piano roll, in beats
  root = $state(4);
  scale = $state('minor');
  octave = $state(3); // where the piano roll opens and random notes are drawn
  notes = $state([]); // {id, pitch, start, length, velocity}
  saved = $state({}); // name -> clip (document form), in this browser
  loops = $state(4); // times it plays in an exported song
  beat = $state(-1); // the playhead, in beats from the clip's start (-1: stopped)
  message = $state('');
  // the piano roll's clipboard ({notes: starts relative to the first, span: beats}), kept across sounds and clips,
  // and its insertion point (beats, -1: none), where a paste lands
  clipboard = $state(null);
  cursor = $state(-1);

  tempo = $derived(Math.min(300, Math.max(20, +this.bpm || 120))); // the field may be empty or absurd
  playing = $derived(session.seqPlaying);
  // the playhead as bar . beat . sixteenth (4/4)
  position = $derived.by(() => {
    const b = Math.max(0, this.beat);
    return `${Math.floor(b / 4) + 1}.${Math.floor(b % 4) + 1}.${Math.floor((b % 1) * 4) + 1}`;
  });

  // the pitches the clip plays now, for the keyboard to light
  sounding = $derived.by(() => {
    if (!this.playing || this.beat < 0) return new Set();
    const b = this.loop && this.length > 0 ? this.beat % this.length : this.beat;
    return new Set(this.notes.filter((n) => b >= n.start && b < n.start + n.length).map((n) => n.pitch));
  });

  #silent = false; // the clip sent last was empty: the player stops by itself, the transport stays on
  #pending = null;
  #ready = false;

  snap() {
    const { name, tempo: bpm, length, loop, grid, root, scale } = this;
    return { name, bpm, length, loop, grid, root, scale, notes: this.notes.map(({ id, ...n }) => n) };
  }
  take(s) {
    this.bpm = s.bpm; this.length = s.length; this.loop = s.loop; this.grid = s.grid; this.root = s.root;
    this.scale = s.scale; this.notes = withIds(s.notes);
  }

  // ---- persistence: the current clip autosaved, named ones on Save; stored data goes through fromDoc ----
  #load(doc) {
    try { this.take(fromDoc(doc)); this.name = typeof doc.name === 'string' && doc.name !== 'untitled' ? doc.name : ''; return true; }
    catch { return false; }
  }
  // an earlier grid sequence ({cells, stepLen, steps, …}) as a clip
  #fromGrid(st) {
    const stepLen = st.stepLen ?? 0.5, steps = st.steps ?? 16;
    return { name: st.name ?? '', bpm: st.bpm ?? 96, length: steps * stepLen, loop: true, grid: '1/16',
      key: `${['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][st.root ?? 4]} ${st.scale ?? 'minor'}`,
      notes: fromCells(st.cells, stepLen, steps) };
  }
  // called once by the page: brings the stored clips back (the earlier grid sequences converted), then saves every
  // change, resends while playing, follows the player's position
  attach() {
    let d = null;
    try { d = JSON.parse(localStorage.getItem(KEY)); } catch { /* storage unavailable */ }
    if (!d) { // first time: the grid sequences of before
      let old = {};
      try { old = JSON.parse(localStorage.getItem(OLD_KEY)) ?? {}; } catch { /* none */ }
      const saved = {};
      for (const [n, st] of Object.entries(old.patterns ?? old.saved ?? {})) saved[n] = this.#fromGrid({ ...st, name: n });
      d = { current: old.current ? this.#fromGrid(old.current) : null, saved };
    }
    this.saved = d.saved && typeof d.saved === 'object' ? d.saved : {};
    session.clipHook = { take: (p) => this.fromPatch(p), give: () => this.toPatch(), stop: () => this.playing && this.stop() };
    if (d.current) this.#load(d.current);
    this.#ready = true;
    $effect.root(() => {
      $effect(() => {
        const data = { current: JSON.parse(docText(this.snap())), saved: $state.snapshot(this.saved) };
        if (this.#ready) try { localStorage.setItem(KEY, JSON.stringify(data)); } catch { /* storage unavailable */ }
      });
      // any change of what is played is resent while playing
      $effect(() => { this.tempo; this.length; this.loop; JSON.stringify(this.notes); untrack(() => this.changed()); });
      // the clip's tempo is the synth's (LFO and delay sync), also after a new sound re-initialized it
      $effect(() => { session.patchGen; const t = this.tempo; untrack(() => session.setP('tempo', t)); });
    });
    session.synth.on('pos', (p) => {
      if (!this.playing) return;
      if (p.frames < 0) { // played once to its end, or stopped from elsewhere
        if (!this.#silent) this.stop(false);
        return;
      }
      this.beat = ((p.frames / p.sampleRate) * this.tempo) / 60;
    });
  }

  // a preset's clip into the sequencer: its "clip" section, else its audition phrase as notes; a preset with
  // neither leaves the clip as it is
  fromPatch(p) {
    let doc = null;
    if (p?.clip && typeof p.clip === 'object') doc = { ...p.clip, name: p.name };
    else if (p?.audition?.notes) {
      const a = p.audition, step = a.step ?? 0.25, { notes, length } = phraseToNotes(a.notes, step, a.gate ?? 0.8, a.velocity ?? 100);
      if (notes.length) doc = { name: p.name, bpm: a.bpm ?? 120, length, loop: true, grid: step >= 1 ? '1/4' : '1/16',
        key: `${['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][this.root]} ${this.scale}`, notes };
    }
    if (doc && this.#load(doc)) {
      this.message = '';
      // the piano roll opens on the octave of the clip's lowest note
      if (this.notes.length) this.octave = Math.min(5, Math.max(1, Math.floor(Math.min(...this.notes.map((n) => n.pitch)) / 12) - 1));
    }
  }
  // the clip as a patch's "clip" section (null when empty)
  toPatch() {
    if (!this.notes.length) return null;
    const { name, ...doc } = toDoc(this.snap());
    return doc;
  }

  pick = (n) => { if (this.saved[n] && this.#load(this.saved[n])) { this.name = n; this.message = ''; } };
  fresh = () => {
    this.notes = [];
    this.name = '';
    this.message = 'New clip: draw notes in the piano roll or write them in the JSON tab, then Save';
  };
  save = () => {
    const n = (this.name || prompt('Name of the clip') || '').trim();
    if (!n) return;
    this.name = n;
    this.saved = { ...this.saved, [n]: JSON.parse(docText(this.snap())) };
    this.message = `Saved "${n}" in this browser`;
  };
  remove = () => {
    if (!this.saved[this.name]) return;
    const { [this.name]: _, ...rest } = this.saved;
    this.saved = rest;
    this.message = `Deleted "${this.name}"`;
    this.name = '';
  };

  // ---- playing ----
  async send() {
    session.setP('tempo', this.tempo);
    this.#silent = !this.notes.length;
    const r = await session.synth.notes(this.notes, this.tempo, this.length, this.loop);
    if (r.length < 0) this.message = r.error;
  }
  changed() { // debounced resend while playing: the loop keeps its position
    if (!this.playing) return;
    clearTimeout(this.#pending);
    this.#pending = setTimeout(() => this.send(), 25);
  }
  play = () => { session.seqPlaying = true; this.beat = 0; this.send(); };
  stop = (toPlayer = true) => {
    clearTimeout(this.#pending);
    session.seqPlaying = false;
    this.beat = -1;
    if (toPlayer) session.synth.stop();
  };
  toggle = () => (this.playing ? this.stop() : this.play());

  // ---- editing (the piano roll) ----
  add = (pitch, start, length, velocity = 100) => {
    const n = { id: nid++, pitch, start, length, velocity };
    this.notes = [...this.notes, n];
    return n.id;
  };
  update = (id, patch) => { this.notes = this.notes.map((n) => (n.id === id ? { ...n, ...patch } : n)); };
  delete = (id) => { this.notes = this.notes.filter((n) => n.id !== id); };
  // several notes at once (one change for the history and the player): fn(note) -> its fields to change, or null
  edit = (ids, fn) => {
    this.notes = this.notes.map((n) => { if (!ids.has(n.id)) return n; const c = fn(n); return c ? { ...n, ...c } : n; });
  };
  addMany = (list) => { const added = withIds(list); this.notes = [...this.notes, ...added]; return added.map((n) => n.id); };
  deleteMany = (ids) => { this.notes = this.notes.filter((n) => !ids.has(n.id)); };
  // legato: each note of ids lasts until the next note starts (of any pitch, after it), the last one until the
  // clip's end
  legato = (ids) => {
    const starts = [...new Set(this.notes.map((n) => n.start))].sort((a, b) => a - b);
    this.edit(ids, (n) => {
      const next = starts.find((t) => t > n.start + 1e-9);
      return { length: (next ?? this.length) - n.start };
    });
  };
  clear = () => { this.notes = []; };
  random = () => {
    if (session.engine?.name === 'acid') { // an acid line: sixteenths, rests, octave jumps, accents and slides
      this.notes = withIds(acidLine(scaleRows(this.root, this.scale, Math.min(this.octave, 2), 1), this.length, Math.min(this.grid, 0.25)));
      return;
    }
    const pool = scaleRows(this.root, this.scale, this.octave, 2), out = [];
    for (let t = 0; t < this.length; t += this.grid) {
      if (Math.random() > 0.3) continue;
      const len = this.grid * (1 + Math.floor(Math.random() * 3));
      out.push({ pitch: pool[Math.floor(Math.random() * pool.length)], start: t, length: Math.min(len, this.length - t),
        velocity: Math.random() < 0.2 ? 120 : 90 });
    }
    this.notes = withIds(out);
  };

  // ---- the JSON form ----
  docText = () => docText(this.snap());
  // applies a typed document; returns '' or what is wrong
  applyText = (text) => {
    try {
      const doc = JSON.parse(text), s = fromDoc(doc);
      this.take(s);
      this.name = s.name;
      this.message = 'Applied';
      return '';
    } catch (e) {
      return e.message;
    }
  };

  // ---- export: a song with the current sound inline ----
  exportSong = () => {
    const patch = session.currentPatch(), s = this.snap();
    const song = buildSong({ ...s, name: (this.name || patch.name + '_clip').replace(/\W+/g, '_') }, patch, this.loops);
    if (!song.tracks.length) { this.message = 'Nothing to export: the clip is empty'; return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(song, null, 2) + '\n'], { type: 'application/json' }));
    a.download = song.name + '.json';
    a.click();
    this.message = `Exported ${song.name}.json (songs/ format)`;
  };
}

export const sequence = new Sequence();
if (import.meta.env.DEV) window.sequence = sequence; // console debugging
