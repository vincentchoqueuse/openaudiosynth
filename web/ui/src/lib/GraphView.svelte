<!-- GraphView.svelte — the Synth view's Graph, in two levels as a modular patch is read: first the sound as one line
     from left to right, In (the notes) → Event → Engine → Transform → Output → Out (the speakers), four blocks wired
     together, each naming what is on inside it and showing, as pictograms, the kinds of modulation acting there
     (graphmods.js). A double-click (or Enter) on a block enters it: a panel of its own blocks, again read from left to
     right, from its input to its output (ChainView with ports), each block with the pictograms of what moves it. There,
     a click on a block opens its settings in the side panel, a click on a pictogram that modulator; the path on top
     (Sound / the part) goes back up. -->
<script>
  import ChainView from './ChainView.svelte';
  import Icon from './Icon.svelte';
  import { session as s } from './session.svelte.js';
  import { ICONS } from './icons.js';
  import { PARTS, partOf, modsByStage, MOD_ICON } from './graphmods.js';
  let { onpick } = $props();

  const isOn = (st) => !st.enable || Math.abs(s.getP(st.enable) - st.off) > 1e-6;
  const stages = $derived([...(s.engine?.stages ?? []), ...(s.fxStages ?? [])]);
  const badges = $derived(modsByStage(s));
  // each part: its stages (those on), and the kinds of modulation acting in it
  const parts = $derived(PARTS.map((p) => {
    const ids = stages.filter((st) => partOf(st) === p.id).map((st) => st.id);
    const on = stages.filter((st) => ids.includes(st.id) && isOn(st));
    const kinds = [...new Set(ids.flatMap((id) => (badges[id] ?? []).map((b) => b.kind)))];
    return { ...p, ids, on, kinds };
  }));
  const current = $derived(parts.find((p) => p.id === s.graphPart) ?? null);
  const enter = (p) => (s.graphPart = p.id);
  let sel = $state(''); // the block clicked once (a double-click enters it)

  // the top level: In, the four parts, Out, on one line across the width
  let W = $state(800);
  const PW = 48, BH = 84, GAP = 34, TOP = 14;
  const BW = $derived(Math.max(130, Math.min(200, (W - 2 * PW - 5 * GAP) / 4)));
  const width = $derived(2 * PW + 4 * BW + 5 * GAP);
  const xOf = (i) => PW + GAP + i * (BW + GAP);
  const H = TOP * 2 + BH;
  const wire = (x1, x2) => `M${x1},${TOP + BH / 2} L${x2},${TOP + BH / 2}`;
  const words = (p) => (p.on.length ? p.on.map((st) => st.label).join(' · ') : p.id === 'event' ? 'the notes' : 'nothing on');
  const KIND = { env: 'envelope', lfo: 'LFO', macro: 'macro', ctl: 'controller' };
</script>

<nav class="path" aria-label="Where you are in the graph">
  <button class="crumb" class:on={!current} onclick={() => (s.graphPart = '')}>Sound</button>
  {#if current}<span class="sep" aria-hidden="true">/</span><span class="here"><Icon name={current.icon} size={13} /> {current.label}</span>{/if}
  <span class="tip muted">{current ? 'from its input to its output · a block opens its settings, a pictogram what moves it' : 'double-click a block to enter it'}</span>
</nav>

{#if !current}
  <div class="chain patch" bind:clientWidth={W}>
    <div class="scroll">
      <svg width={width} height={H} role="group" aria-label="The sound, from the notes to the speakers">
        <path class="wire" d={wire(PW, xOf(0))} />
        {#each parts as p, i (p.id)}{#if i}<path class="wire" d={wire(xOf(i - 1) + BW, xOf(i))} />{/if}{/each}
        <path class="wire" d={wire(xOf(3) + BW, xOf(3) + BW + GAP)} />
        <g class="block port" transform="translate(0,{TOP + BH / 2 - 16})"><rect width={PW} height="32" rx="16" /><text x={PW / 2} y="20" text-anchor="middle">In</text></g>
        <g class="block port" transform="translate({xOf(3) + BW + GAP},{TOP + BH / 2 - 16})"><rect width={PW} height="32" rx="16" /><text x={PW / 2} y="20" text-anchor="middle">Out</text></g>
        {#each parts as p, i (p.id)}
          <g class="block part pick" class:selected={sel === p.id} transform="translate({xOf(i)},{TOP})" role="button" tabindex="0"
             aria-label="{p.label}: {words(p)}. Double-click to enter"
             onclick={() => (sel = p.id)} ondblclick={() => enter(p)}
             onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); enter(p); } }}>
            <title>{p.label}: {p.about}. Double-click to enter.</title>
            <rect width={BW} height={BH} rx="9" />
            <svg x="10" y="10" width="16" height="16" viewBox="0 0 16 16" class="picon"><path d={ICONS[p.icon].d} />{#if ICONS[p.icon].fill}<path class="fill" d={ICONS[p.icon].fill} />{/if}</svg>
            <text class="ptitle" x="32" y="23">{p.label}</text>
            <text class="pwords" x="10" y="46">{words(p).length > BW / 6.2 ? words(p).slice(0, Math.floor(BW / 6.2) - 1) + '…' : words(p)}</text>
            {#each p.kinds as k, j}
              <g class="badge {k}" transform="translate({10 + j * 19},{BH - 26})"><title>{KIND[k]}</title><rect width="16" height="16" rx="4" />
                <svg x="2" y="2" width="12" height="12" viewBox="0 0 16 16"><path d={ICONS[MOD_ICON[k]].d} />{#if ICONS[MOD_ICON[k]].fill}<path class="fill" d={ICONS[MOD_ICON[k]].fill} />{/if}</svg></g>
            {/each}
          </g>
        {/each}
      </svg>
    </div>
  </div>
{:else if current.on.length}
  <ChainView {onpick} only={current.ids} {badges} ports portLabels={{ in: current.id === 'event' ? 'Notes' : 'In', out: current.id === 'output' ? 'Speakers' : 'Out' }} />
{:else}<p class="empty muted">Nothing is on in {current.label}.</p>{/if}

<style>
  .path { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; margin: 4px 0 10px; font-size: 12px; }
  .crumb { box-sizing: border-box; height: 26px; padding: 0 10px; border: 1px solid var(--line); border-radius: 6px; background: var(--surface); color: var(--text); }
  .crumb:hover { border-color: var(--accent); }
  .crumb.on { color: var(--muted); }
  .sep { color: var(--muted); }
  .here { display: inline-flex; align-items: center; gap: 6px; font-weight: 600; }
  .tip { margin-left: auto; font-size: 11.5px; }
  .patch :global(.block.part rect) { fill: var(--surface); stroke: var(--track); stroke-width: 1.2; }
  .patch :global(.block.part.selected rect), .patch :global(.block.part:hover rect) { stroke: var(--accent); }
  .patch .ptitle { font: 600 12.5px var(--sans); fill: var(--text); }
  .patch .pwords { font: 11px var(--sans); fill: var(--muted); }
  .patch .picon path { fill: none; stroke: var(--accent); stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
  .patch .picon path.fill { fill: var(--accent); stroke: none; }
  .patch :global(.block.part) { cursor: pointer; }
  .empty { padding: 20px 0; }
</style>
