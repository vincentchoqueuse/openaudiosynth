<!-- SettingsDialog.svelte — the part's settings, apart from the sound's design: MIDI channel, key range,
     transposition, level and pan in the mix, MPE, and the global tempo, clock and tuning (temperament, concert
     pitch). Opened from the gear in the sidebar; Escape or a click outside closes it. -->
<script>
  import ParamGroups from './ParamGroups.svelte';
  import { session as s } from './session.svelte.js';
</script>

<div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.settingsOpen = false)}>
  <div class="dialog wide" role="dialog" aria-modal="true" aria-label="Settings">
    <div class="dhead"><h2>Settings</h2><button class="btn ghost" onclick={() => (s.settingsOpen = false)}>Close</button></div>
    <ParamGroups tab="settings" />
  </div>
</div>

<style>
  .wide { width: min(960px, 94vw); max-height: 86vh; overflow-y: auto; }
  .dhead { display: flex; align-items: center; justify-content: space-between; }
  .dhead h2 { margin: 0; }
</style>
