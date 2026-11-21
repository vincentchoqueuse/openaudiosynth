// vite.config.js — dev server and build of the web prototype. The engine and the sounds are served from where
// they live, without copies: synth.wasm from build-wasm (`just wasm`), the patches from patches/ with an index
// generated on the fly (patches/index.json), the songs from songs/ likewise (songs/index.json), the demo sample
// kits from build-host/kits (NAME.oidk, recorded by host/kit_demo at every host build); `vite build` emits them
// next to the app. In development only, POST /api/patches saves a sound into patches/ (the board's presets are generated from there).
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { readFileSync, readdirSync, existsSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const ROOT = resolve(__dirname, '../..');
const WASM = resolve(ROOT, 'build-wasm/web/wasm/synth.wasm');
const PATCHES = resolve(ROOT, 'patches');
const SONGS = resolve(ROOT, 'songs');
const KITS = resolve(ROOT, 'build-host/kits');
const kitFiles = () => (existsSync(KITS) ? readdirSync(KITS).filter((f) => /^[a-z0-9_]+\.oidk$/.test(f)) : []);

function patchIndex() {
  const out = [];
  for (const file of readdirSync(PATCHES).filter((f) => f.endsWith('.json')).sort()) {
    try {
      const p = JSON.parse(readFileSync(resolve(PATCHES, file), 'utf8'));
      out.push({ file, name: p.name ?? '', engine: p.engine ?? '', description: p.description ?? '' });
    } catch (e) { // one malformed patch must not empty the browser
      console.warn(`patches/${file} skipped: ${e.message}`);
    }
  }
  return out;
}

// the songs (songs/): name, description, tempo, length and track count, for the Song tab
function songIndex() {
  const out = [];
  for (const file of readdirSync(SONGS).filter((f) => f.endsWith('.json')).sort()) {
    try {
      const s = JSON.parse(readFileSync(resolve(SONGS, file), 'utf8'));
      out.push({ file, name: s.name ?? file.replace(/\.json$/, ''), description: s.description ?? '', bpm: s.bpm ?? 90,
        seconds: (s.length_beats ?? 64) * 60 / (s.bpm ?? 90) + (s.tail_s ?? 6), tracks: (s.tracks ?? []).length });
    } catch (e) {
      console.warn(`songs/${file} skipped: ${e.message}`);
    }
  }
  return out;
}

function wasmMissing() {
  return `synth.wasm not built: run \`just wasm\` (or cmake --build --preset wasm)`;
}

// POST /api/patches {name, patch, overwrite}: writes patches/<name>.json. Same checks as a patch file must pass
// (a name of lowercase letters, digits and _, format version 1, an engine), no silent overwrite. Only this page
// may call it: a JSON content type (a cross-site form or text/plain post cannot send one without a preflight,
// which this server does not answer) and, when the browser sends one, an Origin of this server.
function savePatch(req, res) {
  const type = String(req.headers['content-type'] ?? '').split(';')[0].trim();
  const origin = req.headers.origin, host = req.headers.host;
  if (type !== 'application/json' || (origin && origin !== `http://${host}` && origin !== `https://${host}`)) {
    res.statusCode = 403;
    res.end('forbidden: same-origin JSON requests only');
    req.resume();
    return;
  }
  let body = '';
  req.on('data', (c) => {
    body += c;
    if (body.length > 1 << 20) req.destroy();
  });
  req.on('end', () => {
    const reply = (status, obj) => {
      res.statusCode = status;
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify(obj));
    };
    let m;
    try { m = JSON.parse(body); } catch { return reply(400, { error: 'not JSON' }); }
    const name = String(m?.name ?? '');
    if (!/^[a-z0-9_]{1,48}$/.test(name)) return reply(400, { error: 'name: lowercase letters, digits and _ only' });
    const patch = m.patch;
    if (!patch || patch.version !== 1 || typeof patch.engine !== 'string') return reply(400, { error: 'not a patch (version 1, engine)' });
    const file = resolve(PATCHES, name + '.json');
    if (existsSync(file) && !m.overwrite) return reply(409, { error: `patches/${name}.json exists`, exists: true });
    writeFileSync(file, JSON.stringify({ ...patch, name }, null, 2) + '\n');
    reply(200, { file: name + '.json' });
  });
}

function engineAndSounds() {
  return {
    name: 'synth-engine-and-sounds',
    configureServer(server) {
      server.watcher.add([WASM, PATCHES, SONGS]);
      server.middlewares.use((req, res, next) => {
        if (req.method === 'POST' && (req.url ?? '').split('?')[0] === '/api/patches') return savePatch(req, res);
        let url;
        try { url = decodeURIComponent((req.url ?? '').split('?')[0]); } catch { next(); return; }
        if (url.endsWith('/synth.wasm')) {
          if (!existsSync(WASM)) { res.statusCode = 404; res.end(wasmMissing()); return; }
          res.setHeader('Content-Type', 'application/wasm');
          res.end(readFileSync(WASM));
          return;
        }
        const k = url.match(/\/kits\/([a-z0-9_]+\.oidk)$/);
        if (k) {
          const f = resolve(KITS, k[1]);
          if (!existsSync(f)) { res.statusCode = 404; res.end(`${k[1]}: not built (just build)`); return; }
          res.setHeader('Content-Type', 'application/octet-stream');
          res.end(readFileSync(f));
          return;
        }
        const m = url.match(/\/(patches|songs)\/([\w.-]+\.json)$/);
        if (m) {
          const [dir, index] = m[1] === 'songs' ? [SONGS, songIndex] : [PATCHES, patchIndex];
          const body = m[2] === 'index.json' ? JSON.stringify(index()) : (() => {
            const f = resolve(dir, m[2]);
            return existsSync(f) ? readFileSync(f, 'utf8') : null;
          })();
          if (body === null) { next(); return; }
          res.setHeader('Content-Type', 'application/json');
          res.setHeader('Cache-Control', 'no-store');
          res.end(body);
          return;
        }
        next();
      });
    },
    generateBundle() {
      if (!existsSync(WASM)) this.error(wasmMissing());
      this.emitFile({ type: 'asset', fileName: 'synth.wasm', source: readFileSync(WASM) });
      const index = patchIndex();
      for (const p of index)
        this.emitFile({ type: 'asset', fileName: `patches/${p.file}`, source: readFileSync(resolve(PATCHES, p.file), 'utf8') });
      this.emitFile({ type: 'asset', fileName: 'patches/index.json', source: JSON.stringify(index) });
      const songs = songIndex();
      for (const so of songs)
        this.emitFile({ type: 'asset', fileName: `songs/${so.file}`, source: readFileSync(resolve(SONGS, so.file), 'utf8') });
      this.emitFile({ type: 'asset', fileName: 'songs/index.json', source: JSON.stringify(songs) });
      for (const f of kitFiles()) this.emitFile({ type: 'asset', fileName: `kits/${f}`, source: readFileSync(resolve(KITS, f)) });
    },
  };
}

export default defineConfig({ plugins: [svelte(), engineAndSounds()], base: './' });
