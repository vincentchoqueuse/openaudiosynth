// session.svelte.js — the state of the page in one place, read by every component: the synth (the worklet), the
// catalog of engines and parameters, the current sound (engine, parameter values, the patch it came from, its
// macros' names, its kit), the library and the sounds' demo clips, the save dialog and the notices.
// Components import `session` and call its actions instead of passing a dozen props down. Values are in
// physical units; the synth gets them normalized (synth.js).
import { PATCH_VERSION, putRole, dumps } from './patchjson.js';
import { Synth, fromNorm, toNorm } from './synth.js';
import { parseKit, filesToKit } from './kit.js';
import { engineColor, engineRank, DEFAULT_MACROS, DEFAULT_MACRO_SENDS, BARE } from './engines.js';
import { human, unclaimed } from './naming.js';
import { urlParams } from './urlparams.js';
import { remembered, remember } from './prefs.js';
import { engineStages, fxStages, stageOn } from './stages.js';
import { keys as routeKeys, inUse as routeInUse, clear as clearRoute } from './routes.js';

const FIRST_SOUND = 'wt_drifting_glass'; // what a first visit opens on

class Session {
  synth = new Synth();
  started = $state(false);
  starting = $state(false);
  catalog = $state(null);
  engineId = $state(0); // va, the engine a fresh synth starts on
  values = $state({}); // id -> physical value
  baseline = $state({}); // values when the sound was loaded: what "edited" compares with
  patches = $state([]); // the library (patches/index.json)
  current = $state(''); // patch file of the current sound, '' for a new one
  info = $state(null); // its index entry (name, engine, description)
  description = $state(''); // what the sound is (edited in the Description tab, saved with it)
  macroNames = $state(Array(8).fill('')); // the patch's "macros" names (their values live in parameters)
  kit = $state.raw(null); // the sample kit the synth plays (parsed, for display), null none
  kitName = $state(''); // its name when it comes from kits/ (a patch can name it), '' for dropped files
  // a reference recording (the sound to match): {name, duration}, whether it is heard instead of the synth, and its
  // level in dB relative to the file (set to the synth's at load, or by Match level)
  ref = $state(null);
  refListen = $state(false);
  refGain = $state(0);
  #refWith = ''; // what the reference plays with now: '' (stopped), 'clip' (from the clip's start), 'alone'
  graphPart = $state(''); // the part of the sound the Graph shows ('' all four: graphmods.js)
  seqPlaying = $state(false); // the sequence plays
  // the sequencer, set by sequence.svelte.js: take({name, clip}) puts a clip in it (a sound's demo), give() -> its clip
  // (or null when empty). A sound and the sequencer are apart: loading a sound leaves the clip as it is.
  clipHook = null;
  patchGen = $state(0); // patch loads and engine changes (they reset the synth's tempo)
  picks = $state(0); // sounds picked in the library (the name flashes)
  constructor() {
    if (typeof matchMedia === 'function') matchMedia('(max-width: 760px)').addEventListener('change', (e) => (this.phone = e.matches));
  }
  // the side panel of the Synth view: what a row's Configure opened ({kind, id}: kind 'stage' | 'mod' | 'route' |
  // 'macro'), or null; it stays open from page to page and closes with Escape or its cross
  panel = $state(null);
  panelTitle = $state(''); // the name of what the panel shows, for the path above the view
  synthQuery = $state(''); // the Synth table's search
  synthState = $state('all'); // and on / off: all, on, off (everything, unless asked: nothing hides by surprise)
  tableRows = $state.raw([]); // the Synth table's rows as shown ({kind, key}): the panel's arrows step through them
  panelTab = $state('settings'); // the side panel's tab: general or settings
  synthTab = $state(urlParams.code ? 'code' : 'audio'); // the Synth view's tab: audio (what makes the sound: its stages and effects), mod (what moves it: modulators and macros), graph (its signal path), code (its patch file) or general (the sound as fields)
  openPanel = (kind, id) => { this.panel = { kind, id }; this.panelTab = 'settings'; this.panelBack = []; }; // an item opens on its settings
  closePanel = () => { this.panel = null; this.panelBack = []; };
  // Edit from inside a panel (a modulation's source, a macro): the panel shows that item, and Back returns to the
  // one it came from (panelBack: the way back, {kind, id, title, tab})
  panelBack = $state([]);
  editPanel = (kind, id) => {
    if (this.panel && (this.panel.kind !== kind || this.panel.id !== id))
      this.panelBack = [...this.panelBack, { ...this.panel, title: this.panelTitle, tab: this.panelTab }];
    this.panel = { kind, id };
    this.panelTab = 'settings';
  };
  panelReturn = () => {
    const b = this.panelBack.at(-1);
    if (!b) return;
    this.panelBack = this.panelBack.slice(0, -1);
    this.panel = { kind: b.kind, id: b.id };
    this.panelTab = b.tab ?? 'settings';
  };
  shortcutsOpen = $state(false); // the shortcuts sheet
  cpu = $state(0); // share of the audio time spent rendering, 0..1 (the worklet measures it)
  msegPos = $state([-1, -1, -1]); // where the part's envelopes and the newest note's are, 0..1 (-1: idle)
  // a phone: the page leaves out what is for a desk (the Code view, MIDI CC settings)
  phone = $state(typeof matchMedia === 'function' && matchMedia('(max-width: 760px)').matches);
  shareOpen = $state(false); // the QR code that opens this page on another device
  clipSettingsOpen = $state(false); // the sequencer's clip settings (length, key, octave, song loops)
  settingsOpen = $state(false); // the part's settings (MIDI channel, keys, MPE, tuning, tempo)
  midiNames = $state([]);
  held = $state(new Set()); // notes held on MIDI input (the keyboard lights them)
  macroDrag = $state(-1); // a macro (0..7) being dragged onto a setting: the settings it can move are marked
  played = $state(new Set()); // notes held on the on-screen or computer keyboard (the piano roll lights them, with held)
  saving = $state(null); // the save dialog: {name, description, exists, error}
  toast = $state(null); // {text, kind}: a short notice at the bottom
  meter = $state({ level: 0, hold: -Infinity }); // the output level (0..1 of -90..0 dBFS) and its held peak (dBFS)
  canSave = import.meta.env.DEV; // writing into patches/ goes through the dev server
  // the listening volume (dB, -60 = silent), kept in this browser; applied after the synth, whatever the sound
  volume = $state((() => { try { const v = +localStorage.getItem('synth.volume'); return Number.isFinite(v) && v <= 0 && v >= -60 && localStorage.getItem('synth.volume') !== null ? v : -6; } catch { return -6; } })());
  // ---- the reference: loaded from a file, played with the clip (from its start, looped) or alone while heard ----
  loadRef = async (file) => {
    try {
      const { duration, rmsDb } = await this.synth.loadRef(await file.arrayBuffer());
      this.ref = { name: file.name, duration };
      this.refGain = -18 - rmsDb; // at the level a voice aims for; Match level brings it to the synth's
      this.#refWith = '';
      this.refListen = true;
      this.refSync();
      this.notify(`Reference: ${file.name} (${duration.toFixed(1)} s). \` or the Ref button switches between it and the synth.`);
    } catch (e) { this.notify(`Could not read ${file.name}: ${e.message}`); }
  };
  clearRef = () => { this.synth.refClear(); this.ref = null; this.refListen = false; this.#refWith = ''; };
  toggleRef = () => { if (!this.ref) return; this.refListen = !this.refListen; this.refSync(); };
  setRefGain = (db) => { this.refGain = Math.max(-40, Math.min(40, db)); this.refSync(); };
  matchRef = async () => { // the synth's level now, measured while it is heard, becomes the reference's
    if (!this.ref) return;
    const was = this.refListen;
    if (was) { this.refListen = false; this.refSync(); }
    const db = await this.synth.measureDb(1500);
    if (db < -80) { this.notify('Play the synth (the clip or the keys) to match the reference to its level.'); }
    else { this.refGain = db - 20 * Math.log10(this.synth.refRms); this.notify(`Reference at the synth's level (${db.toFixed(1)} dB RMS).`); }
    if (was) { this.refListen = true; this.refSync(); }
  };
  refSync = () => {
    if (!this.ref) return;
    const want = this.seqPlaying ? 'clip' : this.refListen ? 'alone' : '';
    if (want !== this.#refWith) {
      if (want) this.synth.refPlay(0); else this.synth.refStop();
      this.#refWith = want;
    }
    this.synth.setListen(this.refListen, this.refGain);
  };
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
  title = $derived(this.current ? this.current.replace('.json', '') : 'init'); // the sound's id (address, files)
  // a sound's name as read: without its instrument's prefix ("drone_abyss" under drone: "Abyss")
  nameOf = (p) => { const n = p.name.startsWith(p.engine + '_') ? p.name.slice(p.engine.length + 1) : p.name; return (n.charAt(0).toUpperCase() + n.slice(1)).replace(/_/g, ' '); };
  label = $derived(this.info ? this.nameOf(this.info) : this.current ? this.title : `New ${this.engine?.name ?? ''} sound`);
  // the library in the order it is listed (instruments, then their sounds): what Previous / Next walk through
  ordered = $derived(this.patches.map((p, i) => [p, i]).sort(([a, i], [b, j]) => engineRank(a.engine) - engineRank(b.engine) || a.engine.localeCompare(b.engine) || i - j).map(([p]) => p));
  // A/B: while the sound as loaded is heard (B), the edited values (A) wait here; null: the edit is heard
  edited = $state(null);
  canCompare = $derived(!!this.edited || this.changedCount > 0);

  #toastTimer = null;
  #loadSeq = 0; // the latest patch load wins

  // ---- parameters ----
  getP = (n) => { const d = this.byName[n]; return d ? this.values[d.id] ?? d.def : 0; };
  setP = (n, v) => { const d = this.byName[n]; if (d) this.setValue(d, v); };
  setValue = (d, v) => {
    this.values[d.id] = v;
    this.synth.setParam(d.id, toNorm(d, v));
    if (d.id < 1024) this.#followMacros(d, v);
  };
  // A parameter a macro moves is held by it: setting it directly would do nothing. Its range in each macro that
  // targets it shifts instead, so the parameter lands on the value set and the macro keeps moving it from there.
  #followMacros(d, v) {
    const n = toNorm(d, v);
    for (const { slot, macro, dst, from, to } of this.macroMaps) {
      if (macro < 0 || dst?.id !== d.id) continue;
      const m = this.getP(`macro${macro + 1}`), delta = n - (from + (to - from) * m);
      if (Math.abs(delta) < 1e-6) continue;
      // the range slides as a whole while it can stay within 0..1; what is left moves the end that weighs most where
      // the macro stands (its start when the macro is low, its end when high)
      const shift = Math.max(-Math.min(from, to), Math.min(1 - Math.max(from, to), delta));
      let f = from + shift, t = to + shift;
      if (Math.abs(delta - shift) > 1e-6) {
        if (m < 0.5) f = Math.min(1, Math.max(0, (n - t * m) / (1 - m)));
        else t = Math.min(1, Math.max(0, (n - f * (1 - m)) / m));
      }
      this.setP(`map${slot}_from`, f);
      this.setP(`map${slot}_to`, t);
    }
  }
  // a synth parameter id: the engine's (0..255) or a shared one (master, effects...)
  paramById = (id) => (id >= 0 && id < 256 ? this.engine?.params[id] : this.shared.find((d) => d.id === id));

