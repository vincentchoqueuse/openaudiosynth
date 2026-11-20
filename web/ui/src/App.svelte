<!-- App.svelte — web prototype of the synth: sound library (instruments, then their presets, as on the board),
     the current sound (name, description, changes, audition player), then from top to bottom: the macros, the output
     (wave, spectrum, level), the signal chain, and the editors: parameters generated from the descriptor
     tables (tabs: sound, effects, part,
     modulation with graphical envelopes and routes, step sequencer), Web MIDI and keyboard. The current sound
     can be exported as a patch file (docs/patch_format.md) or, in development, saved into patches/, from which
     the board's presets are generated. The whole interface takes the color of the instrument playing. The
     sampler's Sound tab is its kit (KitPanel): the kit a patch names is fetched (kits/NAME.oidk), or audio
     files dropped on it become one. -->
<script>
  import { Synth, MOD_PARAM, PART_PARAM, toNorm, fromNorm } from './lib/synth.js';
  import Param from './lib/Param.svelte';
  import Keyboard from './lib/Keyboard.svelte';
  import Scope from './lib/Scope.svelte';
  import ModPanel from './lib/ModPanel.svelte';
  import Sequencer from './lib/Sequencer.svelte';
  import ChainView from './lib/ChainView.svelte';
  import Library from './lib/Library.svelte';
  import KitPanel from './lib/KitPanel.svelte';
  import MacroStrip from './lib/MacroStrip.svelte';
  import JsonPanel from './lib/JsonPanel.svelte';
  import { parseKit, filesToKit } from './lib/kit.js';
  import { engineColor } from './lib/engines.js';
  import { keyIsForControl } from './lib/keys.js';
  import { REPO_URL, SITE_URL } from './lib/links.js';

  const synth = new Synth();
  if (import.meta.env.DEV) window.synth = synth; // console debugging

  let started = $state(false), starting = $state(false);
  let catalog = $state(null);
  let engineId = $state(1);
  let values = $state({}); // id -> physical value
  let patches = $state([]);
  let current = $state(''); // patch file
  let message = $state('');
  let toast = $state(null), toastTimer = null; // {text, kind}: a short notice at the bottom
  let info = $state(null); // index entry of the current patch (name, engine, description)
  let saving = $state(null); // the save dialog: {name, description, exists, error}
  let baseline = $state({}); // values when the sound was loaded: what "edited" compares with
  const canSave = import.meta.env.DEV; // writing into patches/ goes through the dev server
  let midiNames = $state([]);
  let held = $state(new Set());
  let notes = $state(''), loop = $state(false), playing = $state(false);
  let tab = $state('sound');
  let peak = $state(-Infinity);
  let lastOn = {}; // value of a stage's switch parameter before it was turned off (this sound only)
  let playTimer = null; // ends the "playing" state of a one-shot audition
  let loadSeq = 0; // the latest patch load wins
  let kit = $state.raw(null); // the sample kit the synth plays (parsed, for display), null none
  let kitName = $state(''); // its name when it comes from kits/ (a patch can name it), '' for dropped files
  let kitLabel = $state('');
  let macroNames = $state(Array(8).fill('')); // the patch's "macros" names (the values live in the synth's parameters)

  const engine = $derived(catalog?.engines[engineId]);
  const shared = $derived(catalog?.shared ?? []);

  // ---- naming: "osc1_wave" -> group "Osc 1", label "Wave" ----
  const WORDS = { lfo: 'LFO', mseg: 'MSEG', fenv: 'Filter env', penv: 'Pitch env', amp: 'Amp', env: 'Env', fx: 'FX', vel: 'Velocity', op: 'Op', bpm: 'BPM', pw: 'PW', pwm: 'PWM', fm: 'FM', db: 'dB' };
  const word = (w) => WORDS[w] ?? w.replace(/^([a-z]+)(\d+)$/, (_, a, n) => (WORDS[a] ?? cap(a)) + ' ' + n);
  const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
  const human = (s) => cap(s.split('_').map(word).join(' '));

  function grouped(params, filter) {
    const count = {};
    for (const d of params) { const p = d.name.split('_')[0]; count[p] = (count[p] || 0) + 1; }
    const out = new Map();
    for (const d of params) {
      if (filter && !d.name.includes(filter)) continue;
      const parts = d.name.split('_'), p = parts[0], own = count[p] > 1 && parts.length > 1;
      const g = own ? human(p) : 'Main';
      if (!out.has(g)) out.set(g, []);
      out.get(g).push({ d, label: own ? human(parts.slice(1).join('_')) : human(d.name) });
    }
    return [...out].sort((a, b) => (a[0] === 'Main' ? -1 : b[0] === 'Main' ? 1 : 0));
  }
  const TABS = [['sound', 'Sound'], ['fx', 'Effects'], ['part', 'Part'], ['mod', 'Modulation'], ['seq', 'Sequencer'], ['json', 'JSON']];
  const tabParams = $derived(
    !engine ? [] :
    tab === 'sound' ? engine.params :
    tab === 'fx' ? shared.filter((d) => d.id < PART_PARAM) :
    tab === 'part' ? shared.filter((d) => d.id >= PART_PARAM && d.id < MOD_PARAM) :
    shared.filter((d) => d.id >= MOD_PARAM));
  // sections: one per stage of the signal chain (engine stages on the Sound tab, the effect bus on Effects),
  // each with its on/off switch; parameters no stage claims go to "Other". Without stages: by name prefix.
  const stageMatch = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
  function byStage(stages, params) {
    const used = new Set(), out = [];
    for (const st of stages) {
      const ps = params.filter((d) => !used.has(d.id) && stageMatch(st.params, d.name));
      ps.forEach((d) => used.add(d.id));
      if (ps.length) out.push([st.label, ps.map((d) => ({ d, label: human(d.name) })), st]);
    }
    const rest = params.filter((d) => !used.has(d.id));
    if (rest.length) out.push(['Other', rest.map((d) => ({ d, label: human(d.name) })), null]);
    return out;
  }
  const groups = $derived(
    tab === 'sound' && engine?.stages?.length ? byStage(engine.stages, engine.params) :
    tab === 'fx' && catalog?.fx ? byStage(catalog.fx, tabParams) :
    grouped(tabParams, '').map(([g, ps]) => [g, ps, null]));
  const stageOn = (st) => !st?.enable || Math.abs(getP(st.enable) - st.off) > 1e-6;
  function toggleStage(st) {
    if (stageOn(st)) { lastOn[st.enable] = getP(st.enable); setP(st.enable, st.off); }
    else setP(st.enable, lastOn[st.enable] ?? st.on);
  }
  // parameters by name (engine first, then shared), and name-based get / set for the editors
  const byName = $derived.by(() => {
    const m = {};
    for (const d of shared) m[d.name] = d;
    for (const d of engine?.params ?? []) m[d.name] = d;
    return m;
  });
  const getP = (n) => { const d = byName[n]; return d ? values[d.id] ?? d.def : 0; };
  // a synth parameter id: the engine's (0..255) or a shared one (master, effects...)
  const paramById = (id) => (id >= 0 && id < 256 ? engine?.params[id] : shared.find((d) => d.id === id));
  const setP = (n, v) => { const d = byName[n]; if (d) setValue(d, v); };
  const color = $derived(engineColor(engine?.name));
  function notify(text, kind = 'info') {
    clearTimeout(toastTimer);
    toast = { text, kind };
    toastTimer = setTimeout(() => (toast = null), kind === 'error' ? 6000 : 3500);
  }
  $effect(() => { if (message) { notify(message, /could not|error|unknown/i.test(message) ? 'error' : 'info'); message = ''; } });
  // parameters moved since the sound was loaded (or since the defaults, for a new sound)
  const changedCount = $derived(engine ? [...engine.params, ...shared].filter((d) => Math.abs((values[d.id] ?? d.def) - (baseline[d.id] ?? d.def)) > 1e-6).length : 0);


  function resetValues(withShared) {
    const v = withShared ? {} : { ...values };
    for (const d of engine.params) v[d.id] = d.def;
    if (withShared) for (const d of shared) v[d.id] = d.def;
    values = v;
    baseline = { ...v };
  }

  async function start() {
    starting = true;
    try {
      catalog = await synth.start();
      started = true;
      resetValues(true);
      synth.on('midiin', (d) => {
        const t = d[0] & 0xf0;
        if (t === 0xb0) { // a macro's CC: the synth has turned it, the knob follows
          for (let k = 1; k <= 8; k++)
            if (Math.round(getP(`macro${k}_cc`)) === d[1]) { const m = byName[`macro${k}`]; if (m) values[m.id] = d[2] / 127; }
          return;
        }
        if (t === 0x90 && d[2] > 0) held.add(d[1]);
        else if (t === 0x80 || (t === 0x90 && d[2] === 0)) held.delete(d[1]);
        else return;
        held = new Set(held);
      });
      await listPatches();
      synth.enableMidi((n) => (midiNames = n)).catch(() => (midiNames = []));
    } catch (e) {
      message = 'Could not start audio: ' + e.message;
    }
    starting = false;
  }

  async function listPatches() {
    try {
      const r = await fetch('patches/index.json');
      if (!r.ok) throw new Error(await r.text());
      patches = await r.json();
    } catch (e) {
      patches = [];
      message = 'Could not list the patches: ' + e.message;
    }
  }

  function setEngine(id) {
    stop();
    engineId = id;
    synth.setEngine(id);
    current = '';
    info = null;
    lastOn = {};
    resetValues(false);
    // routes point at engine parameters by index: they mean nothing for another engine
    for (let r = 1; r <= 8; r++) if (getP(`route${r}_destination`)) setP(`route${r}_destination`, 0);
  }

  // back to the engine's default sound (the engine too, not only the sliders)
  function resetEngine() {
    lastOn = {};
    for (const d of engine.params) if (Math.abs((values[d.id] ?? d.def) - d.def) > 1e-9) setValue(d, d.def);
  }

  function setValue(d, v) {
    values[d.id] = v;
    synth.setParam(d.id, toNorm(d, v));
  }

  async function loadPatch(p) {
    const seq = ++loadSeq;
    let text;
    try {
      const res = await fetch('patches/' + p.file);
      if (!res.ok) throw new Error(`${p.file}: ${res.status}`);
      text = await res.text();
    } catch (e) {
      message = 'Could not load the patch: ' + e.message;
      return;
    }
    const r = await applyPatch(text, seq);
    if (!r?.ok) return;
    current = p.file;
    info = p;
    baseline = { ...values };
    notes = '';
    tab = 'sound';
  }

  // A patch's text into the synth (the C patch reader checks it): its values, its macro names, its kit.
  // Returns the worklet's answer ({ok: false, error} if refused), or null if another load overtook it.
  async function applyPatch(text, seq = ++loadSeq) {
    let r;
    try { r = await synth.loadPatch(text); } catch (e) { r = { ok: false, error: 'Could not load the patch: ' + e.message }; }
    if (seq !== loadSeq) return null; // another patch was clicked meanwhile
    if (!r.ok) { message = r.error; return r; }
    clearTimeout(playTimer);
    playing = false; // loading a patch re-initializes the engine: the player is stopped
    lastOn = {};
    engineId = r.engine;
    resetValues(true); // a patch starts from a fresh synth_init, like synth_render
    for (const [id, v] of r.values) values[id] = v;
    try {
      const macros = JSON.parse(text).macros ?? [];
      macroNames = Array.from({ length: 8 }, (_, k) => String(macros[k]?.name ?? ''));
    } catch { macroNames = Array(8).fill(''); }
    message = r.warning;
    if (r.kit && r.kit !== kitName) await loadNamedKit(r.kit); // the synth keeps the last kit across patches
    return r;
  }

  // ---- the JSON editor: the sound as its patch file, and a hand-edited file back into the synth ----
  function patchWithAudition() {
    const p = currentPatch();
    if (kitName && engine.name === 'sampler') p.kit = kitName;
    if (notes) p.audition = { notes };
    return p;
  }
  async function applyFromEditor(text) {
    const r = await applyPatch(text);
    if (!r) return { ok: false, text: 'Overtaken by another load' };
    if (!r.ok) return { ok: false, text: r.error };
    try { const a = JSON.parse(text).audition; if (a?.notes) notes = a.notes; } catch {}
    return { ok: true, text: r.warning ? 'Applied, with warnings: ' + r.warning : 'Applied' };
  }

  // ---- sample kits ----
  async function useKit(buf, name, label) {
    const r = await synth.loadKit(buf);
    if (r.count < 0) { notify('Not a valid kit', 'error'); return false; }
    kit = parseKit(buf);
    kitName = name;
    kitLabel = label;
    return true;
  }
  async function loadNamedKit(name) {
    try {
      const res = await fetch(`kits/${name}.oidk`);
      if (!res.ok) throw new Error(res.status === 404 ? 'not built (just build)' : String(res.status));
      await useKit(await res.arrayBuffer(), name, name);
    } catch (e) {
      notify(`Kit ${name}: ${e.message}`, 'error');
    }
  }
  async function dropFiles(files) {
    notify(`Reading ${files.length} file${files.length > 1 ? 's' : ''}…`);
    const { buffer, failed } = await filesToKit(files);
    if (buffer && (await useKit(buffer, '', `${files.length - failed.length} dropped file${files.length - failed.length > 1 ? 's' : ''}`)))
      notify(failed.length ? `Kit loaded; not audio: ${failed.join(', ')}` : `Kit loaded: ${kit.entries.length} samples`);
    else if (!buffer) notify('No audio file could be read', 'error');
  }

  async function play() {
    const r = await synth.audition(notes, loop);
    if (r.length < 0) message = r.error;
    else {
      playing = true;
      message = '';
      clearTimeout(playTimer);
      if (!loop) playTimer = setTimeout(() => (playing = false), r.length * 1000 + 300);
    }
  }
  function stop() { clearTimeout(playTimer); synth.stop(); playing = false; }

  // the current sound as a patch object (docs/patch_format.md): only what differs from the defaults
  function currentPatch() {
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
    for (const d of [...engine.params, ...shared])
      if (Math.abs((values[d.id] ?? d.def) - d.def) > 1e-6 && !unusedPoint(d.name) && !/^(map\d+_|macro\d+(_cc)?$)/.test(d.name))
        params[d.name] = label(d, values[d.id]);
    // the macros as the readable "macros" section (docs/patch_format.md): name, value, targets in their units
    const macros = [];
    for (let k = 0; k < 8; k++) {
      const targets = [];
      for (let s = 1; s <= 32; s++) {
        const dst = paramById(Math.round(getP(`map${s}_destination`)) - 1);
        if (Math.round(getP(`map${s}_macro`)) !== k + 1 || !dst) continue;
        const v = (n) => { const x = fromNorm(dst, n); return dst.labels ? dst.labels[Math.round(x - dst.min)] : +x.toPrecision(5); };
        targets.push({ param: dst.name, from: v(getP(`map${s}_from`)), to: v(getP(`map${s}_to`)) });
      }
      const value = +getP(`macro${k + 1}`).toPrecision(4), cc = Math.round(getP(`macro${k + 1}_cc`));
      macros.push({ ...(macroNames[k] ? { name: macroNames[k] } : {}), ...(value ? { value } : {}),
                    ...(cc !== 102 + k ? { cc } : {}), ...(targets.length ? { targets } : {}) });
    }
    while (macros.length && !Object.keys(macros[macros.length - 1]).length) macros.pop();
    const name = current ? current.replace('.json', '') : engine.name + '_new';
    const p = { version: 1, engine: engine.name, name, description: info?.description ?? '', params };
    if (macros.length) p.macros = macros;
    if (kitName && engine.name === 'sampler') p.kit = kitName; // a dropped kit has no name to save
    return p;
  }

  function exportPatch() {
    const p = currentPatch(), name = p.name;
    if (notes) p.audition = { notes };
    const text = JSON.stringify(p, null, 2) + '\n';
    navigator.clipboard?.writeText(text).catch(() => {});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = name + '.json';
    a.click();
    notify(`Exported ${name}.json, also copied to the clipboard`);
  }

  // ---- save into patches/ (development): the board's presets are generated from there ----
  function openSave() {
    const p = currentPatch();
    saving = { name: p.name, description: p.description, exists: false, error: '' };
  }
  async function save(overwrite = false) {
    const p = currentPatch();
    if (notes) p.audition = { notes };
    p.description = saving.description.trim();
    const res = await fetch('/api/patches', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: saving.name.trim(), patch: p, overwrite }),
    }).catch((e) => ({ ok: false, status: 0, json: async () => ({ error: e.message }) }));
    const r = await res.json().catch(() => ({ error: 'no answer from the dev server' }));
    if (res.status === 409) { saving = { ...saving, exists: true, error: '' }; return; }
    if (!res.ok) { saving = { ...saving, error: r.error ?? 'could not save' }; return; }
    saving = null;
    baseline = { ...values };
    await listPatches();
    current = r.file;
    info = patches.find((x) => x.file === r.file) ?? info;
    notify(`Saved patches/${r.file}. On the board: just build, then just flash.`);
  }
  async function revert() {
    if (info) await loadPatch(info);
    else resetEngine();
  }

  function onKey(e) {
    if (saving && e.key === 'Escape') { saving = null; return; }
    // in the library, Space auditions the sound just picked instead of clicking its button again
    const inLibrary = e.code === 'Space' && e.target?.closest?.('.library nav');
    if (!started || saving || tab === 'seq' || (!inLibrary && keyIsForControl(e))) return;
    if (e.code === 'Space') { e.preventDefault(); playing ? stop() : current && play(); }
  }
