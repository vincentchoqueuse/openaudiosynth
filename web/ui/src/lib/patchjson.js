// patchjson.js — a patch (docs/patch_format.md, version 2) groups its settings by module: {"filter": {"cutoff": 800},
// "fx": {"delay": {"mix": 0.2}}}, each leaf's path a parameter's role. putRole places a value in that tree (the
// modules on the way are made, in the order they first come); dumps writes JSON as the library's files are written:
// 2-space indent, an object or an array on one line when it fits (tools/patchtree.py writes the same).
export const PATCH_VERSION = 2;

export function putRole(tree, role, value) {
  const parts = role.split('.');
  let node = tree;
  for (const p of parts.slice(0, -1)) node = node[p] ??= {};
  node[parts[parts.length - 1]] = value;
}

export function dumps(obj, width = 118) {
  const one = (v) => Array.isArray(v) ? '[' + v.map(one).join(', ') + ']'
    : v !== null && typeof v === 'object' ? '{' + Object.entries(v).map(([k, x]) => JSON.stringify(k) + ': ' + one(x)).join(', ') + '}'
    : JSON.stringify(v);
  const fmt = (v, ind, lead) => {
    const flat = one(v);
    if (v === null || typeof v !== 'object' || !Object.keys(v).length || flat.length + lead <= width) return flat;
    const pad = ' '.repeat(ind + 2);
    if (Array.isArray(v)) return '[\n' + v.map((x) => pad + fmt(x, ind + 2, ind + 2)).join(',\n') + '\n' + ' '.repeat(ind) + ']';
    return '{\n' + Object.entries(v).map(([k, x]) => {
      const key = JSON.stringify(k) + ': ';
      return pad + key + fmt(x, ind + 2, ind + 2 + key.length);
    }).join(',\n') + '\n' + ' '.repeat(ind) + '}';
  };
  return fmt(obj, 0, 0) + '\n';
}
