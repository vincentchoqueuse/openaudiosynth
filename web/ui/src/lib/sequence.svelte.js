// sequence.svelte.js — the step sequence played with the current sound, shared by the transport (play, tempo,
// position) and the Sequence tab (grid and JSON): its settings and cells, the saved sequences (this browser
// only), playing through the worklet's sample-accurate player (one phrase per row, edits take over in place)
// and the playhead. It keeps playing whatever the page shows. The JSON form is toDoc / fromDoc (seqExport.js).
import { untrack } from 'svelte';
import { session } from './session.svelte.js';
import { scaleRows, phrases, buildSong, toDoc, fromDoc } from './seqExport.js';

const KEY = 'synth.sequencer.v1';
export const LOOPS = [1, 2, 4, 8, 16];

class Sequence {
  bpm = $state(80);
  steps = $state(16);
  stepLen = $state(0.5); // beats per step
  gate = $state(0.85);
  swing = $state(0);
  root = $state(4);
  scale = $state('minor');
  octave = $state(3);
  span = $state(2);
  cells = $state({}); // "note:step" -> 'on' | 'acc' | 'soft' | 'tie'
  name = $state('');
  saved = $state({}); // name -> sequence, in this browser
  loops = $state(4); // times it plays in an exported song
  playStep = $state(-1);
  message = $state('');

  rows = $derived(scaleRows(this.root, this.scale, this.octave, this.span)); // high notes first
  tempo = $derived(Math.min(300, Math.max(20, +this.bpm || 120))); // the field may be empty or absurd
  playing = $derived(session.seqPlaying);
  // the playhead as bar . beat (4/4), from the step playing
  position = $derived.by(() => {
    if (this.playStep < 0) return '1.1';
    const b = this.playStep * this.stepLen;
    return `${Math.floor(b / 4) + 1}.${Math.floor(b % 4) + 1}`;
  });

  #silent = false; // the sequence sent last was empty: the player stops by itself, the transport stays on
  #pending = null;
  #ready = false;

  snap() {
    const { name, tempo: bpm, steps, stepLen, gate, swing, root, scale, octave, span, cells } = this;
    return { name, bpm, steps, stepLen, gate, swing, root, scale, octave, span, cells };
  }
  take(s) {
    this.bpm = s.bpm; this.steps = s.steps; this.stepLen = s.stepLen; this.gate = s.gate; this.swing = s.swing;
    this.root = s.root; this.scale = s.scale; this.octave = s.octave; this.span = s.span; this.cells = s.cells;
  }

