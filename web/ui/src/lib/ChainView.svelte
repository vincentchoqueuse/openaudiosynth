<!-- ChainView.svelte — the signal chain of the current engine followed by the effect bus, drawn from the stage
     descriptors (dsp/engines/engine.h): audio stages in columns along the signal, modulators below; pointing at a
     modulator draws its dashed links to what it acts on (including the active part routes: MSEG, LFO, macros...). It
     reflects the controls (a stage switched off is drawn dashed, the signal passes through it), and with `onpick`
     it is the editor's map: a click on a block or a modulator opens its settings. -->
<script>
  // onpick(stage, kind): a block or modulator clicked ('audio' for the signal's stages, 'mod' for a modulator, 'route'
  // for a part route); without it the diagram is display only
  let { engine, fx, values, byName, getP, onpick = null } = $props();

  let W = $state(800);
  let hover = $state(-1); // modulator under the pointer: only its links are drawn
  const BW = 112, BH = 36, ROWH = 54, TOP = 12;

  // audio stages of the engine (or one block for engines without a description), then the effect bus
  const audio = $derived.by(() => {
    const st = engine?.stages?.length ? engine.stages : [{ id: 'engine', label: engine?.name ?? 'engine', kind: 'source', params: '', inputs: '', targets: '', enable: null }];
    const eng = st.filter((s) => s.kind !== 'mod' && s.kind !== 'voice');
    const fed = new Set(eng.flatMap((s) => s.inputs.split(' ').filter(Boolean)));
    const sinks = eng.filter((s) => !fed.has(s.id)).map((s) => s.id);
    const bus = fx.map((s) => ({ ...s, inputs: s.inputs === 'engine' ? sinks.join(' ') : s.inputs }));
    return [...eng, ...bus];
  });
  const mods = $derived((engine?.stages ?? []).filter((s) => s.kind === 'mod' || s.kind === 'voice'));

  // columns by longest path from the sources
  const layout = $derived.by(() => {
    const byId = Object.fromEntries(audio.map((s) => [s.id, s]));
    const col = {};
    const depth = (id, seen = new Set()) => {
      if (col[id] !== undefined) return col[id];
      if (seen.has(id)) return 0;
      seen.add(id);
      const ins = (byId[id]?.inputs ?? '').split(' ').filter((x) => byId[x]);
      return (col[id] = ins.length ? 1 + Math.max(...ins.map((x) => depth(x, seen))) : 0);
    };
    audio.forEach((s) => depth(s.id));
    const ncol = Math.max(...Object.values(col)) + 1;
    const rows = {}, pos = {};
    for (const s of audio) {
      const c = col[s.id], r = (rows[c] = (rows[c] ?? -1) + 1);
      pos[s.id] = { c, r };
    }
    const nrow = Math.max(...Object.values(rows)) + 1;
    const gap = Math.max(18, (W - ncol * BW) / Math.max(1, ncol - 1));
    const scale = ncol * BW + (ncol - 1) * gap > W ? W / (ncol * BW + (ncol - 1) * 18) : 1;
    const bw = BW * Math.min(1, scale), g = ncol > 1 ? (W - ncol * bw) / (ncol - 1) : 0;
    const xy = {};
    for (const [id, { c, r }] of Object.entries(pos)) {
      const inCol = rows[c] + 1;
      xy[id] = { x: c * (bw + g), y: TOP + ((nrow - inCol) * ROWH) / 2 + r * ROWH, w: bw };
    }
    return { xy, height: TOP + nrow * ROWH, bw };
  });

  // modulators: the engine's own, plus the active part routes grouped by source
  const routeChips = $derived.by(() => {
    const out = new Map();
    for (let r = 1; r <= 8; r++) {
      const src = Math.round(getP(`route${r}_source`));
      const dst = engine?.params?.[Math.round(getP(`route${r}_destination`)) - 1];
      if (src <= 0 || !dst || !getP(`route${r}_amount`)) continue;
      const name = byName[`route${r}_source`]?.labels?.[src] ?? 'route';
      const st = stageOf(dst.name);
      if (!st) continue;
      if (!out.has(name)) out.set(name, new Set());
      out.get(name).add(st);
    }
    return [...out].map(([name, set]) => ({ id: 'route:' + name, label: name, kind: 'route', targets: [...set].join(' ') }));
  });
  const chips = $derived([...mods, ...routeChips]);
  const chipY = $derived(layout.height + 34);
  const chipX = (i) => (chips.length > 1 ? (i * (W - 96)) / (chips.length - 1) : (W - 96) / 2);
  const H = $derived(chips.length ? chipY + 30 : layout.height + 6);

  // parameters of a stage, by name tokens ("osc2_" = prefix)
  const matches = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
  function stageOf(paramName) {
    for (const s of [...(engine?.stages ?? []), ...fx]) if (s.kind !== 'mod' && s.kind !== 'voice' && matches(s.params, paramName)) return s.id;
    return engine?.stages?.length ? null : 'engine'; // no description: the engine is drawn as one block
  }
  const isOn = (s) => !s.enable || Math.abs(getP(s.enable) - s.off) > 1e-6;
  function edge(a, b) { // from the right side of a to the left side of b
    const p = layout.xy[a], q = layout.xy[b];
    const x1 = p.x + p.w, y1 = p.y + BH / 2, x2 = q.x, y2 = q.y + BH / 2, mx = (x1 + x2) / 2;
    return `M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}`;
  }
  function modEdge(i, target) {
    const q = layout.xy[target];
    if (!q) return '';
    const x1 = chipX(i) + 48, y1 = chipY - 11, x2 = q.x + q.w / 2, y2 = q.y + BH;
    return `M${x1},${y1} C${x1},${(y1 + y2) / 2} ${x2},${(y1 + y2) / 2} ${x2},${y2}`;
  }
