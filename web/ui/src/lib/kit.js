// kit.js — sample kits in the page: reading one (the demo kits served as NAME.oidk) to show its samples, and
// building one from audio files dropped by the user. Format of dsp/core/kit.h: "OIDK", version 1, count, a
// 32-byte entry per sample {name[20], note, flags, reserved u16, offset u32, frames u32}, 16-bit mono PCM at
// 48 kHz. The browser decodes the files (WAV, AIFF, MP3, FLAC...) and resamples them to the context's rate.

const MAGIC = 0x4b44494f, HEADER = 8, ENTRY = 32, NAME = 20, MAX = 128, RATE = 48000;

// {entries: [{name, note, frames, pcm: Int16Array}]} or null if not a valid kit
export function parseKit(buf) {
  const v = new DataView(buf);
  if (buf.byteLength < HEADER || v.getUint32(0, true) !== MAGIC || v.getUint16(4, true) !== 1) return null;
  const n = v.getUint16(6, true);
  if (n > MAX || buf.byteLength < HEADER + n * ENTRY) return null;
  const entries = [];
  for (let k = 0; k < n; k++) {
    const o = HEADER + k * ENTRY;
    const bytes = new Uint8Array(buf, o, NAME);
    const name = new TextDecoder().decode(bytes.subarray(0, Math.max(0, bytes.indexOf(0)) || NAME));
    const offset = v.getUint32(o + 24, true), frames = v.getUint32(o + 28, true);
    if (offset & 1 || offset + 2 * frames > buf.byteLength) return null;
    entries.push({ name, note: v.getUint8(o + 20), frames, pcm: new Int16Array(buf, offset, frames) });
  }
  return { entries };
}

// samples: [{name, note, data: Float32Array at 48 kHz}] -> ArrayBuffer (sorted by note: pad 1 is the lowest)
export function buildKit(samples) {
  const s = [...samples].sort((a, b) => a.note - b.note).slice(0, MAX);
  let off = HEADER + s.length * ENTRY;
  const offsets = s.map((x) => { const o = off; off += 2 * x.data.length; return o; });
  const buf = new ArrayBuffer(off), v = new DataView(buf);
  v.setUint32(0, MAGIC, true);
  v.setUint16(4, 1, true);
  v.setUint16(6, s.length, true);
  s.forEach((x, k) => {
    const o = HEADER + k * ENTRY;
    const name = new TextEncoder().encode(x.name).subarray(0, NAME - 1);
    new Uint8Array(buf, o, NAME).set(name);
    v.setUint8(o + 20, x.note & 127);
    v.setUint32(o + 24, offsets[k], true);
    v.setUint32(o + 28, x.data.length, true);
    for (let i = 0; i < x.data.length; i++) {
      const c = Math.max(-1, Math.min(1, x.data[i]));
      v.setInt16(offsets[k] + 2 * i, Math.round(c * 32767), true);
    }
  });
  return buf;
}

// Audio files -> kit. A name starting with a MIDI note ("36_kick.wav") keeps it; the others take the next free
// notes from 36 (C2, the kick of the drum maps) in name order.
export async function filesToKit(files) {
  const ctx = new OfflineAudioContext(1, 1, RATE);
  const list = [...files].sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true })).slice(0, MAX);
  const used = new Set(), out = [], failed = [];
  for (const f of list) {
    const m = f.name.match(/^(\d{1,3})[ _-]*/);
    if (m && +m[1] <= 127 && !used.has(+m[1])) used.add(+m[1]);
  }
  let next = 36;
  for (const f of list) {
    let audio;
    try { audio = await ctx.decodeAudioData(await f.arrayBuffer()); } catch { failed.push(f.name); continue; }
    const m = f.name.match(/^(\d{1,3})[ _-]*/);
    let note = m && +m[1] <= 127 ? +m[1] : -1;
    if (note < 0) { while (used.has(next) && next < 127) next++; note = next; used.add(note); }
    const data = new Float32Array(audio.length);
    for (let c = 0; c < audio.numberOfChannels; c++) {
      const ch = audio.getChannelData(c);
      for (let i = 0; i < data.length; i++) data[i] += ch[i] / audio.numberOfChannels;
    }
    const name = f.name.replace(/\.[^.]+$/, '').replace(/^\d{1,3}[ _-]*/, '') || f.name;
    out.push({ name, note, data });
  }
  return { buffer: out.length ? buildKit(out) : null, failed };
}

// min/max of a sample in `width` columns, for drawing its waveform
export function peaks(pcm, width) {
  const out = new Float32Array(2 * width), step = pcm.length / width;
  for (let x = 0; x < width; x++) {
    let lo = 0, hi = 0;
    const a = Math.floor(x * step), b = Math.max(a + 1, Math.floor((x + 1) * step));
    for (let i = a; i < b && i < pcm.length; i++) { const s = pcm[i] / 32768; if (s < lo) lo = s; if (s > hi) hi = s; }
    out[2 * x] = lo;
    out[2 * x + 1] = hi;
  }
  return out;
}

const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const noteName = (n) => NOTE[n % 12] + (Math.floor(n / 12) - 1);
