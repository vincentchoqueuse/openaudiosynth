// synth-worklet.js — AudioWorkletProcessor running synth.wasm (web/wasm/synth_wasm.c) on the audio
// thread. The main thread compiles the module and sends it in processorOptions; every API call
// (MIDI, parameters, patches) arrives through the port and runs here, between two renders.

// AudioWorkletGlobalScope has no TextEncoder / TextDecoder: minimal UTF-8 by hand.
function utf8Encode(s) {
  const out = [];
  for (const ch of s) {
    const c = ch.codePointAt(0);
    if (c < 0x80) out.push(c);
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }
  return Uint8Array.from(out);
}
function utf8Decode(b) {
  let s = '';
  for (let i = 0; i < b.length;) {
    const c = b[i++];
    if (c < 0x80) s += String.fromCharCode(c);
    else if (c < 0xe0) s += String.fromCharCode(((c & 31) << 6) | (b[i++] & 63));
    else if (c < 0xf0) s += String.fromCharCode(((c & 15) << 12) | ((b[i++] & 63) << 6) | (b[i++] & 63));
    else s += String.fromCodePoint(((c & 7) << 18) | ((b[i++] & 63) << 12) | ((b[i++] & 63) << 6) | (b[i++] & 63));
  }
  return s;
}

class SynthProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    try {
      const wasi = new Proxy({}, { get: () => () => 52 }); // ENOSYS: stdio is never used on the audio path
      const inst = new WebAssembly.Instance(options.processorOptions.module, {
        wasi_snapshot_preview1: wasi,
        env: new Proxy({}, { get: () => () => 0 }),
      });
      this.w = inst.exports;
      this.mem = inst.exports.memory;
      if (this.w._initialize) this.w._initialize();
      this.w.w_init(sampleRate);
      this.port.onmessage = (e) => this.onMessage(e.data);
      this.port.postMessage({ type: 'ready', catalog: this.str(this.w.w_catalog()), sampleRate });
    } catch (e) { // the main thread is waiting for 'ready': tell it instead of leaving it hanging
      this.w = null;
      this.port.postMessage({ type: 'error', message: String(e?.message ?? e) });
    }
  }

  str(ptr) {
    const b = new Uint8Array(this.mem.buffer, ptr);
    let n = 0;
    while (b[n]) n++;
    return utf8Decode(b.subarray(0, n));
  }

  withString(s, fn) {
    const bytes = utf8Encode(s);
    const p = this.w.w_alloc(bytes.length + 1);
    const b = new Uint8Array(this.mem.buffer, p, bytes.length + 1);
    b.set(bytes);
    b[bytes.length] = 0;
    try { return fn(p); } finally { this.w.w_free(p); }
  }

  // every request (a message with `req`) gets exactly one reply carrying the same `req`, also when it throws
  onMessage(m) {
    try {
      this.handle(m);
    } catch (e) {
      if (m.req !== undefined)
        this.port.postMessage({ type: m.type, req: m.req, ok: false, length: -1, error: String(e?.message ?? e) });
    }
  }

  handle(m) {
    const w = this.w;
    switch (m.type) {
      case 'midi': w.w_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0, m.data.length); break;
      case 'param': w.w_set_param(m.id, m.norm); break;
      case 'partParam': w.w_part_param(m.part, m.id, m.norm); break;
      case 'engine': w.w_set_engine(m.id); break;
      case 'stop': w.w_stop(); break;
      case 'patch': {
        const rc = this.withString(m.json, (p) => w.w_patch(p));
        const values = [];
        for (let i = 0; i < w.w_patch_count(); i++) values.push([w.w_patch_id(i), w.w_patch_value(i)]);
        this.port.postMessage({
          type: 'patch', req: m.req, ok: rc === 0, error: rc ? this.str(w.w_error()) : '',
          warning: this.str(w.w_warning()), engine: w.w_engine(), values, kit: rc === 0 ? this.str(w.w_patch_kit()) : '',
        });
        break;
      }
      case 'kit': { // a sample kit (dsp/core/kit.h): copied into wasm memory, owned by the engine from there
        const bytes = new Uint8Array(m.data);
        let n = -1;
        if (bytes.length) {
          const p = w.w_alloc(bytes.length);
          if (p) {
            new Uint8Array(w.memory.buffer, p, bytes.length).set(bytes);
            n = w.w_kit(p, bytes.length);
          }
        } else {
          n = w.w_kit(0, 0);
        }
        this.port.postMessage({ type: 'kit', req: m.req, count: n });
        break;
      }
      case 'audition': {
        const len = this.withString(m.notes || '', (p) => w.w_audition(p, m.loop ? 1 : 0));
        this.port.postMessage({ type: 'audition', req: m.req, length: len, error: len < 0 ? this.str(w.w_error()) : '' });
        break;
      }
      case 'notes': { // a clip: Float32Array of [pitch, start, length, velocity] per note, in beats
        const data = new Float32Array(m.data), p = data.length ? w.w_alloc(data.byteLength) : 0;
        if (data.length && !p) throw new Error('out of memory for the clip');
        if (p) new Float32Array(w.memory.buffer, p, data.length).set(data);
        let len;
        try { len = w.w_notes(p, data.length / 4, m.bpm, m.length, m.loop ? 1 : 0); } finally { if (p) w.w_free(p); }
        this.port.postMessage({ type: 'notes', req: m.req, length: len, error: len < 0 ? this.str(w.w_error()) : '' });
        break;
      }
      case 'song': { // a song with its patches inline, played live (one part per track)
        const len = this.withString(m.json || '', (p) => w.w_song(p, m.loop ? 1 : 0));
        this.port.postMessage({ type: 'song', req: m.req, length: len, error: len < 0 ? this.str(w.w_error()) : '',
          warning: this.str(w.w_warning()) });
        break;
      }
      case 'sequence': {
        const len = this.withString(m.rows || '', (p) => w.w_sequence(p, m.bpm, m.step, m.gate, m.swing || 0, m.loop ? 1 : 0));
        this.port.postMessage({ type: 'sequence', req: m.req, length: len, error: len < 0 ? this.str(w.w_error()) : '' });
        break;
      }
    }
  }

  process(inputs, outputs) {
    if (!this.w) return false; // failed to start
    const out = outputs[0];
    const n = out[0].length;
    this.w.w_render(n);
    if ((this.tick = (this.tick || 0) + 1) % 8 === 0) { // player position ~every 21 ms, for the sequencer playhead
      const pos = this.w.w_player_pos();
      if (pos >= 0 || this.lastPos >= 0) this.port.postMessage({ type: 'pos', frames: pos, sampleRate });
      this.lastPos = pos;
    }
    // views on the render buffers: rebuilt only when the memory has grown (a patch load) or the quantum changes,
    // so nothing is allocated on the audio thread in steady state
    if (this.buf !== this.mem.buffer || this.n !== n) {
      this.buf = this.mem.buffer;
      this.n = n;
      const k = Math.min(n, this.w.w_max_frames());
      this.L = new Float32Array(this.buf, this.w.w_buf_l(), k);
      this.R = new Float32Array(this.buf, this.w.w_buf_r(), k);
    }
    out[0].set(this.L);
    if (out[1]) out[1].set(this.R);
    return true;
  }
}

registerProcessor('synth', SynthProcessor);