</script>

<div class="chain">
  <div bind:clientWidth={W}>
  <svg width={W} height={H}>
    {#each audio as s}
      {#each s.inputs.split(' ').filter((x) => layout.xy[x]) as from}
        <path class="wire" class:dim={!isOn(audio.find((t) => t.id === from))} d={edge(from, s.id)} />
      {/each}
    {/each}
    {#each chips as c, i}
      {#if hover === i}
        {#each c.targets.split(' ').filter(Boolean) as t}
          <path class="modwire" class:route={c.kind === 'route'} d={modEdge(i, t)} />
        {/each}
      {/if}
    {/each}
    {#each audio as s (s.id)}
      {@const p = layout.xy[s.id]}
      {@const on = isOn(s)}
      <!-- svelte-ignore a11y_no_noninteractive_tabindex (the role is a button whenever it is focusable) -->
      <g class="block {s.kind}" class:off={!on} class:target={hover >= 0 && chips[hover]?.targets.split(' ').includes(s.id)}
         class:pick={!!onpick} transform="translate({p.x},{p.y})" role={onpick ? 'button' : 'img'} tabindex={onpick ? 0 : undefined}
         aria-label="{s.label}{on ? '' : ' (off)'}" onclick={() => onpick?.(s, 'audio')}
         onkeydown={(e) => { if (onpick && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); onpick(s, 'audio'); } }}>
        <title>{s.label}{on ? '' : ' (off)'}</title>
        <rect width={p.w} height={BH} rx="7" />
        <text x="9" y={BH / 2 + 4}>{s.label}</text>
        {#if s.enable}<circle class="led" class:on cx={p.w - 9} cy="9" r="3" />{/if}
      </g>
    {/each}
    {#each chips as c, i (c.id)}
      <!-- svelte-ignore a11y_no_noninteractive_tabindex (the role is a button whenever it is focusable) -->
      <g class="chip {c.kind}" class:hot={hover === i} class:pick={!!onpick} transform="translate({chipX(i)},{chipY - 11})"
         role={onpick ? 'button' : 'img'} tabindex={onpick ? 0 : undefined} aria-label="{c.label} modulates {c.targets}"
         onmouseenter={() => (hover = i)} onmouseleave={() => (hover = -1)} onclick={() => onpick?.(c, c.kind === 'route' ? 'route' : 'mod')}
         onkeydown={(e) => { if (onpick && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); onpick(c, c.kind === 'route' ? 'route' : 'mod'); } }}>
        <rect width="96" height="22" rx="11" />
        <text x="48" y="15" text-anchor="middle">{c.label}</text>
      </g>
    {/each}
  </svg>
  </div>
</div>