  // ---- the effect rack: the bus effects (phaser, flanger, chorus, delay, reverb) in the order of its five places
  // (fx_slot1..5); read as the synth does: an effect named twice keeps its first place, one not named goes just before the effect that usually follows it ----
  rackUnits = $derived(this.byName.fx_slot1?.labels ?? []);
  rack = $derived.by(() => {
    const units = this.rackUnits, out = [];
    for (let k = 1; k <= units.length; k++) {
      const e = units[Math.round(this.getP(`fx_slot${k}`))];
      if (e && !out.includes(e)) out.push(e);
    }
    for (let i = units.length - 1; i >= 0; i--) { // one not named: just before the effect that usually follows it
      if (out.includes(units[i])) continue;
      const at = out.indexOf(units[i + 1]);
      out.splice(at < 0 ? out.length : at, 0, units[i]);
    }
    return out;
  });
  setRack = (order) => order.forEach((e, k) => this.setP(`fx_slot${k + 1}`, this.rackUnits.indexOf(e)));
  // the stages of the effect chain as the signal goes through them now: those of the rack in its order, each fed
  // by the one before it (the others, before and after the rack, stay where they are)
  fxStages = $derived.by(() => {
    const all = this.catalog?.fx ?? [], order = this.rack;
    const at = all.map((st, i) => (order.includes(st.id) ? i : -1)).filter((i) => i >= 0);
    if (at.length !== order.length || !at.length) return all;
    const out = all.map((st) => ({ ...st })), first = all[at[0]].inputs, last = all[at[at.length - 1]].id;
    order.forEach((id, k) => { out[at[k]] = { ...all.find((st) => st.id === id), inputs: k ? order[k - 1] : first }; });
    for (const st of out) if (!order.includes(st.id)) st.inputs = st.inputs.split(' ').map((x) => (x === last ? order[order.length - 1] : x)).join(' ');
    return out;
  });

