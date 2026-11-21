// synth.js — main-thread side of the web synth: AudioContext + AudioWorklet running synth.wasm,
// parameter conversions (same curves as dsp/mod/params.c), Web MIDI input, patch loading.

export const MOD_PARAM = 1024; // SYNTH_MOD_PARAM (dsp/synth.h)
export const PART_PARAM = 512; // SYNTH_PART_PARAM

// physical <-> normalized, mirror of param_from_norm / param_to_norm
export function fromNorm(d, n) {
  n = Math.min(1, Math.max(0, n));
  if (d.curve === 'exp') return d.min * Math.pow(d.max / d.min, n);
  const v = d.min + n * (d.max - d.min);
  return d.curve === 'int' ? Math.round(v) : v;
}
export function toNorm(d, v) {
  v = Math.min(d.max, Math.max(d.min, v));
  if (d.max === d.min) return 0;
  if (d.curve === 'exp') return Math.log(v / d.min) / Math.log(d.max / d.min);
  return (v - d.min) / (d.max - d.min);
}
export function format(d, v) {
  if (d.labels) return d.labels[Math.round(v - d.min)] ?? String(v);
  const a = Math.abs(v);
  let s;
  if (d.curve === 'int') s = String(Math.round(v));
  else if (d.unit === 's' && a < 1) return (v * 1000).toPrecision(3) + ' ms';
  else if (d.unit === 'Hz' && a >= 1000) return (v / 1000).toPrecision(3) + ' kHz';
  else s = a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2);
  return d.unit ? `${s} ${d.unit}` : s;
}

export class Synth {
  constructor() {
    this.ctx = null;
    this.node = null;
    this.analyser = null;
    this.catalog = null;
    this.listeners = {};
    this.midiInputs = [];
    this.pending = new Map(); // request id -> resolve
    this.nextReq = 1;
  }

  on(type, fn) { // returns an unsubscribe function
    (this.listeners[type] ||= []).push(fn);
    return () => { this.listeners[type] = this.listeners[type].filter((g) => g !== fn); };
  }
  emit(type, data) { (this.listeners[type] || []).forEach((f) => f(data)); }

  // Starts the audio. On failure everything is released and the error is thrown (start can be retried).
  async start() {
    const ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
    try {
      const [module] = await Promise.all([
        WebAssembly.compileStreaming(fetch('synth.wasm')),
        ctx.audioWorklet.addModule('synth-worklet.js'),
      ]);
      const node = new AudioWorkletNode(ctx, 'synth', {
        numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { module },
      });
      // the worklet answers 'ready' with the catalog, or 'error'; a crash or silence must not hang the page
      const msg = await new Promise((res, rej) => {
        const timer = setTimeout(() => rej(new Error('the audio engine did not start (timeout)')), 15000);
        node.onprocessorerror = () => { clearTimeout(timer); rej(new Error('the audio engine crashed while starting')); };
        node.port.onmessage = (e) => {
          if (e.data.type === 'ready') { clearTimeout(timer); res(e.data); }
          else if (e.data.type === 'error') { clearTimeout(timer); rej(new Error(e.data.message)); }
        };
      });
      node.port.onmessage = (e) => this.receive(e.data);
      this.catalog = JSON.parse(msg.catalog);
      this.analyser = ctx.createAnalyser();
      this.analyser.fftSize = 4096;
      this.analyser.smoothingTimeConstant = 0.6;
      // the listening volume, after the meter (which shows the synth's own level): outside the synth, so no
      // sound or patch changes it
      this.volume = ctx.createGain();
      node.connect(this.analyser);
      this.analyser.connect(this.volume);
      this.volume.connect(ctx.destination);
      this.ctx = ctx;
      this.node = node;
      await ctx.resume();
      return this.catalog;
    } catch (e) {
      ctx.close().catch(() => {});
      throw e;
    }
  }

  // a reply to a request resolves that request only; everything else goes to the listeners of its type
  receive(d) {
    const resolve = d.req !== undefined && this.pending.get(d.req);
    if (resolve) { this.pending.delete(d.req); resolve(d); }
    else this.emit(d.type, d);
  }

  send(m) { this.node?.port.postMessage(m); }
  midi(bytes) { this.send({ type: 'midi', data: Array.from(bytes) }); }
  noteOn(note, vel = 100) { this.midi([0x90, note, vel]); }
  noteOff(note) { this.midi([0x80, note, 0]); }
  setParam(id, norm) { this.send({ type: 'param', id, norm }); }
  setEngine(id) { this.send({ type: 'engine', id }); }
  // listening volume in dB (-60 or below: silent), glided over ~20 ms so that a move never clicks
  setVolume(db) {
    if (!this.volume) return;
    const g = db <= -60 ? 0 : 10 ** (db / 20);
    this.volume.gain.setTargetAtTime(g, this.ctx.currentTime, 0.02);
  }
  partParam(part, id, norm) { this.send({ type: 'partParam', part, id, norm }); }
  stop() { this.send({ type: 'stop' }); }

  request(type, m) { // each request carries an id, echoed by the worklet: concurrent requests cannot mix up
    return new Promise((res) => {
      const req = this.nextReq++;
      this.pending.set(req, res);
      this.send({ type, req, ...m });
    });
  }
  loadPatch(json) { return this.request('patch', { json }); }
  // a sample kit (ArrayBuffer in the format of dsp/core/kit.h; empty: none): resolves {count}, -1 if invalid
  loadKit(data) { return this.request('kit', { data }); }
  audition(notes = '', loop = false) { return this.request('audition', { notes, loop }); }
  // rows: one phrase per line (audition syntax); replaces a running loop in place
  sequence(rows, bpm, step, gate, swing, loop = true) { return this.request('sequence', { rows, bpm, step, gate, swing, loop }); }
  // a clip of notes [{pitch, start, length, velocity}] in beats over `length` beats; replaces a running loop in place
  notes(notes, bpm, length, loop = true) {
    const data = new Float32Array(notes.length * 4);
    [...notes].sort((a, b) => a.start - b.start).forEach((n, i) => data.set([n.pitch, n.start, n.length, n.velocity], 4 * i));
    return this.request('notes', { data: data.buffer, bpm, length, loop });
  }
  // a song (songs/ format) with its patches inline, played live one part per track: resolves {length, error, warning}
  song(json, loop = false) { return this.request('song', { json, loop }); }

  async enableMidi(onChange) {
    if (!navigator.requestMIDIAccess) throw new Error('Web MIDI unavailable (use Chrome or Edge)');
    const access = await navigator.requestMIDIAccess();
    const bind = () => {
      this.midiInputs = [...access.inputs.values()].filter((i) => i.state === 'connected');
      for (const input of this.midiInputs) {
        input.onmidimessage = (e) => {
          const d = e.data;
          if (d[0] >= 0xf0 && d[0] !== 0xf8 && d[0] !== 0xfa && d[0] !== 0xfb && d[0] !== 0xfc) return;
          this.midi(d);
          if (d[0] < 0xf0) this.emit('midiin', d); // not the clock (24 messages per beat)
        };
      }
      onChange?.(this.midiInputs.map((i) => i.name));
    };
    access.onstatechange = bind;
    bind();
  }
}
