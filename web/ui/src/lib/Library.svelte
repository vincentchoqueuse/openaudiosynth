<!-- Library.svelte — the sound library, as on the board: instruments first (one per engine, in its color, with
     its number of presets), the presets of the open one below it; typing in the search box lists the matching
     presets of every instrument instead. -->
<script>
  import { engineColor, engineRank } from './engines.js';
  let { patches, current, onpick } = $props();

  let search = $state('');
  let open = $state(null); // engine whose presets are shown

  const instruments = $derived.by(() => {
    const m = new Map();
    for (const p of patches) {
      if (!m.has(p.engine)) m.set(p.engine, []);
      m.get(p.engine).push(p);
    }
    return [...m].sort((a, b) => engineRank(a[0]) - engineRank(b[0]) || a[0].localeCompare(b[0]));
  });
  const found = $derived.by(() => {
    const q = search.trim().toLowerCase();
    return q ? patches.filter((p) => (p.name + ' ' + p.engine + ' ' + p.description).toLowerCase().includes(q)) : null;
  });
  // the instrument of the sound playing opens by itself
  $effect(() => {
    const p = patches.find((x) => x.file === current);
    if (p) open = p.engine;
  });
</script>

<div class="library">
  <input class="search" type="search" placeholder="Search sounds" bind:value={search} aria-label="Search sounds" />
  <nav aria-label="Sounds">
    {#if found}
      {#each found as p (p.file)}
        <button class="preset" class:active={p.file === current} style="--c:{engineColor(p.engine)}" onclick={() => onpick(p)}>
          <span class="dot"></span><span class="pname">{p.name}</span><span class="peng">{p.engine}</span>
        </button>
      {:else}
        <p class="empty">No sound matches.</p>
      {/each}
    {:else}
      {#each instruments as [eng, list] (eng)}
        <button class="inst" class:open={open === eng} style="--c:{engineColor(eng)}" aria-expanded={open === eng}
                onclick={() => (open = open === eng ? null : eng)}>
          <span class="bar"></span><span class="iname">{eng}</span><span class="count">{list.length}</span>
        </button>
        {#if open === eng}
          <div class="presets" style="--c:{engineColor(eng)}">
            {#each list as p (p.file)}
              <button class="preset" class:active={p.file === current} title={p.description} onclick={() => onpick(p)}>
                <span class="pname">{p.name}</span>
              </button>
            {/each}
          </div>
        {/if}
      {/each}
    {/if}
  </nav>
</div>
