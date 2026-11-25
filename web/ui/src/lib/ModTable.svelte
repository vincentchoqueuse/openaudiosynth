<!-- ModTable.svelte — the Modulation page, two tables. Modulators: the two global envelopes and the per-note one,
     the two global LFOs and the per-note one (lib/mods.js), and Wander; each its scope, its type, the line of its
     settings and what it moves. Routes: every route in use, of the whole sound and of each note (lib/routes.js):
     source, setting moved, amount in the setting's units, via. Configure opens either in the side panel; New route
     opens a free one there. -->
<script>
  import DataTable from './DataTable.svelte';
  import { session as s } from './session.svelte.js';
  import { modRows } from './mods.js';
  import * as R from './routes.js';

  // a modulator's line: an LFO's wave and rate or division, an envelope's points and loop
  function line(m) {
    if (m.key === 'wander') return `${(s.getP('wander') * 100).toFixed(0)} % · ${s.getP('wander_rate').toPrecision(2)} Hz`;
    const p = m.prefix, lab = (n) => { const d = s.byName[p + n]; return d?.labels?.[Math.round(s.getP(p + n) - d.min)]; };
    if (m.type === 'LFO') {
      const sync = Math.round(s.getP(p + 'sync'));
      return `${lab('wave') ?? ''} · ${sync ? lab('sync') : `${s.getP(p + 'rate').toPrecision(3)} Hz`}`;
    }
    const n = Math.round(s.getP(p + 'points')), loop = lab('mode');
    return `${n} points${loop && loop !== 'oneshot' ? ' · ' + loop.replace('_', ' ') : ''}`;
  }
  const routesOf = (m) => [...R.keys('part'), ...R.keys('voice')].map((k) => R.get(s, k)).filter((x) => x.src && x.srcLabel === m.src);
  const mods = $derived(modRows());
  const modCols = [{ id: 'name', label: 'Modulator', w: '20%' }, { id: 'scope', label: 'Scope', w: '12%', cls: 'muted' },
                   { id: 'type', label: 'Type', w: '13%', cls: 'muted' }, { id: 'sum', label: 'Settings', w: '24%', cls: 'sum' },
                   { id: 'moves', label: 'Moves', cls: 'sum' }];
  function moves(m) {
    if (m.key === 'wander') return s.getP('wander') > 0 ? 'the macros' : '—';
    const xs = routesOf(m);
    return xs.length ? xs.map((x) => R.dstName(s, x)).join(', ') : '—';
  }

  // the routes in use, and the one open in the panel even while it has no source yet
  const routes = $derived([...R.keys('part'), ...R.keys('voice')].map((k) => R.get(s, k))
    .filter((x) => x.src || (s.panel?.kind === 'route' && s.panel.id === x.key))
    .map((x) => ({ ...x, group: x.kind === 'part' ? 'On the whole sound' : 'On each note' })));
  const routeCols = [{ id: 'num', label: '#', w: '40px', cls: 'muted mono' }, { id: 'src', label: 'Source', w: '20%' },
                     { id: 'dst', label: 'Setting', w: '28%' }, { id: 'amt', label: 'Amount', w: '16%', cls: 'mono' }, { id: 'via', label: 'Via', cls: 'muted' }];
  const viaName = (x) => (x.kind === 'part' && x.via ? '× ' + R.srcName(R.srcLabels(s, 'part')[x.via]) : '');
  function newRoute() { const k = R.free(s, 'part') ?? R.free(s, 'voice'); if (k) s.openPanel('route', k); }
  const sel = $derived(s.panel ? s.panel.id : null);
</script>

<h3 class="sub">Modulators</h3>
<DataTable label="Modulators" columns={modCols} rows={mods} selected={s.panel?.kind === 'mod' ? sel : null} onopen={(r) => s.openPanel('mod', r.key)}>
  {#snippet cell(r, c)}
    {#if c.id === 'name'}<span class="strong">{r.label}</span>
    {:else if c.id === 'scope'}{r.kind === 'part' ? 'Whole sound' : 'Each note'}
    {:else if c.id === 'type'}{r.type}
    {:else if c.id === 'sum'}{line(r)}
    {:else}<span class:off={moves(r) === '—'}>{moves(r)}</span>{/if}
  {/snippet}
</DataTable>

<div class="sub-head">
  <h3 class="sub">Routes</h3>
  <button class="btn ghost" onclick={newRoute} disabled={!R.free(s, 'part') && !R.free(s, 'voice')}>+ New route</button>
</div>
<DataTable label="Routes" columns={routeCols} rows={routes} selected={s.panel?.kind === 'route' ? sel : null} onopen={(r) => s.openPanel('route', r.key)}
           empty="Nothing is modulated yet: a route sends a source (an LFO, an envelope, the velocity…) to a setting.">
  {#snippet cell(r, c)}
    {#if c.id === 'num'}{r.r}
    {:else if c.id === 'src'}<span class="strong">{R.srcName(r.srcLabel)}</span>
    {:else if c.id === 'dst'}{r.src ? R.dstName(s, r) : ''}
    {:else if c.id === 'amt'}{r.src ? R.inUnits(s, r) : ''}
    {:else}{viaName(r)}{/if}
  {/snippet}
</DataTable>
