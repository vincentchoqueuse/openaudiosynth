// about.js — what a module of the sound does, in a few sentences: the About tab of the side panel. The effects, the
// modulators, the controllers and the macros by name; an instrument's stages by their role (each parameter then
// says what it does through its descriptor's help text).

const FX = {
  outfilter: 'A filter on the whole instrument, before the effects: it shapes the mix of every voice at once.',
  crush: 'Fewer bits and a lower sample rate: the grain and the aliasing of early digital machines.',
  phaser: 'Notches that sweep through the spectrum: copies of the sound shifted in phase and mixed back.',
  flanger: 'A copy of the sound a few milliseconds late, the delay swept: a comb that rises and falls, with feedback for a metallic edge.',
  chorus: 'Copies of the sound slightly late and moving in pitch: width and a gentle shimmer.',
  delay: 'Echoes of the sound, on the tempo or free, each fainter and darker than the last; ping-pong sends them from side to side.',
  reverb: 'A room or a hall around the sound; Freeze holds it forever, Shimmer adds a tail that climbs in pitch.',
  tape: 'A tape machine: slow wow and fast flutter in the pitch, the saturation and the dull top of tape, a little hiss.',
  master: 'The level of the whole sound, after every effect.',
  saturation: 'Gentle overdrive on the whole sound: warmth and density, louder parts rounded off.',
  limiter: 'Keeps the peaks under the ceiling, so nothing clips however loud the sound gets.',
};
const ROLE = {
  source: 'A source: it makes the sound that the stages after it shape.',
  filter: 'A filter: it takes away part of the spectrum, or brings out a band of it.',
  amp: 'The amplifier: how loud each note is over its life, from the key pressed to after it is let go.',
  mod: "One of the instrument's own modulators: it moves its settings over time, note by note.",
  voice: 'How notes become voices: how many play at once, glide, legato, and what happens when there are too many.',
};
const MOD = {
  mseg: 'An envelope of up to 16 points, each with its time, its level and its curve. It can hold on a point while a key is held, or loop. It moves any setting through its routes.',
  vmseg: 'An envelope of up to 16 points that starts again on every note: each note has its own. It moves pitch, level, pan, brightness or timbre through its routes.',
  mlfo: 'A slow oscillator for the whole sound, free in Hz or on the tempo. Its shape can be drawn from harmonics. It moves any setting through its routes.',
  vlfo: 'A slow oscillator that starts on every note: each note has its own. It moves pitch, level, pan, brightness or timbre through its routes.',
  wander: 'Eight slow random walks added to the eight macros: the sound drifts through what its macros reach, without anyone touching it.',
};
const CTL = {
  velocity: 'How hard the key was struck, 0 to 1.', note: 'Which key: low notes give less, high notes more (keyboard tracking).',
  random: 'A new random value on each note.', mod_wheel: 'The mod wheel of the keyboard (CC 1).', aftertouch: 'The pressure on the keys after they are down.',
  pitch_bend: 'The pitch bend wheel, -1 to 1.', pressure: 'The pressure on this note (MPE, or channel pressure).', slide: 'The slide of the finger along this key (MPE, CC 74).',
};
export function about(kind, row) {
  if (kind === 'stage') return FX[row.st?.id] ?? ROLE[row.st?.kind] ?? 'A part of the instrument.';
  if (kind === 'mod') return row.ctl ? CTL[row.src] ?? '' : MOD[row.key === 'wander' ? 'wander' : row.prefix.replace(/\d?_$/, '')] ?? '';
  if (kind === 'macro') return 'A macro moves several settings together, each between its value at 0 and its value at 1. It is played on the strip above the keyboard, by a MIDI CC, or by Wander.';
  if (kind === 'kit') return 'The kit of the sampler: 16 pads, each a sample with its own pitch, start and end, direction, filter and choke group.';
  return '';
}
