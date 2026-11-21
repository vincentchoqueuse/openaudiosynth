// session.svelte.js — the state of the page in one place, read by every component: the synth (the worklet), the
// catalog of engines and parameters, the current sound (engine, parameter values, the patch it came from, its
// macros' names, its kit), the library, the audition phrase and its transport, the save dialog and the notices.
// Components import `session` and call its actions instead of passing a dozen props down. Values are in
// physical units; the synth gets them normalized (synth.js).
import { Synth, fromNorm, toNorm } from './synth.js';
import { parseKit, filesToKit } from './kit.js';
import { engineColor, DEFAULT_MACROS, DEFAULT_MACRO_SENDS } from './engines.js';
import { human } from './naming.js';

class Session {
  synth = new Synth();
  started = $state(false);
  starting = $state(false);
  catalog = $state(null);
  engineId = $state(1);
  values = $state({}); // id -> physical value
  baseline = $state({}); // values when the sound was loaded: what "edited" compares with
  patches = $state([]); // the library (patches/index.json)
  current = $state(''); // patch file of the current sound, '' for a new one
  info = $state(null); // its index entry (name, engine, description)
  description = $state(''); // what the sound is (edited in the Description tab, saved with it)
  macroNames = $state(Array(8).fill('')); // the patch's "macros" names (their values live in parameters)
  kit = $state.raw(null); // the sample kit the synth plays (parsed, for display), null none
  kitName = $state(''); // its name when it comes from kits/ (a patch can name it), '' for dropped files
  kitLabel = $state('');
  seqPlaying = $state(false); // the sequence plays
  // the clip a preset carries goes to the sequencer, and comes back into the patch when it is saved: set by
  // sequence.svelte.js (take(patch object), give() -> the patch's "clip" section or null)
  clipHook = null;
  #audition = null; // the loaded patch's audition phrase, kept as it was when it is saved (the boards play it)
  patchGen = $state(0); // patch loads and engine changes (they reset the synth's tempo)
  picks = $state(0); // sounds picked in the library (the editor opens on Macros)
  // the Synth tab's editor: which tab is open, and the stage the diagram asked to show (scrolled to, lit)
  editorTab = $state('macros');
  focus = $state(null); // {id, n}: n changes at every request, so the same stage can be asked again
  goTo = (tab, id = null) => { this.editorTab = tab; this.focus = id ? { id, n: (this.focus?.n ?? 0) + 1 } : null; };
  shortcutsOpen = $state(false); // the shortcuts sheet
  settingsOpen = $state(false); // the part's settings (MIDI channel, keys, MPE, tuning, tempo)
  midiNames = $state([]);
  held = $state(new Set()); // notes held on MIDI input (the keyboard lights them)
  saving = $state(null); // the save dialog: {name, description, exists, error}
  toast = $state(null); // {text, kind}: a short notice at the bottom
  meter = $state({ level: 0, hold: -Infinity }); // the output level (0..1 of -90..0 dBFS) and its held peak (dBFS)
  canSave = import.meta.env.DEV; // writing into patches/ goes through the dev server
  // the listening volume (dB, -60 = silent), kept in this browser; applied after the synth, whatever the sound
  volume = $state((() => { try { const v = +localStorage.getItem('synth.volume'); return Number.isFinite(v) && v <= 0 && v >= -60 && localStorage.getItem('synth.volume') !== null ? v : -6; } catch { return -6; } })());
  setVolume = (db) => {
    this.volume = Math.min(0, Math.max(-60, db));
    this.synth.setVolume(this.volume);
    try { localStorage.setItem('synth.volume', String(this.volume)); } catch { /* storage unavailable */ }
  };

  engine = $derived(this.catalog?.engines[this.engineId]);
  shared = $derived(this.catalog?.shared ?? []);
  color = $derived(engineColor(this.engine?.name));
  // parameters by name (engine first, then shared)
  byName = $derived.by(() => {
    const m = {};
    for (const d of this.shared) m[d.name] = d;
    for (const d of this.engine?.params ?? []) m[d.name] = d;
    return m;
  });
  // parameters moved since the sound was loaded (or since the defaults, for a new sound)
  changedCount = $derived(this.engine ? [...this.engine.params, ...this.shared]
    .filter((d) => Math.abs((this.values[d.id] ?? d.def) - (this.baseline[d.id] ?? d.def)) > 1e-6).length : 0);
  title = $derived(this.current ? this.current.replace('.json', '') : 'init');

