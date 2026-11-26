<!-- ViewTitle.svelte — the title of a part of the page (Sequence, Synth, Code, Live control): its icon in a tinted
     square, then its name, or a path (the Synth view: instrument / sound / module open). -->
<script>
  import Icon from './Icon.svelte';
  let { icon, label, crumbs = null } = $props(); // crumbs: [{label, onclick?}] replaces the name by a path
</script>

<h1 class="viewtitle"><span class="vicon"><Icon name={icon} size={18} /></span>
  {#if crumbs}
    <span class="crumbs">{#each crumbs as c, i}{#if i}<span class="sep">/</span>{/if}
      {#if c.onclick && i < crumbs.length - 1}<button onclick={c.onclick}>{c.label}</button>{:else}<span class:last={i === crumbs.length - 1}>{c.label}</span>{/if}{/each}</span>
  {:else}{label}{/if}</h1>
