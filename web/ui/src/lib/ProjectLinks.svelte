<!-- ProjectLinks.svelte — small icons at the bottom of the sidebar: Settings (once audio runs), the theme
     switch (auto / light / dark), the source code on GitHub and the author's site. -->
<script>
  import { REPO_URL, SITE_URL } from './links.js';
  import { theme } from './theme.svelte.js';
  import { session as s } from './session.svelte.js';
  const LABEL = { auto: 'Theme: as the system', light: 'Theme: light', dark: 'Theme: dark' };
</script>

<nav class="links" aria-label="Project">
  {#if s.started}
    <button class="theme" onclick={() => (s.settingsOpen = true)} title="Settings: MIDI channel, keys, MPE, tempo, tuning" aria-label="Settings">
      <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="2.3"/><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4"/></g></svg>
    </button>
  {/if}
  <button class="theme" onclick={theme.next} title="{LABEL[theme.mode]} (click to change)" aria-label={LABEL[theme.mode]}>
    <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
      {#if theme.mode === 'light'}
        <g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="3.2"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.4 1.4M11.6 11.6 13 13M3 13l1.4-1.4M11.6 4.4 13 3"/></g>
      {:else if theme.mode === 'dark'}
        <path fill="currentColor" d="M13.5 10.2A6 6 0 0 1 5.8 2.5a6 6 0 1 0 7.7 7.7z"/>
      {:else}
        <g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="6.2"/></g><path fill="currentColor" d="M8 1.8a6.2 6.2 0 0 1 0 12.4z"/>
      {/if}
    </svg>
  </button>
  <a href={REPO_URL} target="_blank" rel="noopener noreferrer" title="Source code on GitHub" aria-label="Source code on GitHub">
    <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/></svg>
  </a>
  <a href={SITE_URL} target="_blank" rel="noopener noreferrer" title="vchoqueuse.fr" aria-label="Author's website, vchoqueuse.fr">
    <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.3"><circle cx="8" cy="8" r="6.6"/><ellipse cx="8" cy="8" rx="2.9" ry="6.6"/><path d="M1.6 5.8h12.8M1.6 10.2h12.8"/></g></svg>
  </a>
</nav>