  // ---- macros: 8 macros, each moving targets kept in the part's 32 map slots (dsp/mod/partmod.h) ----
  // the shared parameters a macro can move: those of the effect chain's stages (master, output filter, effects)
  #fxIds = $derived(new Set(this.shared.filter((d) => d.id >= 256 && d.id < 512 && !unclaimed(this.catalog?.fx ?? [], [d]).length).map((d) => d.id)));
  // a target: an engine parameter, or one of the effect chain's
  #mapTarget = (id) => (id >= 0 && id < 256 ? this.paramById(id) : this.#fxIds.has(id) ? this.paramById(id) : undefined);
  // every slot: {slot (1..32), macro (0..7, -1: none), dst (descriptor or undefined), from, to (normalized)}
  macroMaps = $derived(Array.from({ length: 32 }, (_, i) => {
    const slot = i + 1, mac = Math.round(this.getP(`map${slot}_macro`));
    return { slot, macro: mac >= 1 && mac <= 8 ? mac - 1 : -1, dst: this.#mapTarget(Math.round(this.getP(`map${slot}_destination`)) - 1),
             from: this.getP(`map${slot}_from`), to: this.getP(`map${slot}_to`) };
  }));
  // the parameters a macro can move: the engine's live ones, then the master and the effects
  macroTargets = $derived({
    engine: (this.engine?.params ?? []).filter((d) => d.realtime),
    fx: this.shared.filter((d) => this.#fxIds.has(d.id) && d.realtime),
  });
  macroName = (k) => this.macroNames[k] || `Macro ${k + 1}`;
  // the ids a macro can move (a drop of a macro on a setting: assignMacro)
  macroTargetIds = $derived(new Set([...this.macroTargets.engine, ...this.macroTargets.fx].map((d) => d.id)));
  // macro k (0..7) dropped on a setting: a free slot moves it over a third of its travel, placed so that the macro
  // where it is now leaves the setting where it is (the sound does not change); false when it cannot
  assignMacro = (k, id) => {
    const d = this.paramById(id);
    if (!d || !this.macroTargetIds.has(id)) { this.say('A macro cannot move this setting.'); return false; }
    if (this.macroMaps.some((r) => r.macro === k && r.dst?.id === id)) { this.say(`${this.macroName(k)} already moves ${human(d.name)}.`); return false; }
    const r = this.macroMaps.find((x) => !x.dst) ?? this.macroMaps.find((x) => x.macro < 0);
    if (!r) { this.say('Every macro slot is in use (32): remove one in a macro first.'); return false; }
    const now = toNorm(d, this.values[id] ?? d.def), m = this.getP(`macro${k + 1}`), span = 1 / 3;
    // the macro at m gives now; near an end the travel is shifted inside 0..1 (the setting moves a little)
    const from = Math.max(0, Math.min(1 - span, now - m * span)), to = from + span;
    this.setP(`map${r.slot}_destination`, id + 1);
    this.setP(`map${r.slot}_from`, from);
    this.setP(`map${r.slot}_to`, to);
    this.setP(`map${r.slot}_macro`, k + 1);
    this.say(`${this.macroName(k)} now moves ${human(d.name)}.`);
    return true;
  };
  // parameter id -> the macros (0..7) that move it, in order: the badge on a parameter's row
  macrosOf = $derived.by(() => {
    const out = {};
    for (const m of this.macroMaps) if (m.macro >= 0 && m.dst && !(out[m.dst.id] ??= []).includes(m.macro)) out[m.dst.id].push(m.macro);
    for (const id in out) out[id].sort((x, y) => x - y);
    return out;
  });

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
      this.synth.on('mseg', (d) => (this.msegPos = d.pos));
      this.synth.on('cpu', (d) => (this.cpu = d.load));
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
      const asked = urlParams.sound && this.patches.find((p) => p.name === urlParams.sound || p.file === urlParams.sound + '.json');
      if (asked) await this.loadPatch(asked); // ?sound= in the address
      else {
        if (urlParams.sound) this.say(`No sound called ${urlParams.sound}`);
        // the page opens on a sound that plays: the last one played here, or the showcase
        const last = remembered('synth.sound', FIRST_SOUND);
        const first = this.patches.find((p) => p.name === last) ?? this.patches.find((p) => p.name === FIRST_SOUND);
        if (first) await this.loadPatch(first);
      }
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

  // The instrument's default macros for a sound that has none (as on the board's knobs): macros 1..6 on its main
  // parameters, around their present value (a third of the travel either way; a cutoff or any frequency from about
  // four octaves below to three above, so the macro sweeps from dark to bright; only a little towards a long
  // attack), 7 and 8 on the delay and reverb sends over their whole travel. Each macro is set where the parameter
  // is, so assigning them changes nothing in the sound. All in normalized units: along each parameter's own curve.
  defaultMacros = () => {
    const names = [...(DEFAULT_MACROS[this.engine?.name] ?? []).slice(0, 6), ...Array(6).fill('')].slice(0, 6).concat(DEFAULT_MACRO_SENDS);
    for (let s = 1; s <= 32; s++) if (this.getP(`map${s}_macro`)) this.setP(`map${s}_macro`, 0); // no leftovers
    let slot = 1;
    const labels = Array(8).fill('');
    names.forEach((n, k) => {
      if (Array.isArray(n)) { // a scene: several settings, each from where it is, upwards; the macro starts at 0
        const [name, ...targets] = n;
        for (const [pn, travel] of targets) {
          const t = this.byName[pn];
          if (!t || !this.byName[`map${slot}_macro`]) continue;
          const v = toNorm(t, this.values[t.id] ?? t.def);
          this.setP(`map${slot}_macro`, k + 1);
          this.setP(`map${slot}_destination`, t.id + 1);
          this.setP(`map${slot}_from`, v);
          this.setP(`map${slot}_to`, Math.min(1, v + travel));
          slot++;
        }
        this.setP(`macro${k + 1}`, 0);
        labels[k] = name;
        return;
      }
      const d = this.byName[n];
      if (!d || !this.byName[`map${slot}_macro`]) return;
      const v = toNorm(d, this.values[d.id] ?? d.def), send = k >= 6;
      const freq = d.unit === 'Hz' || /cutoff/.test(n), opens = /attack/.test(n);
      const from = send ? 0 : Math.max(0, v - (freq ? 0.4 : 0.35)), to = send ? 1 : Math.min(1, v + (freq ? 0.3 : opens ? 0.12 : 0.35));
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

  // An empty sound on an instrument, bare (#bare). It goes through the patch reader like any sound; the clip in the
  // sequencer stays, and goes on playing with it.
  newSound = async (engine) => {
    const prev = [this.current, this.info];
    this.current = '';
    this.info = null;
    const r = await this.applyPatch(JSON.stringify({ version: PATCH_VERSION, engine, name: `${engine}_new` }));
    if (!r?.ok) { if (r) [this.current, this.info] = prev; return false; }
    this.#bare();
    this.baseline = { ...this.values };
    this.picks++;
    return true;
  };

  // A new sound starts bare: the instrument's first source alone; every other module and every effect switched off,
  // no modulation route (part or voice), Wander still, no macro (its targets, names and values cleared), and what
  // still moves or adds at its default set still (engines.js, BARE).
  #bare = () => {
    let kept = false;
    for (const r of engineStages(this)) {
      const st = r.st;
      if (!kept && st?.kind === 'source') { // the main oscillator, on
        kept = true;
        if (st.enable && !stageOn(this, st)) this.setP(st.enable, st.on);
      } else if (st?.enable && stageOn(this, st)) this.setP(st.enable, st.off);
    }
    for (const r of fxStages(this)) if (r.st?.enable && stageOn(this, r.st)) this.setP(r.st.enable, r.st.off);
    for (const kind of ['part', 'voice']) for (const k of routeKeys(kind)) if (routeInUse(this, k)) clearRoute(this, k);
    if (this.byName.wander) this.setP('wander', 0);
    for (let m = 1; m <= 32; m++) if (this.byName[`map${m}_macro`] && this.getP(`map${m}_macro`)) this.setP(`map${m}_macro`, 0);
    for (let k = 1; k <= 8; k++) if (this.byName[`macro${k}`]) this.setP(`macro${k}`, 0);
    this.macroNames = Array(8).fill('');
    for (const [name, v] of Object.entries(BARE[this.engine?.name] ?? {})) if (this.byName[name]) this.setP(name, v);
  };

  // a sound of the library; returns true when it is the current one. Its demo clip (clips/<name>.json, a file of
  // its own) comes into the sequencer, and goes on playing if a clip was playing
  loadPatch = async (p) => {
    const wasPlaying = !!this.clipHook?.playing();
    this.clipHook?.stop();
    const seq = ++this.#loadSeq;
    let text;
    try {
      const res = await fetch('patches/' + p.file);
      if (!res.ok) throw new Error(`${p.file}: ${res.status}`);
      text = await res.text();
    } catch (e) {
      this.say('Could not load the sound: ' + e.message);
      return false;
    }
    // its name first, with its values (the history records them as one state); back to the previous one if refused
    const prev = [this.current, this.info];
    this.current = p.file;
    this.info = p;
    const r = await this.applyPatch(text, seq);
    if (!r?.ok) { if (seq === this.#loadSeq) [this.current, this.info] = prev; return false; }
    this.baseline = { ...this.values };
    this.picks++;
    remember('synth.sound', p.name);
    await this.loadDemo(true);
    if (wasPlaying && seq === this.#loadSeq) this.clipHook?.play();
    return true;
  };

  // the current sound's demo clip (clips/<name>.json) into the sequencer; quiet: no notice when it has none
  loadDemo = async (quiet = false) => {
    const name = this.current ? this.current.replace(/\.json$/, '') : '';
    let doc = null;
    if (name) try { const res = await fetch(`clips/${name}.json`); if (res.ok) doc = await res.json(); } catch { /* none */ }
    if (!doc) { if (!quiet) this.notify(name ? `${this.label} has no demo clip` : 'A new sound has no demo clip yet'); return false; }
    this.clipHook?.take({ name, clip: doc });
    const t = this.byName.tempo; // the demo's tempo is the synth's: not an edit of the sound
    if (t && doc.bpm) { this.setP('tempo', doc.bpm); this.baseline = { ...this.baseline, [t.id]: this.values[t.id] }; }
    return true;
  };

  // A patch's text into the synth (the C patch reader checks it): its values, its macro names, its kit.
  // Returns the worklet's answer ({ok: false, error} if refused), or null if another load overtook it.
  applyPatch = async (text, seq = ++this.#loadSeq) => {
    let r;
    try { r = await this.synth.loadPatch(text); } catch (e) { r = { ok: false, error: 'Could not load the sound: ' + e.message }; }
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
      this.description = typeof o.description === 'string' ? o.description : '';
      if (!Array.isArray(o.macros)) this.defaultMacros(); // a sound that sets its macros (names, values, routes from them) keeps them as they are
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
  async useKit(buf, name) {
    const r = await this.synth.loadKit(buf);
    if (r.count < 0) { this.notify('Not a valid kit', 'error'); return false; }
    this.kit = parseKit(buf);
    this.kitName = name;
    return true;
  }
  async loadNamedKit(name) {
    try {
      const res = await fetch(`kits/${name}.oidk`);
      if (!res.ok) throw new Error(res.status === 404 ? 'not built (just build)' : String(res.status));
      await this.useKit(await res.arrayBuffer(), name);
    } catch (e) {
      this.notify(`Kit ${name}: ${e.message}`, 'error');
    }
  }
  dropFiles = async (files) => {
    this.notify(`Reading ${files.length} file${files.length > 1 ? 's' : ''}…`);
    const { buffer, failed } = await filesToKit(files);
    if (buffer && (await this.useKit(buffer, '')))
      this.notify(failed.length ? `Kit loaded; not audio: ${failed.join(', ')}` : `Kit loaded: ${this.kit.entries.length} samples`);
    else if (!buffer) this.notify('No audio file could be read', 'error');
  };

  // the current sound as a patch object (docs/patch_format.md): only what differs from the defaults
  currentPatch = () => {
    const engine = this.engine, getP = this.getP;
    const label = (d, v) => {
      if (/^route\d+_destination$/.test(d.name)) return this.#mapTarget(Math.round(v) - 1)?.role ?? 0; // by role (the engine's or an effect's)
      return d.labels ? d.labels[Math.round(v - d.min)] : +v.toPrecision(5);
    };
    // envelope points beyond the point count are leftovers of an earlier, longer shape: not part of the sound
    const unusedPoint = (name) => {
      const m = name.match(/^(mseg\d|vmseg)_[tlc](\d+)$/);
      return m && +m[2] > Math.round(getP(m[1] + '_points'));
    };
    const settings = {}; // by module (the parameters' roles), in the catalog's order
    for (const d of [...engine.params, ...this.shared])
      if (Math.abs((this.values[d.id] ?? d.def) - d.def) > 1e-6 && !unusedPoint(d.name) && !/^(map\d+_|macro\d+(_cc)?$)/.test(d.name))
        putRole(settings, d.role, label(d, this.values[d.id]));
    // the macros as the readable "macros" section (docs/patch_format.md): name, value, targets in their units
    const macros = [];
    for (let k = 0; k < 8; k++) {
      const targets = [];
      for (let s = 1; s <= 32; s++) {
        const dst = this.paramById(Math.round(getP(`map${s}_destination`)) - 1);
        if (Math.round(getP(`map${s}_macro`)) !== k + 1 || !dst) continue;
        const v = (n) => { const x = fromNorm(dst, n); return dst.labels ? dst.labels[Math.round(x - dst.min)] : +x.toPrecision(5); };
        targets.push({ param: dst.role, from: v(getP(`map${s}_from`)), to: v(getP(`map${s}_to`)) });
      }
      const value = +getP(`macro${k + 1}`).toPrecision(4), cc = Math.round(getP(`macro${k + 1}_cc`));
      macros.push({ ...(this.macroNames[k] ? { name: this.macroNames[k] } : {}), ...(value ? { value } : {}),
                    ...(cc !== 102 + k ? { cc } : {}), ...(targets.length ? { targets } : {}) });
    }
    while (macros.length && !Object.keys(macros[macros.length - 1]).length) macros.pop();
    const name = this.current ? this.current.replace('.json', '') : engine.name + '_new';
    const p = { version: PATCH_VERSION, engine: engine.name, name, description: this.description };
    if (this.kitName && engine.name === 'sampler') p.kit = this.kitName; // a dropped kit has no name to save
    Object.assign(p, settings);
    if (macros.length) p.macros = macros;
    return p;
  };

  exportPatch = () => {
    if (this.edited) this.compare(); // the edit is what is exported, not the loaded sound heard meanwhile
    const p = this.currentPatch(), name = p.name;
    const text = dumps(p);
    navigator.clipboard?.writeText(text).catch(() => {});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = name + '.json';
    a.click();
    this.notify(`Exported ${name}.json, also copied to the clipboard`);
  };

  // ---- save into patches/ (development): the board's presets are generated from there; the sequencer's clip is
  // saved as the sound's demo (clips/<name>.json) ----
  openSave = () => {
    if (this.edited) this.compare();
    const p = this.currentPatch();
    this.saving = { name: p.name, description: p.description, exists: false, error: '' };
  };
  save = async (overwrite = false) => {
    const p = this.currentPatch();
    p.description = this.saving.description.trim();
    const res = await fetch('/api/patches', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: this.saving.name.trim(), patch: p, clip: this.clipHook?.give() ?? null, overwrite }),
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
  // Randomize: every setting of the instrument thrown somewhere else at once (effects, modulation and macros are
  // left alone). A few are kept where the result stays playable: the volume and the bend range do not move, the
  // attack stays short enough to hear something, a level is either off (one in four) or well up, and the settings
  // that wreck everything (feedback, mutate) are drawn low more often than high.
  randomize = () => {
    const e = this.engine;
    if (!e) return;
    const r = Math.random;
    let moved = 0;
    for (const d of e.params) {
      if (d.name === 'volume' || d.name === 'bend_range' || d.name === 'kit') continue;
      let n = r();
      if (/attack$/.test(d.name)) n = 0.35 * r();
      else if (/release$/.test(d.name)) n = 0.2 + 0.5 * r();
      else if (/(^|_)level(_|$)/.test(d.name)) n = r() < 0.25 ? 0 : 0.4 + 0.6 * r();
      else if (d.name === 'feedback' || d.name === 'mutate') n = r() * r();
      this.setValue(d, fromNorm(d, n));
      moved++;
    }
    // never a silent result: the first level of the instrument is up
    const first = e.params.find((d) => /(^|_)level(_|$)/.test(d.name));
    if (first) this.setValue(first, fromNorm(first, 0.6 + 0.4 * r()));
    this.notify(`Randomized ${moved} settings. Undo or Revert brings the sound back.`);
  };
  // the sound before or after the current one in the library (it goes round); from a new sound, the first or last
  step = async (dir) => {
    const list = this.ordered, n = list.length;
    if (!n) return;
    const i = list.findIndex((p) => p.file === this.current);
    await this.loadPatch(list[i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n]);
  };
  // A/B: hear the sound as it was loaded, then the edit again. The values go straight to the synth (no macro
  // follows them: both states are whole). Moving a setting while the loaded sound is heard starts a new edit from
  // it (TopBar drops the one waiting; Undo brings it back).
  compare = () => {
    const to = this.edited ?? this.baseline;
    const waiting = this.edited ? null : { ...this.values };
    for (const d of [...this.engine.params, ...this.shared]) {
      const v = to[d.id] ?? d.def;
      if (Math.abs((this.values[d.id] ?? d.def) - v) < 1e-9) continue;
      this.values[d.id] = v;
      this.synth.setParam(d.id, toNorm(d, v));
    }
    this.edited = waiting;
  };
  revert = async () => {
    if (this.info) await this.loadPatch(this.info);
    else this.resetEngine();
  };
}

export const session = new Session();
if (import.meta.env.DEV) window.session = session; // console debugging
