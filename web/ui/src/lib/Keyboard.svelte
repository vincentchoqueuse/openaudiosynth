<!-- Keyboard.svelte — slim two-octave keyboard plus the computer keyboard (physical key positions, so it
     works in AZERTY and QWERTY): bottom letter row from C, top row an octave up; arrow keys shift octaves. -->
<script>
  import { keyIsForControl } from './keys.js';
  let { synth, held = new Set() } = $props();
  let octave = $state(3);
  let down = $state(new Set());
  const ROW1 = ['KeyZ','KeyS','KeyX','KeyD','KeyC','KeyV','KeyG','KeyB','KeyH','KeyN','KeyJ','KeyM','Comma'];
  const ROW2 = ['KeyQ','Digit2','KeyW','Digit3','KeyE','KeyR','Digit5','KeyT','Digit6','KeyY','Digit7','KeyU','KeyI'];
  const keys = Array.from({ length: 25 }, (_, i) => ({ i, black: [1, 3, 6, 8, 10].includes(i % 12) }));
  const whites = keys.filter((k) => !k.black);
  const base = $derived(12 * (octave + 1));

  function on(n) { if (n <= 127 && !down.has(n)) { down.add(n); down = new Set(down); synth.noteOn(n, 100); } }
  function off(n) { if (down.has(n)) { down.delete(n); down = new Set(down); synth.noteOff(n); } }
  // changing octave releases what is held: the key-up to come would be computed in the new octave
  function setOctave(o) { [...down].forEach(off); octave = Math.min(7, Math.max(0, o)); }
  function keyNote(code) {
    let k = ROW1.indexOf(code);
    if (k >= 0) return base + k;
    k = ROW2.indexOf(code);
    return k >= 0 ? base + 12 + k : -1;
  }
  function kd(e) {
    if (keyIsForControl(e) || e.repeat || e.metaKey || e.ctrlKey) return;
    if (e.code === 'ArrowLeft') { setOctave(octave - 1); e.preventDefault(); return; }
    if (e.code === 'ArrowRight') { setOctave(octave + 1); e.preventDefault(); return; }
    const n = keyNote(e.code);
    if (n >= 0) { on(n); e.preventDefault(); }
  }
  function ku(e) { const n = keyNote(e.code); if (n >= 0) off(n); }
  const lit = (n) => down.has(n) || held.has(n);
  const blackX = (i) => (whites.filter((w) => w.i < i).length * 100) / whites.length;
  function press(e, n) { e.currentTarget.setPointerCapture(e.pointerId); on(n); }
</script>

<svelte:window onkeydown={kd} onkeyup={ku} onblur={() => down.forEach(off)} />
<div class="keys-wrap">
  <div class="oct">
    <button aria-label="Octave down" onclick={() => setOctave(octave - 1)}>−</button>
    <span>C{octave}</span>
    <button aria-label="Octave up" onclick={() => setOctave(octave + 1)}>+</button>
  </div>
  <div class="keys">
    {#each whites as k}
      <div class="w" class:lit={lit(base + k.i)} role="button" tabindex="-1" aria-label="Note {base + k.i}" onpointerdown={(e) => press(e, base + k.i)}
           onpointerup={() => off(base + k.i)} onpointercancel={() => off(base + k.i)}></div>
    {/each}
    {#each keys.filter((k) => k.black) as k}
      <div class="b" class:lit={lit(base + k.i)} role="button" tabindex="-1" aria-label="Note {base + k.i}" style="left: calc({blackX(k.i)}% - 1.3%)"
           onpointerdown={(e) => press(e, base + k.i)} onpointerup={() => off(base + k.i)}
           onpointercancel={() => off(base + k.i)}></div>
    {/each}
  </div>
</div>