</script>

<svelte:window onkeydown={onKey} />

{#if !started}
  <main class="splash">
    <div class="mark">OASynth</div>
    <div class="byline">by Choc</div>
    <p>The DSP engine of the boards, compiled to WebAssembly, playing in your browser.</p>
    <button class="primary" onclick={start} disabled={starting}>{starting ? 'Loading…' : 'Start audio'}</button>
    {#if toast}<p class="error">{toast.text}</p>{/if}
    <nav class="links" aria-label="Project">
      <a href={REPO_URL} target="_blank" rel="noopener noreferrer" title="Source code on GitHub" aria-label="Source code on GitHub">
        <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/></svg>
      </a>
      <a href={SITE_URL} target="_blank" rel="noopener noreferrer" title="vchoqueuse.fr" aria-label="Author's website, vchoqueuse.fr">
        <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="6.6"/><ellipse cx="8" cy="8" rx="2.9" ry="6.6"/><path d="M1.6 5.8h12.8M1.6 10.2h12.8"/></g></svg>
      </a>
    </nav>
  </main>
{:else}
  <div class="app" style="--accent:{color}">
    <aside>
      <div class="brand"><span class="logo"></span>OASynth <span class="by">by Choc</span></div>
      <Library {patches} {current} onpick={loadPatch} />
      <div class="side-foot">
        <span class="midi" class:on={midiNames.length} title={midiNames.join(', ') || 'No MIDI input'}>
          {midiNames.length ? midiNames[0] : 'no MIDI input'}
        </span>
        <nav class="links" aria-label="Project">
          <a href={REPO_URL} target="_blank" rel="noopener noreferrer" title="Source code on GitHub" aria-label="Source code on GitHub">
            <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/></svg>
          </a>
          <a href={SITE_URL} target="_blank" rel="noopener noreferrer" title="vchoqueuse.fr" aria-label="Author's website, vchoqueuse.fr">
            <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="6.6"/><ellipse cx="8" cy="8" rx="2.9" ry="6.6"/><path d="M1.6 5.8h12.8M1.6 10.2h12.8"/></g></svg>
          </a>
        </nav>
      </div>
    </aside>

    <main>
      <header class="sound">
        <div class="ident">
          <div class="kicker">
            <select class="engine" value={engineId} onchange={(e) => setEngine(+e.target.value)} aria-label="Instrument">
              {#each catalog.engines as e}<option value={e.id}>{e.name}</option>{/each}
            </select>
            {#if changedCount}
              <span class="badge" title="parameters moved since the sound was loaded">{changedCount} edited</span>
              <button class="link" onclick={revert} title={info ? 'reload the saved sound' : 'back to the defaults'}>Revert</button>
            {/if}
          </div>
          <h1>{current ? current.replace('.json', '') : 'init'}</h1>
          <p class="desc">{info?.description || engine?.description}</p>
        </div>
        <div class="actions">
          {#if canSave}<button class="btn" onclick={openSave}>Save to library</button>{/if}
          <button class="btn ghost" onclick={exportPatch}>Export</button>
        </div>
      </header>

      <div class="player">
        <button class="play" class:on={playing} onclick={() => (playing ? stop() : play())} disabled={!current && !playing}
                aria-label={playing ? 'Stop' : 'Play audition'} title="Space">{playing ? '■' : '▶'}</button>
        <input class="phrase" placeholder={current ? 'Audition phrase, or type notes: C3 E3 G3+B3 - _' : 'Load a sound to audition it'} bind:value={notes} />
        <label class="toggle"><input type="checkbox" bind:checked={loop} /> Loop</label>
      </div>

      <MacroStrip {engine} {shared} {byName} {values} {setValue} bind:names={macroNames} {baseline} />

      <Scope analyser={synth.analyser} bind:peak />

      <ChainView {engine} fx={catalog.fx} {values} {byName} {getP} />

      <div class="tabs" role="tablist">
        {#each TABS as [id, name]}
          <button role="tab" aria-selected={tab === id} class:on={tab === id} onclick={() => (tab = id)}>{name}</button>
        {/each}
        {#if tab === 'sound'}<button class="link small" onclick={resetEngine}>Defaults</button>{/if}
      </div>

      {#if tab === 'json'}
        <JsonPanel getPatch={patchWithAudition} apply={applyFromEditor} />
      {:else if tab === 'mod'}
        <ModPanel {engine} {shared} get={getP} set={setP} {byName} {human} {values} {setValue} />
      {:else if tab === 'seq'}
        <Sequencer {synth} setTempo={(b) => setP('tempo', b)} getPatch={currentPatch} />
      {:else}
        {#if tab === 'sound' && engine?.name === 'sampler'}
          <KitPanel {kit} kitName={kitLabel} {byName} {values} {setValue} {synth} onfiles={dropFiles} />
        {:else}
        <div class="groups">
          {#each groups as [g, ps, st] (g)}
            <section class="card" class:off={!stageOn(st)}>
              <div class="stage-head">
                <h2>{g}</h2>
                {#if st?.enable}
                  <button class="onoff" class:on={stageOn(st)} onclick={() => toggleStage(st)} role="switch"
                          aria-checked={stageOn(st)} aria-label="{g} on/off" title={stageOn(st) ? 'on' : 'off'}></button>
                {/if}
              </div>
              {#each ps as { d, label } (d.id)}
                <Param {d} {label} value={values[d.id] ?? d.def} onchange={(v) => setValue(d, v)} />
              {/each}
            </section>
          {/each}
        </div>
        {/if}
      {/if}
    </main>

    <footer><Keyboard {synth} {held} /></footer>

    {#if toast}<div class="toast {toast.kind}" role="status">{toast.text}</div>{/if}

    {#if saving}
      <div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (saving = null)}>
        <form class="dialog" onsubmit={(e) => { e.preventDefault(); save(saving.exists); }}>
          <h2>Save to the library</h2>
          <p class="muted">Writes <code>patches/{saving.name || '…'}.json</code>. The board's presets are generated
            from this folder: <code>just build</code>, then <code>just flash</code>.</p>
          <label>Name <input bind:value={saving.name} pattern="[a-z0-9_]+" required autocomplete="off"
                 oninput={() => (saving.exists = false)} /></label>
          <label>Description <textarea rows="3" bind:value={saving.description} placeholder="What it sounds like, how to play it"></textarea></label>
          {#if saving.error}<p class="error">{saving.error}</p>{/if}
          {#if saving.exists}<p class="warn">patches/{saving.name}.json already exists.</p>{/if}
          <div class="row">
            <button type="button" class="btn ghost" onclick={() => (saving = null)}>Cancel</button>
            <button type="submit" class="btn primary">{saving.exists ? 'Replace' : 'Save'}</button>
          </div>
        </form>
      </div>
    {/if}
  </div>
{/if}
