<!-- ViewTitle.svelte — the title of a part of the page (Sequence, Synth, Code, Live control): its icon in a tinted
     square, then its name. -->
<script>
  import Icon from './Icon.svelte';
  let { icon, label } = $props();
</script>

<h1 class="viewtitle"><span class="vicon"><Icon name={icon} size={18} /></span>{label}</h1>
