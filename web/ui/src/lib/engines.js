// engines.js — how the engines are presented: the order of the families and one color each, the same as on
// the board's screen (target/h745/app/m4/app_m4.c), so an instrument looks the same everywhere.
export const ENGINE_ORDER = ['va', 'mono', 'wt', 'fm6', 'modal', 'drums', 'sampler', 'drone', 'micro'];

const COLORS = {
  va: '#5ab4e0', mono: '#e5a33d', fm6: '#9b8cff', modal: '#4fd1c5',
  drums: '#e5534b', wt: '#56d39a', sampler: '#f08fb8', drone: '#8fc9e8',
  micro: '#e8e2d0',
};

export const engineColor = (name) => COLORS[name] ?? '#9aa5b1';

// rank of an engine in the presentation order (unknown ones last, by name)
export const engineRank = (name) => {
  const i = ENGINE_ORDER.indexOf(name);
  return i < 0 ? ENGINE_ORDER.length : i;
};

// Default macros of each instrument, as on the board's knobs (target/h745/app/knobs.c): six main parameters, then
// the delay and reverb sends (an entry may also be a scene: a name and several parameters). A sound without macros of its own gets them (session.defaultMacros).
// A new sound is bare (session.newSound): beyond the switches turned off, the settings of an instrument that would still
// move or add to its first source at their defaults: a second oscillator in a fixed mixer, the filter envelope, the
// mod wheel's vibrato, the analog drift, the voices' spread, the motion of a drone.
export const BARE = {
  va: { osc2_level: 0, fenv_amount: 0, wheel_vibrato: 0, drift: 0, tolerance: 0, jitter: 0, pan_spread: 0, lfo2_spread: 0 },
  mono: { osc2_level: 0, mod_pitch: 0, drift: 0, tolerance: 0, jitter: 0 },
  fm6: { pan_spread: 0 },
  wt: { pan_spread: 0 },
  modal: { pan_spread: 0 },
  drone: { motion: 0, depth: 0 },
};

export const DEFAULT_MACROS = {
  va: ['cutoff', 'resonance', 'fenv_amount', 'fenv_decay', 'amp_attack', 'amp_release'],
  mono: ['cutoff', 'emphasis', 'contour', 'fenv_decay', 'mixer_drive', 'glide'],
  fm6: ['op2_level', 'op4_level', 'op6_level', 'feedback', 'lfo_speed', 'lfo_pmd'],
  wt: ['a_position', 'b_position', 'a_warp', 'cutoff', 'resonance', 'amp_release'],
  modal: ['brightness', 'position', 'hardness', 'damping', 'decay', 'inharmonicity'],
  drums: ['drive', 'kick_tune', 'kick_decay', 'snare_tone', 'snare_decay', 'chat_decay'],
  sampler: ['tune', 'resonance', 'release', 'pad1_decay', 'pad2_decay', 'pad3_decay'],
  drone: ['temperature', 'density', 'motion', 'depth', 'brightness', 'sparkle'],
  micro: ['freq', 'pitch_amount', 'pitch_time', 'amp_decay', 'drive', 'click_level'],
};
export const DEFAULT_MACRO_SENDS = ['delay_mix', 'reverb_mix'];
