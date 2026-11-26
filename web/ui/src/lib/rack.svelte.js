// rack.svelte.js — the effects of the rack as records of the Synth table: an effect is in the table when it is on,
// when a macro moves it, or since it was added (so a mix dragged to 0 does not take it out); Remove switches it off
// and takes it out. The effects play in the order they were added (fx_slot1..6, session.setRack): one added goes
// after those in use. What was added or removed lasts while the sound is the same.
import { session as s } from './session.svelte.js';
import { fxStages, stageOn, toggleStage } from './stages.js';

class Rack {
  loaded = $state({});
  emptied = $state({});
  #gen = -1;
  // called from an effect of the table: another sound forgets, then what is on or moved by a macro comes in
  sync() {
    if (this.#gen !== s.patchGen) { this.#gen = s.patchGen; this.loaded = {}; this.emptied = {}; }
    for (const r of fxStages(s)) {
      if (!s.rack.includes(r.key) || this.loaded[r.key]) continue;
      if (stageOn(s, r.st)) { this.loaded[r.key] = true; delete this.emptied[r.key]; }
      else if (!this.emptied[r.key] && r.ps.some(({ d }) => s.macrosOf[d.id])) this.loaded[r.key] = true;
    }
  }
  has = (id) => !!this.loaded[id];
  used = () => s.rack.filter((id) => this.loaded[id]);
  free = () => s.rack.filter((id) => !this.loaded[id]);
  add(id) {
    const used = this.used().filter((x) => x !== id);
    s.setRack([...used, id, ...s.rack.filter((x) => x !== id && !used.includes(x))]);
    this.loaded[id] = true;
    delete this.emptied[id];
    const st = fxStages(s).find((r) => r.key === id)?.st;
    if (st && !stageOn(s, st)) toggleStage(s, st);
  }
  remove(id) {
    const st = fxStages(s).find((r) => r.key === id)?.st;
    if (st && stageOn(s, st)) toggleStage(s, st);
    delete this.loaded[id];
    this.emptied[id] = true;
    if (s.panel?.id === id) s.closePanel();
  }
}
export const rack = new Rack();
