// meter.js — the level of what is heard, for the top bar's Out meter (session.meter): the peak of each frame of the
// output's AnalyserNode, held then falling 0.5 dB a frame. Runs once audio has started, whatever view is shown.
const DB_FLOOR = -90;

export function startMeter(session) {
  const a = session.synth.analyser;
  if (!a) return () => {};
  const time = new Float32Array(a.fftSize);
  let raf, held = -Infinity;
  const tick = () => {
    a.getFloatTimeDomainData(time);
    let pk = 0;
    for (const v of time) pk = Math.max(pk, Math.abs(v));
    const db = 20 * Math.log10(pk + 1e-9);
    held = Math.max(db, held - 0.5);
    session.meter = { level: Math.max(0, Math.min(1, (db - DB_FLOOR) / -DB_FLOOR)), hold: held };
    raf = requestAnimationFrame(tick);
  };
  tick();
  return () => cancelAnimationFrame(raf);
}
