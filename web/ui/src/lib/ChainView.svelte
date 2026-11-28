<!-- ChainView.svelte — the signal chain of the current engine followed by the effect bus, drawn from the stage
     descriptors (dsp/engines/engine.h): audio stages in columns along the signal (the effects of the rack in the
     order it has now), modulators below; pointing at a
     modulator draws its dashed links to what it acts on (including the active part routes: MSEG, LFO, macros...). Only
     what is active is drawn (a stage switched off or an effect not in use is left out, the signal joins around it), and with `onpick`
     it is the editor's map: a click on a block or a modulator opens its settings. -->
<script>
  // onpick(stage, kind): a block or modulator clicked ('audio' for the signal's stages, 'mod' for a modulator, 'route'
  // for a part route); without it the diagram is display only
  import { session } from './session.svelte.js';
  import { srcName } from './routes.js';
  import { ICONS } from './icons.js';
  import { MOD_ICON } from './graphmods.js';
  // only: the stage ids drawn (a part of the sound: GraphView), the signal joining around the others; badges: stage
  // id -> its modulators as pictograms (graphmods.js), drawn on the block instead of the chips under the line
  // ports: an In block before the first stages and an Out block after the last (a part entered from the Graph), named
  // by `portLabels` {in, out}
  let { onpick = null, only = null, badges = null, ports = false, portLabels = { in: 'In', out: 'Out' } } = $props(); // a click on a block or a chip: onpick(stage, kind)
  const engine = $derived(session.engine), fx = $derived(session.fxStages), byName = $derived(session.byName);
  const getP = session.getP;

  let W = $state(800);
  let hover = $state(-1); // modulator under the pointer: only its links are drawn
  const BW = 112, BH = 36, ROWH = 64, TOP = 12, MINW = 92, GAP = 16; // blocks never narrower than MINW: the line scrolls

  // audio stages of the engine (or one block for engines without a description), then the effect bus
  const parts = $derived.by(() => {
    const st = engine?.stages?.length ? engine.stages : [{ id: 'engine', label: engine?.name ?? 'engine', kind: 'source', params: '', inputs: '', targets: '', enable: null }];
    const eng = st.filter((s) => s.kind !== 'mod' && (s.kind !== 'voice' || only?.includes(s.id))); // the voice only in its own part
    const fed = new Set(eng.flatMap((s) => s.inputs.split(' ').filter(Boolean)));
    const sinks = eng.filter((s) => !fed.has(s.id)).map((s) => s.id);
    const bus = fx.map((s) => ({ ...s, inputs: s.inputs === 'engine' ? sinks.join(' ') : s.inputs }));
    return { eng, bus };
  });
  // only what is active is drawn: a stage switched off (an effect not in use, an oscillator off) leaves the
  // picture, and the signal goes straight from what feeds it to what it fed
  const isOn = (s) => !s.enable || Math.abs(getP(s.enable) - s.off) > 1e-6;
  const inner = $derived.by(() => {
    const all = [...parts.eng, ...parts.bus], byId = Object.fromEntries(all.map((s) => [s.id, s]));
    const feeds = (id, seen = new Set()) => { // the active stages a stage's signal comes from
      if (seen.has(id) || !byId[id] || (only && !only.includes(id))) return [];
      seen.add(id);
      return isOn(byId[id]) ? [id] : byId[id].inputs.split(' ').filter(Boolean).flatMap((x) => feeds(x, seen));
    };
    const inPart = (st) => !only || only.includes(st.id);
    return all.filter((st) => isOn(st) && inPart(st)).map((s) => ({ ...s, inputs: [...new Set(s.inputs.split(' ').filter(Boolean).flatMap((x) => feeds(x)))].join(' ') }));
  });
  // with ports: In feeds the stages nothing in the part feeds, Out takes what feeds nothing in it
  const audio = $derived.by(() => {
    if (!ports || !inner.length) return inner;
    const used = new Set(inner.flatMap((s) => s.inputs.split(' ').filter(Boolean)));
    const sinks = inner.filter((s) => !used.has(s.id)).map((s) => s.id);
    return [{ id: 'in', label: portLabels.in, kind: 'port', inputs: '', enable: null },
            ...inner.map((s) => (s.inputs ? s : { ...s, inputs: 'in' })),
            { id: 'out', label: portLabels.out, kind: 'port', inputs: sinks.join(' '), enable: null }];
  });
  const mods = $derived((engine?.stages ?? []).filter((s) => (s.kind === 'mod' || s.kind === 'voice') && isOn(s)));

  // columns by longest path from the sources: the engine's stages and the bus on one line, which scrolls sideways
  // when the blocks would get narrower than MINW
  function columns(stages) {
    const byId = Object.fromEntries(stages.map((s) => [s.id, s]));
    const col = {};
    const depth = (id, seen = new Set()) => {
      if (col[id] !== undefined) return col[id];
      if (seen.has(id)) return 0;
      seen.add(id);
      const ins = (byId[id]?.inputs ?? '').split(' ').filter((x) => byId[x]);
      return (col[id] = ins.length ? 1 + Math.max(...ins.map((x) => depth(x, seen))) : 0);
    };
    stages.forEach((s) => depth(s.id));
    const rows = {}, pos = {};
    for (const s of stages) {
      const c = col[s.id], r = (rows[c] = (rows[c] ?? -1) + 1);
      pos[s.id] = { c, r };
    }
    return { pos, rows, ncol: Math.max(...Object.values(col)) + 1, nrow: Math.max(...Object.values(rows)) + 1 };
  }
  // the columns across the visible width, or wider (cw > W, the view scrolls) to keep each block at MINW
  const layout = $derived.by(() => {
    const cl = columns(audio), xy = {};
    const bw = Math.min(BW, Math.max(MINW, (W - (cl.ncol - 1) * GAP) / cl.ncol));
    const cw = Math.max(W, cl.ncol * bw + (cl.ncol - 1) * GAP);
    const g = cl.ncol > 1 ? (cw - cl.ncol * bw) / (cl.ncol - 1) : 0;
    const PORT = 56; // In and Out: narrow, at the two ends
    for (const [id, { c, r }] of Object.entries(cl.pos)) {
      const port = id === 'in' || id === 'out';
      xy[id] = { x: c * (bw + g) + (id === 'out' ? bw - PORT : 0), y: TOP + ((cl.nrow - (cl.rows[c] + 1)) * ROWH) / 2 + r * ROWH, w: port ? PORT : bw };
    }
    return { xy, height: TOP + cl.nrow * ROWH, cw };
  });

  // modulators: the engine's own, plus the active part routes grouped by source
  const routeChips = $derived.by(() => {
    const out = new Map();
    for (let r = 1; r <= 8; r++) {
      const src = Math.round(getP(`route${r}_source`));
      const dst = session.paramById(Math.round(getP(`route${r}_destination`)) - 1); // the engine's or an effect's
      if (src <= 0 || !dst || !getP(`route${r}_amount`)) continue;
      const name = srcName(byName[`route${r}_source`]?.labels?.[src]);
      const st = stageOf(dst.name);
      if (!st) continue;
      if (!out.has(name)) out.set(name, new Set());
      out.get(name).add(st);
    }
    return [...out].map(([name, set]) => ({ id: 'route:' + name, label: name, kind: 'route', targets: [...set].join(' ') }));
  });
  const chips = $derived(badges ? [] : [...mods, ...routeChips]);
  const chipY = $derived(layout.height + 34);
  const chipX = (i) => (chips.length > 1 ? (i * (layout.cw - 96)) / (chips.length - 1) : (layout.cw - 96) / 2);
  const H = $derived(chips.length ? chipY + 30 : layout.height + 6);

  // parameters of a stage, by name tokens ("osc2_" = prefix)
  const matches = (tokens, name) => tokens.split(' ').filter(Boolean).some((t) => (t.endsWith('_') ? name.startsWith(t) : name === t));
  function stageOf(paramName) {
    for (const s of [...(engine?.stages ?? []), ...fx]) if (s.kind !== 'mod' && s.kind !== 'voice' && matches(s.params, paramName)) return s.id;
    return engine?.stages?.length ? null : 'engine'; // no description: the engine is drawn as one block
  }
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
  <div class="scroll" bind:clientWidth={W}>
  <svg width={layout.cw} height={H}>
    {#each audio as s}
      {#each s.inputs.split(' ').filter((x) => layout.xy[x]) as from}
        <path class="wire" d={edge(from, s.id)} />
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
         class:pick={!!onpick && s.kind !== 'port'} transform="translate({p.x},{p.y})" role={onpick && s.kind !== 'port' ? 'button' : 'img'} tabindex={onpick && s.kind !== 'port' ? 0 : undefined}
         aria-label="{s.label}{on ? '' : ' (off)'}" onclick={() => s.kind !== 'port' && onpick?.(s, 'audio')}
         onkeydown={(e) => { if (onpick && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); onpick(s, 'audio'); } }}>
        <title>{s.label}{on ? '' : ' (off)'}</title>
        <rect width={p.w} height={BH} rx={s.kind === 'port' ? BH / 2 : 7} />
        <text x="9" y={BH / 2 + 4}>{s.label}</text>
        {#each (badges?.[s.id] ?? []).slice(0, Math.floor(p.w / 17)) as b, j}
          <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
          <g class="badge {b.kind}" transform="translate({j * 17},{BH + 2})" onclick={(e) => { e.stopPropagation(); b.open(); }}>
            <title>{b.label}: open it</title>
            <rect width="16" height="16" rx="4" />
            <svg x="2" y="2" width="12" height="12" viewBox="0 0 16 16"><path d={ICONS[MOD_ICON[b.kind]].d} />{#if ICONS[MOD_ICON[b.kind]].fill}<path class="fill" d={ICONS[MOD_ICON[b.kind]].fill} />{/if}</svg>
          </g>
        {/each}
        {#if s.enable && !badges}<circle class="led" class:on cx={p.w - 9} cy="9" r="3" />{/if}
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
