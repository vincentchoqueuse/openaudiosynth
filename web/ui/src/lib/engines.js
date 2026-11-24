// engines.js — how the engines are presented: the order of the families and one color each, the same as on
// the board's screen (target/h745/app/m4/app_m4.c), so an instrument looks the same everywhere.
export const ENGINE_ORDER = ['va', 'mono', 'poly', 'acid', 'wt', 'fm6', 'modal', 'drums', 'sampler', 'coupled', 'selfosc', 'drone', 'havoc', 'micro'];

const COLORS = {
  va: '#5ab4e0', mono: '#e5a33d', fm6: '#9b8cff', modal: '#4fd1c5', poly: '#f2777a', acid: '#b8e05a',
  drums: '#e5534b', coupled: '#7fa7ff', selfosc: '#d98cff', wt: '#56d39a', sampler: '#f08fb8', drone: '#8fc9e8',
  micro: '#e8e2d0', havoc: '#d4ff3a',
};

export const engineColor = (name) => COLORS[name] ?? '#9aa5b1';

// rank of an engine in the presentation order (unknown ones last, by name)
export const engineRank = (name) => {
  const i = ENGINE_ORDER.indexOf(name);
  return i < 0 ? ENGINE_ORDER.length : i;
};

// Default macros of each instrument, as on the board's knobs (target/h745/app/knobs.c): six main parameters, then
// the delay and reverb sends (an entry may also be a scene: a name and several parameters). A sound without macros of its own gets them (session.defaultMacros).
export const DEFAULT_MACROS = {
  va: ['cutoff', 'resonance', 'fenv_amount', 'fenv_decay', 'amp_attack', 'amp_release'],
  acid: ['cutoff', 'resonance', 'env_mod', 'decay', 'accent', 'distortion'],
  mono: ['cutoff', 'emphasis', 'contour', 'fenv_decay', 'mixer_drive', 'glide'],
  poly: ['cutoff', 'resonance', 'fenv_amount', 'fenv_decay', 'amp_release', 'lfo_filter'],
  // scenes: each macro moves several settings, upwards from where they are ([name, [parameter, travel]...], the
  // travel in normalized units)
  havoc: [['Rot', ['g1_rot', 0.7], ['g2_rot', 0.7], ['g3_rot', 0.7], ['g4_rot', 0.7]],
          ['Motion', ['g1_lfo_a', 0.25], ['g2_lfo_a', 0.25], ['g3_lfo_a', 0.25], ['g4_lfo_a', 0.25],
                     ['g1_lfo_b', 0.25], ['g2_lfo_b', 0.25], ['g3_lfo_b', 0.25], ['g4_lfo_b', 0.25]],
          ['Chaos', ['feedback', 0.8], ['mutate', 0.8]],
          ['Density', ['g1_life_rate', 0.4], ['g2_life_rate', 0.4], ['g3_life_rate', 0.4], ['g4_life_rate', 0.4]],
          ['Dark', ['dark', 1]],
          ['Space', ['reverb_mix', 0.6]]],
  fm6: ['op2_level', 'op4_level', 'op6_level', 'feedback', 'lfo_speed', 'lfo_pmd'],
  wt: ['a_position', 'b_position', 'a_warp', 'cutoff', 'resonance', 'amp_release'],
  modal: ['brightness', 'position', 'hardness', 'damping', 'decay', 'inharmonicity'],
  drums: ['drive', 'kick_tune', 'kick_decay', 'snare_tone', 'snare_decay', 'chat_decay'],
  coupled: ['amp_coupling', 'phase_coupling', 'stretch', 'tilt', 'drift', 'amp_release'],
  selfosc: ['drive', 'stiffness', 'entrain', 'cutoff', 'resonance', 'amp_release'],
  sampler: ['tune', 'resonance', 'release', 'pad1_decay', 'pad2_decay', 'pad3_decay'],
  drone: ['temperature', 'density', 'motion', 'depth', 'brightness', 'sparkle'],
  micro: ['freq', 'pitch_amount', 'pitch_time', 'amp_decay', 'drive', 'click_level'],
};
export const DEFAULT_MACRO_SENDS = ['delay_mix', 'reverb_mix'];
