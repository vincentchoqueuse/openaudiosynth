<!-- Meter.svelte — a labelled bar meter of the top bar: the CPU load and the output peak. `value` 0..1 fills the
     bar, `text` is read after it, `hot` turns both red. -->
<script>
  let { label, value, text, hot = false, title = '' } = $props();
</script>

<div class="meter" class:hot {title}>
  <span class="mlabel">{label}</span>
  <div class="track"><div class="fill" style="width:{Math.min(1, Math.max(0, value)) * 100}%"></div></div>
  <span class="mono mval">{text}</span>
</div>