  // ---- persistence: the current sequence autosaved, named ones on Save; stored data goes through fromDoc ----
  restore(st) {
    if (!st || typeof st !== 'object') return;
    try {
      this.take(fromDoc(toDoc({ ...this.snap(), ...st, cells: st.cells && typeof st.cells === 'object' ? st.cells : {} })));
      this.name = typeof st.name === 'string' ? st.name : '';
      if (LOOPS.includes(st.loops)) this.loops = st.loops;
    } catch { /* damaged: keep what is there */ }
  }
  // called once by the page: brings the stored sequences back, then saves every change, resends while playing,
  // follows the player's position
  attach() {
    let d = {};
    try { d = JSON.parse(localStorage.getItem(KEY)) ?? {}; } catch { /* storage unavailable */ }
    this.saved = d.saved ?? d.patterns ?? {};
    this.restore(d.current);
    this.#ready = true;
    $effect.root(() => {
      $effect(() => {
        const data = { current: $state.snapshot({ ...this.snap(), loops: this.loops }), saved: $state.snapshot(this.saved) };
        if (this.#ready) try { localStorage.setItem(KEY, JSON.stringify(data)); } catch { /* storage unavailable */ }
      });
      // any change of what is played is resent while playing
      $effect(() => { this.tempo; this.stepLen; this.gate; this.swing; this.steps; this.rows; this.cells; untrack(() => this.changed()); });
      // a new sound under a running sequence: the synth was re-initialized, its tempo comes back
      $effect(() => { session.patchGen; untrack(() => { if (this.playing) session.setP('tempo', this.tempo); }); });
    });
    session.synth.on('pos', (p) => {
      if (!this.playing) return;
      if (p.frames < 0) { // the player was stopped from elsewhere: follow it
        if (!this.#silent) this.stop(false);
        return;
      }
      const t = p.frames / p.sampleRate, sd = (60 / this.tempo) * this.stepLen;
      this.playStep = Math.floor(t / sd) % this.steps;
    });
  }

  pick = (n) => { if (this.saved[n]) { this.restore(this.saved[n]); this.name = n; this.message = ''; } };
  fresh = () => {
    this.cells = {};
    this.name = '';
    this.message = 'New sequence: draw it or write it in JSON, then Save';
  };
  save = () => {
    const n = (this.name || prompt('Name of the sequence') || '').trim();
    if (!n) return;
    this.name = n;
    this.saved = { ...this.saved, [n]: $state.snapshot({ ...this.snap(), loops: this.loops }) };
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
    const rows = phrases(this.cells, this.rows, this.steps).join('\n'); // what you see is what plays
    this.#silent = !rows;
    const r = await session.synth.sequence(rows, this.tempo, this.stepLen, this.gate, this.swing, true);
    if (r.length < 0) this.message = r.error;
  }
  changed() { // debounced resend while playing: the loop keeps its position
    if (!this.playing) return;
    clearTimeout(this.#pending);
    this.#pending = setTimeout(() => this.send(), 25);
  }
  play = () => { if (session.playing) session.stop(); session.seqPlaying = true; this.send(); };
  stop = (toPlayer = true) => {
    clearTimeout(this.#pending);
    session.seqPlaying = false;
    this.playStep = -1;
    if (toPlayer) session.synth.stop();
  };
  toggle = () => (this.playing ? this.stop() : this.play());

  // ---- editing ----
  get = (n, s) => this.cells[`${n}:${s}`];
  set = (n, s, v) => {
    const cells = { ...this.cells };
    if (v) cells[`${n}:${s}`] = v; else delete cells[`${n}:${s}`];
    this.cells = cells;
  };
  clear = () => { this.cells = {}; };
  random = () => {
    const next = {}, pool = this.rows.slice(Math.floor(this.rows.length / 3));
    for (let s = 0; s < this.steps; s++) {
      if (Math.random() > 0.3) continue;
      const n = pool[Math.floor(Math.random() * pool.length)];
      next[`${n}:${s}`] = Math.random() < 0.2 ? 'acc' : 'on';
      const hold = Math.floor(Math.random() * 3);
      for (let k = 1; k <= hold && s + k < this.steps; k++) next[`${n}:${s + k}`] = 'tie';
    }
    this.cells = next;
  };

  // ---- the JSON form ----
  docText = () => JSON.stringify(toDoc(this.snap()), null, 2) + '\n';
  // applies a typed document; returns '' or what is wrong
  applyText = (text) => {
    try {
      const s = fromDoc(JSON.parse(text));
      this.take(s);
      this.name = s.name === 'untitled' ? '' : s.name;
      this.message = 'Applied';
      return '';
    } catch (e) {
      return e.message;
    }
  };

  // ---- export: a song with the current sound inline, one track per row ----
  exportSong = () => {
    const patch = session.currentPatch();
    const song = buildSong({ name: (this.name || patch.name + '_sequence').replace(/\W+/g, '_'), bpm: this.tempo,
      steps: this.steps, stepLen: this.stepLen, gate: this.gate, swing: this.swing, cells: this.cells, notes: this.rows,
      loops: this.loops }, patch);
    if (!song.tracks.length) { this.message = 'Nothing to export: the sequence is empty'; return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(song, null, 2) + '\n'], { type: 'application/json' }));
    a.download = song.name + '.json';
    a.click();
    this.message = `Exported ${song.name}.json (songs/ format)`;
  };
}

export const sequence = new Sequence();
if (import.meta.env.DEV) window.sequence = sequence; // console debugging
