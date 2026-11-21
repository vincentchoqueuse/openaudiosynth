<!-- ShareDialog.svelte — sharing the synth: a QR code of this page's address (to open it on a phone or pass it
     around a room), the address itself and a button to copy it. Closed with Escape or a click outside. -->
<script>
  import QRCode from 'qrcode';
  import { session as s } from './session.svelte.js';
  const url = location.origin + location.pathname;
  let svg = $state(''), copied = $state(false);
  QRCode.toString(url, { type: 'svg', margin: 1, errorCorrectionLevel: 'M', color: { dark: '#000000', light: '#ffffff' } })
    .then((t) => (svg = t));
  async function copy() {
    try { await navigator.clipboard.writeText(url); copied = true; setTimeout(() => (copied = false), 1500); } catch { /* no clipboard */ }
  }
</script>

<div class="scrim" role="presentation" onclick={(e) => e.target === e.currentTarget && (s.shareOpen = false)}>
  <div class="dialog share" role="dialog" aria-modal="true" aria-label="Share">
    <div class="dhead"><h2>Share</h2><button class="btn ghost" onclick={() => (s.shareOpen = false)}>Close</button></div>
    <div class="qr" aria-label="QR code of {url}">{@html svg}</div>
    <p class="mono url">{url}</p>
    <div class="row"><button class="btn" onclick={copy}>{copied ? 'Copied' : 'Copy link'}</button></div>
  </div>
</div>

<style>
  .share { width: min(340px, 92vw); }
  .dhead { display: flex; align-items: center; justify-content: space-between; }
  .dhead h2 { margin: 0; font-size: 15px; }
  .qr { width: 240px; height: 240px; margin: 4px auto; background: #fff; border-radius: 10px; padding: 8px; }
  .qr :global(svg) { display: block; width: 100%; height: 100%; }
  .url { margin: 0; font-size: 11.5px; color: var(--muted); text-align: center; word-break: break-all; }
  .row { justify-content: center; }
</style>