  #toastTimer = null;
  #loadSeq = 0; // the latest patch load wins

  // ---- parameters ----
  getP = (n) => { const d = this.byName[n]; return d ? this.values[d.id] ?? d.def : 0; };
  setP = (n, v) => { const d = this.byName[n]; if (d) this.setValue(d, v); };
  setValue = (d, v) => { this.values[d.id] = v; this.synth.setParam(d.id, toNorm(d, v)); };
  // a synth parameter id: the engine's (0..255) or a shared one (master, effects...)
  paramById = (id) => (id >= 0 && id < 256 ? this.engine?.params[id] : this.shared.find((d) => d.id === id));

  // ---- notices: errors stay longer ----
  notify = (text, kind = 'info') => {
    if (!text) return;
    clearTimeout(this.#toastTimer);
    this.toast = { text, kind };
    this.#toastTimer = setTimeout(() => (this.toast = null), kind === 'error' ? 6000 : 3500);
  };
  say = (text) => this.notify(text, /could not|error|unknown/i.test(text) ? 'error' : 'info');

  resetValues(withShared) {
    const v = withShared ? {} : { ...this.values };
    for (const d of this.engine.params) v[d.id] = d.def;
    if (withShared) for (const d of this.shared) v[d.id] = d.def;
    this.values = v;
    this.baseline = { ...v };
  }

  start = async () => {
    this.starting = true;
    try {
      this.catalog = await this.synth.start();
      this.synth.setVolume(this.volume);
      this.started = true;
      this.resetValues(true);
      this.defaultMacros();
      this.baseline = { ...this.values };
      this.synth.on('midiin', (d) => {
        const t = d[0] & 0xf0;
        if (t === 0xb0) { // a macro's CC: the synth has turned it, the knob follows
          for (let k = 1; k <= 8; k++)
            if (Math.round(this.getP(`macro${k}_cc`)) === d[1]) { const m = this.byName[`macro${k}`]; if (m) this.values[m.id] = d[2] / 127; }
          return;
        }
        const held = new Set(this.held);
        if (t === 0x90 && d[2] > 0) held.add(d[1]);
        else if (t === 0x80 || (t === 0x90 && d[2] === 0)) held.delete(d[1]);
        else return;
        this.held = held;
      });
      await this.listPatches();
      this.synth.enableMidi((n) => (this.midiNames = n)).catch(() => (this.midiNames = []));
    } catch (e) {
      this.say('Could not start audio: ' + e.message);
    }
    this.starting = false;
  };

  listPatches = async () => {
    try {
      const r = await fetch('patches/index.json');
      if (!r.ok) throw new Error(await r.text());
      this.patches = await r.json();
    } catch (e) {
      this.patches = [];
      this.say('Could not list the patches: ' + e.message);
    }
  };

  setEngine = (id) => {
    this.patchGen++;
    this.#audition = null; // a new sound: no phrase of its own
    this.engineId = id;
    this.synth.setEngine(id);
    this.current = '';
    this.info = null;
    this.resetValues(false);
    this.description = '';
    // routes point at engine parameters by index: they mean nothing for another engine
    for (let r = 1; r <= 8; r++) if (this.getP(`route${r}_destination`)) this.setP(`route${r}_destination`, 0);
    this.defaultMacros();
    this.baseline = { ...this.values };
  };

  // The instrument's default macros for a sound that has none (as on the board's knobs): macros 1..6 on its main
  // parameters, around their present value (a third of the travel either way; only a little towards a closed
  // filter or a long attack), 7 and 8 on the delay and reverb sends over their whole travel. Each macro is set
  // where the parameter is, so assigning them changes nothing in the sound.
  defaultMacros = () => {
    const names = [...(DEFAULT_MACROS[this.engine?.name] ?? []).slice(0, 6), ...Array(6).fill('')].slice(0, 6).concat(DEFAULT_MACRO_SENDS);
    for (let s = 1; s <= 32; s++) if (this.getP(`map${s}_macro`)) this.setP(`map${s}_macro`, 0); // no leftovers
    let slot = 1;
    const labels = Array(8).fill('');
    names.forEach((n, k) => {
      const d = this.byName[n];
      if (!d || !this.byName[`map${slot}_macro`]) return;
      const v = toNorm(d, this.values[d.id] ?? d.def), send = k >= 6;
      const closes = d.unit === 'Hz' || /cutoff/.test(n), opens = /attack/.test(n);
      const from = send ? 0 : Math.max(0, v - (closes ? 0.12 : 0.35)), to = send ? 1 : Math.min(1, v + (opens ? 0.12 : 0.35));
      this.setP(`map${slot}_macro`, k + 1);
      this.setP(`map${slot}_destination`, d.id + 1);
      this.setP(`map${slot}_from`, from);
      this.setP(`map${slot}_to`, to);
      this.setP(`macro${k + 1}`, to > from ? (v - from) / (to - from) : 0);
      labels[k] = human(n);
      slot++;
    });
    this.macroNames = labels;
  };

  // back to the engine's default sound (the engine too, not only the sliders)
  resetEngine = () => {
    for (const d of this.engine.params) if (Math.abs((this.values[d.id] ?? d.def) - d.def) > 1e-9) this.setValue(d, d.def);
  };

  // a sound of the library; returns true when it is the current one
  loadPatch = async (p) => {
    const seq = ++this.#loadSeq;
    let text;
    try {
      const res = await fetch('patches/' + p.file);
      if (!res.ok) throw new Error(`${p.file}: ${res.status}`);
      text = await res.text();
    } catch (e) {
      this.say('Could not load the patch: ' + e.message);
      return false;
    }
    const r = await this.applyPatch(text, seq);
    if (!r?.ok) return false;
    this.current = p.file;
    this.info = p;
    this.baseline = { ...this.values };
    this.picks++;
    this.editorTab = 'macros';
    return true;
  };

  // A patch's text into the synth (the C patch reader checks it): its values, its macro names, its kit.
  // Returns the worklet's answer ({ok: false, error} if refused), or null if another load overtook it.
  applyPatch = async (text, seq = ++this.#loadSeq) => {
    let r;
    try { r = await this.synth.loadPatch(text); } catch (e) { r = { ok: false, error: 'Could not load the patch: ' + e.message }; }
    if (seq !== this.#loadSeq) return null; // another patch was clicked meanwhile
    if (!r.ok) { this.say(r.error); return r; }
    this.patchGen++; // a running sequence goes on with the new sound
    this.engineId = r.engine;
    this.resetValues(true); // a patch starts from a fresh synth_init, like synth_render
    for (const [id, v] of r.values) this.values[id] = v;
    try {
      const macros = JSON.parse(text).macros ?? [];
      this.macroNames = Array.from({ length: 8 }, (_, k) => String(macros[k]?.name ?? ''));
    } catch { this.macroNames = Array(8).fill(''); }
    this.say(r.warning);
    try {
      const o = JSON.parse(text);
      this.#audition = o.audition ?? null;
      this.description = typeof o.description === 'string' ? o.description : '';
      if (!(o.macros ?? []).some((m) => m?.targets?.length)) this.defaultMacros();
      this.clipHook?.take(o);
    } catch { /* the C reader has accepted it: it is JSON */ }
    if (r.kit && r.kit !== this.kitName) await this.loadNamedKit(r.kit); // the synth keeps the last kit across patches
    return r;
  };

  // ---- the JSON editor: the sound as its patch file, and a hand-edited file back into the synth ----
  applyFromEditor = async (text) => {
    const r = await this.applyPatch(text);
    if (!r) return { ok: false, text: 'Overtaken by another load' };
    if (!r.ok) return { ok: false, text: r.error };
    return { ok: true, text: r.warning ? 'Applied, with warnings: ' + r.warning : 'Applied' };
  };

  // ---- sample kits ----
  async useKit(buf, name, label) {
    const r = await this.synth.loadKit(buf);
    if (r.count < 0) { this.notify('Not a valid kit', 'error'); return false; }
    this.kit = parseKit(buf);
    this.kitName = name;
    this.kitLabel = label;
    return true;
  }
  async loadNamedKit(name) {
    try {
      const res = await fetch(`kits/${name}.oidk`);
      if (!res.ok) throw new Error(res.status === 404 ? 'not built (just build)' : String(res.status));
      await this.useKit(await res.arrayBuffer(), name, name);
    } catch (e) {
      this.notify(`Kit ${name}: ${e.message}`, 'error');
    }
  }
  dropFiles = async (files) => {
    this.notify(`Reading ${files.length} file${files.length > 1 ? 's' : ''}…`);
    const { buffer, failed } = await filesToKit(files);
    const n = files.length - failed.length;
    if (buffer && (await this.useKit(buffer, '', `${n} dropped file${n > 1 ? 's' : ''}`)))
      this.notify(failed.length ? `Kit loaded; not audio: ${failed.join(', ')}` : `Kit loaded: ${this.kit.entries.length} samples`);
    else if (!buffer) this.notify('No audio file could be read', 'error');
  };

  // the current sound as a patch object (docs/patch_format.md): only what differs from the defaults
  currentPatch = () => {
    const engine = this.engine, getP = this.getP;
    const label = (d, v) => {
      if (/^route\d+_destination$/.test(d.name)) return engine.params[Math.round(v) - 1]?.name ?? 0; // by name
      return d.labels ? d.labels[Math.round(v - d.min)] : +v.toPrecision(5);
    };
    // envelope points beyond the point count are leftovers of an earlier, longer shape: not part of the sound
    const unusedPoint = (name) => {
      const m = name.match(/^(mseg\d|vmseg)_[tlc](\d+)$/);
      return m && +m[2] > Math.round(getP(m[1] + '_points'));
    };
    const params = {};
    for (const d of [...engine.params, ...this.shared])
      if (Math.abs((this.values[d.id] ?? d.def) - d.def) > 1e-6 && !unusedPoint(d.name) && !/^(map\d+_|macro\d+(_cc)?$)/.test(d.name))
        params[d.name] = label(d, this.values[d.id]);
    // the macros as the readable "macros" section (docs/patch_format.md): name, value, targets in their units
    const macros = [];
    for (let k = 0; k < 8; k++) {
      const targets = [];
      for (let s = 1; s <= 32; s++) {
        const dst = this.paramById(Math.round(getP(`map${s}_destination`)) - 1);
        if (Math.round(getP(`map${s}_macro`)) !== k + 1 || !dst) continue;
        const v = (n) => { const x = fromNorm(dst, n); return dst.labels ? dst.labels[Math.round(x - dst.min)] : +x.toPrecision(5); };
        targets.push({ param: dst.name, from: v(getP(`map${s}_from`)), to: v(getP(`map${s}_to`)) });
      }
      const value = +getP(`macro${k + 1}`).toPrecision(4), cc = Math.round(getP(`macro${k + 1}_cc`));
      macros.push({ ...(this.macroNames[k] ? { name: this.macroNames[k] } : {}), ...(value ? { value } : {}),
                    ...(cc !== 102 + k ? { cc } : {}), ...(targets.length ? { targets } : {}) });
    }
    while (macros.length && !Object.keys(macros[macros.length - 1]).length) macros.pop();
    const name = this.current ? this.current.replace('.json', '') : engine.name + '_new';
    const p = { version: 1, engine: engine.name, name, description: this.description, params };
    if (macros.length) p.macros = macros;
    if (this.kitName && engine.name === 'sampler') p.kit = this.kitName; // a dropped kit has no name to save
    if (this.#audition) p.audition = this.#audition;
    const clip = this.clipHook?.give();
    if (clip) p.clip = clip;
    return p;
  };

  exportPatch = () => {
    const p = this.currentPatch(), name = p.name;
    const text = JSON.stringify(p, null, 2) + '\n';
    navigator.clipboard?.writeText(text).catch(() => {});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = name + '.json';
    a.click();
    this.notify(`Exported ${name}.json, also copied to the clipboard`);
  };

  // ---- save into patches/ (development): the board's presets are generated from there ----
  openSave = () => {
    const p = this.currentPatch();
    this.saving = { name: p.name, description: p.description, exists: false, error: '' };
  };
  save = async (overwrite = false) => {
    const p = this.currentPatch();
    p.description = this.saving.description.trim();
    const res = await fetch('/api/patches', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: this.saving.name.trim(), patch: p, overwrite }),
    }).catch((e) => ({ ok: false, status: 0, json: async () => ({ error: e.message }) }));
    const r = await res.json().catch(() => ({ error: 'no answer from the dev server' }));
    if (res.status === 409) { this.saving = { ...this.saving, exists: true, error: '' }; return; }
    if (!res.ok) { this.saving = { ...this.saving, error: r.error ?? 'could not save' }; return; }
    this.description = p.description;
    this.saving = null;
    this.baseline = { ...this.values };
    await this.listPatches();
    this.current = r.file;
    this.info = this.patches.find((x) => x.file === r.file) ?? this.info;
    this.notify(`Saved patches/${r.file}. On the board: just build, then just flash.`);
  };
  revert = async () => {
    if (this.info) await this.loadPatch(this.info);
    else this.resetEngine();
  };
}

export const session = new Session();
if (import.meta.env.DEV) window.session = session; // console debugging
