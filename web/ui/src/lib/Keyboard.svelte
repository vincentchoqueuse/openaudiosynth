<!-- Keyboard.svelte — slim two-octave keyboard plus the computer keyboard (physical key positions, so it
     works in AZERTY and QWERTY): bottom letter row from C, top row an octave up; arrow keys shift octaves. Hold keeps
     the notes played until they are played again or Hold goes off (a drone under one hand, the macros under the other).
     The computer's keys play only while `active` (the Live player shown); folding it releases the keys down. -->
<script>
  import { keyIsForControl } from './keys.js';
  import { session } from './session.svelte.js';
  let { synth, held = new Set(), active = true } = $props();
  let octave = $state(3);
  let down = $state(new Set());
  let hold = $state(false), latched = $state(new Set()); // Hold: the notes kept after their key is released
  const ROW1 = ['KeyZ','KeyS','KeyX','KeyD','KeyC','KeyV','KeyG','KeyB','KeyH','KeyN','KeyJ','KeyM','Comma'];
  const ROW2 = ['KeyQ','Digit2','KeyW','Digit3','KeyE','KeyR','Digit5','KeyT','Digit6','KeyY','Digit7','KeyU','KeyI'];
  const keys = Array.from({ length: 25 }, (_, i) => ({ i, black: [1, 3, 6, 8, 10].includes(i % 12) }));
  const whites = keys.filter((k) => !k.black);
  const base = $derived(12 * (octave + 1));

  function on(n) {
    if (n > 127 || down.has(n)) return;
    down.add(n); down = new Set(down);
    if (latched.has(n)) { latched.delete(n); latched = new Set(latched); synth.noteOff(n); return; } // played again: released
    synth.noteOn(n, 100);
    if (hold) { latched.add(n); latched = new Set(latched); }
  }
  function off(n) {
    if (!down.has(n)) return;
    down.delete(n); down = new Set(down);
    if (!latched.has(n) && !hold) synth.noteOff(n);
  }
  function setHold(v) { hold = v; if (!v) { latched.forEach((n) => !down.has(n) && synth.noteOff(n)); latched = new Set(); } }
  // changing octave releases what is held: the key-up to come would be computed in the new octave
  function setOctave(o) { [...down].forEach(off); octave = Math.min(7, Math.max(0, o)); } // held notes stay
  function keyNote(code) {
    let k = ROW1.indexOf(code);
    if (k >= 0) return base + k;
    k = ROW2.indexOf(code);
    return k >= 0 ? base + 12 + k : -1;
  }
  function kd(e) {
    if (!active || keyIsForControl(e) || e.repeat || e.metaKey || e.ctrlKey) return;
    if (e.code.startsWith('Arrow') && e.target?.closest?.('.roll')) return; // the piano roll moves its notes
    if (e.code === 'ArrowLeft') { setOctave(octave - 1); e.preventDefault(); return; }
    if (e.code === 'ArrowRight') { setOctave(octave + 1); e.preventDefault(); return; }
    const n = keyNote(e.code);
    if (n >= 0) { on(n); e.preventDefault(); }
  }
  function ku(e) { const n = keyNote(e.code); if (n >= 0) off(n); }
  $effect(() => { if (!active) [...down].forEach(off); }); // folded: what the keys held is let go
  const lit = (n) => down.has(n) || latched.has(n) || held.has(n);
  $effect(() => { session.played = new Set([...down, ...latched]); }); // shown on the piano roll
  const blackX = (i) => (whites.filter((w) => w.i < i).length * 100) / whites.length;
  function press(e, n) { e.currentTarget.setPointerCapture(e.pointerId); on(n); }
</script>

<svelte:window onkeydown={kd} onkeyup={ku} onblur={() => down.forEach(off)} />
<div class="keys-wrap">
  <div class="oct">
    <button aria-label="Octave down" onclick={() => setOctave(octave - 1)}>−</button>
    <span>C{octave}</span>
    <button aria-label="Octave up" onclick={() => setOctave(octave + 1)}>+</button>
    <button class="hold" class:on={hold} aria-pressed={hold} onclick={() => setHold(!hold)} title="Hold: notes stay until played again">Hold</button>
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
