<!-- AboutPanel.svelte — the About tab of the side panel: what the module does (about.js), where its signal comes
     from and goes (a stage), or what it moves (a source of modulation, a macro), then each of its settings with what
     it does (the descriptor's help text), as a table. -->
<script>
  import { about } from './about.js';
  import { human } from './naming.js';
  let { kind, row, params = [], links = [] } = $props(); // params: [{d, label}]; links: [[label, text]]
</script>

<p class="sp-about">{about(kind, row)}</p>
{#if links.length}
  <table class="ptable kv"><tbody>
    {#each links as [k, v]}<tr><th>{k}</th><td>{v}</td></tr>{/each}
  </tbody></table>
{/if}
{#if params.length}
  <table class="ptable kv"><thead><tr><th>Setting</th><th>What it does</th></tr></thead><tbody>
    {#each params as { d, label } (d.id)}<tr><th>{label ?? human(d.name)}</th><td>{d.help || '—'}</td></tr>{/each}
  </tbody></table>
{/if}
