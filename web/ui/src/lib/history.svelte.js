// history.svelte.js — undo / redo of everything edited on the page: the sound (engine, every parameter, which
// preset it came from, its macros' names) and the clip (notes, length, loop, tempo, grid, key). A snapshot is
// taken whenever one of them changes; a burst of changes (a knob turned, a note dragged) is one step: it ends
// 400 ms after the last change, or when the gesture that holds it (hold / release) ends; step() before a discrete
// action (a key, a button) makes it a step of its own. Undo puts the synth back by setting the parameters that
// differ (all of them when the engine differs). Not undone: the sample kit, the listening volume, saving.
import { untrack } from 'svelte';
import { session as s } from './session.svelte.js';
import { sequence as q } from './sequence.svelte.js';

const KEEP = 200; // steps kept
const IDLE_MS = 400;

const capture = () => JSON.stringify({
  e: s.engineId, v: s.values, cur: s.current, m: s.macroNames,
  q: { ...q.snap(), name: q.name },
});

class History {
  canUndo = $state(false);
  canRedo = $state(false);
  #undo = [];
  #redo = [];
  #last = null; // the state as last seen
  #timer = null; // a burst of changes in progress
  #held = 0; // gestures in progress (a burst does not end while one is held)

  #refresh() { this.canUndo = this.#undo.length > 0; this.canRedo = this.#redo.length > 0; }

  // called once audio runs (the sound and the clip are there): from then on every change is recorded
  attach() {
    this.#last = capture();
    $effect.root(() => {
      $effect(() => { const cur = capture(); untrack(() => this.#seen(cur)); });
    });
  }

  #seen(cur) {
    if (cur === this.#last) return; // an undo or redo putting a state back sees itself here
    if (!this.#timer) { // the first change of a burst: the state before it is one step
      this.#undo.push(this.#last);
      if (this.#undo.length > KEEP) this.#undo.shift();
      this.#redo = [];
      this.#refresh();
    }
    this.#last = cur;
    this.#arm();
  }
  #arm() {
    clearTimeout(this.#timer);
    this.#timer = setTimeout(() => { if (this.#held) this.#arm(); else this.#timer = null; }, IDLE_MS);
  }
  #end() { clearTimeout(this.#timer); this.#timer = null; }

  // a discrete action (a key, a button) is a step of its own, however soon after the last change
  step = () => { if (!this.#held) this.#end(); };
  hold = () => { this.step(); this.#held++; };
  release = () => { this.#held = Math.max(0, this.#held - 1); };

  undo = () => this.#move(this.#undo, this.#redo);
  redo = () => this.#move(this.#redo, this.#undo);

  #move(from, to) {
    this.#end();
    const cur = capture();
    if (cur !== this.#last) { this.#undo.push(this.#last); this.#last = cur; } // a change not seen yet
    if (!from.length) { this.#refresh(); return; }
    const state = from.pop();
    to.push(cur);
    this.#last = state;
    this.#restore(JSON.parse(state));
    this.#refresh();
  }

  #restore(p) {
    const engineChanged = p.e !== s.engineId;
    if (engineChanged) {
      s.engineId = p.e;
      s.synth.setEngine(p.e);
      s.patchGen++;
    }
    for (const [key, v] of Object.entries(p.v)) {
      const id = +key, d = s.paramById(id);
      if (d && (engineChanged || s.values[id] !== v)) s.setValue(d, v);
    }
    s.current = p.cur;
    s.info = s.patches.find((x) => x.file === p.cur) ?? null;
    s.macroNames = p.m;
    q.take({ ...p.q, bpm: p.q.bpm });
    q.name = p.q.name;
  }
}

export const history = new History();
if (import.meta.env.DEV) window.history_ = history; // console debugging
