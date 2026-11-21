<!-- ProjectLinks.svelte — small icons at the bottom of the sidebar: Settings (once audio runs), the theme
     switch (auto / light / dark), the source code on GitHub and the author's site. -->
<script>
  import { REPO_URL, SITE_URL } from './links.js';
  import { theme } from './theme.svelte.js';
  import { session as s } from './session.svelte.js';
  import Icon from './Icon.svelte';
  const LABEL = { auto: 'Theme: as the system', light: 'Theme: light', dark: 'Theme: dark' };
</script>

<nav class="links" aria-label="Project">
  {#if s.started}
    <button class="theme" onclick={() => (s.settingsOpen = true)} title="Settings: MIDI channel, keys, MPE, tempo, tuning" aria-label="Settings"><Icon name="settings" weight={1.2} />
    </button>
  {/if}
  <button class="theme" onclick={theme.next} title="{LABEL[theme.mode]} (click to change)" aria-label={LABEL[theme.mode]}><Icon name={theme.mode} weight={1.3} />
  </button>
  <a href={REPO_URL} target="_blank" rel="noopener noreferrer" title="Source code on GitHub" aria-label="Source code on GitHub"><Icon name="github" />
  </a>
  <a href={SITE_URL} target="_blank" rel="noopener noreferrer" title="vchoqueuse.fr" aria-label="Author's website, vchoqueuse.fr"><Icon name="site" weight={1.3} />
  </a>
</nav>
