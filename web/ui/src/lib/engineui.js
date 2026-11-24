// engineui.js — what the generic parameter cards cannot guess about an instrument: the name of a control whose
// meaning depends on another setting, and which controls are the main ones of a card (the others fold under
// "More"). One entry per engine that needs it; an engine without entry shows every parameter under its own name.

import { human } from './naming.js';

// havoc: the two shape controls of a generator by its process, the two controls of a processor by its type
const GEN = {
  tone: ['Feedback', 'Detune'], logistic: ['Chaos', 'Smooth'], byte: ['Formula', 'Constant'], dust: ['Tune', 'Ring'],
  wind: ['Whistle', 'Gusts'], pulsar: ['Formant', 'Length'], cell: ['Rule', 'Smooth'], swarm: ['Spread', 'Speed'],
  computer: ['Range', 'Speed'], rumble: ['Fold', 'Crackle'], bleep: ['Range', 'Speed'], glitch: ['Noise', 'Speed'],
  talk: ['Speed', 'Whisper'], modem: ['Baud', 'Tones'], neuron: ['Bursts', 'Current'], alias: ['Fold', 'Steps'],
};
const PROC = {
  lp: ['Cutoff', 'Resonance'], bp: ['Cutoff', 'Resonance'], hp: ['Cutoff', 'Resonance'], comb: ['Pitch', 'Feedback'],
  fold: ['Drive', 'Asymmetry'], crush: ['Rate', 'Bits'], ring: ['Frequency', 'Stereo'], chop: ['Rate', 'Open'],
  stutter: ['Length', 'Stuck'], sub: ['Octave', 'Buzz'], warp: ['Frequency', 'Depth'], mangle: ['Operation', 'Amount'],
};
// the label of a choice parameter's present value ("tone", "lp"...)
const choice = (s, name) => { const d = s.byName[name]; return d?.labels?.[Math.round(s.getP(name) - d.min)]; };

export const ENGINE_UI = {
  havoc: {
    // the label of a parameter in its card (undefined: the generic one)
    label(d, s) {
      let m = /^g(\d)_(a|b|lfo_rate|lfo_a|lfo_b|life_rate|life_shape)$/.exec(d.name);
      if (m) {
        const [a, b] = GEN[choice(s, `g${m[1]}_type`)] ?? ['A', 'B'];
        const life = choice(s, `g${m[1]}_life`);
        return { a, b, lfo_rate: 'Motion rate', lfo_a: `Motion → ${a.toLowerCase()}`, lfo_b: `Motion → ${b.toLowerCase()}`,
                 life_rate: 'Life rate', life_shape: { bursts: 'Time on', pulse: 'Length', swell: 'Depth' }[life] ?? 'Life shape' }[m[2]];
      }
      m = /^x(\d)_(p1|p2)$/.exec(d.name);
      if (m) return (PROC[choice(s, `x${m[1]}_type`)] ?? ['P1', 'P2'])[m[2] === 'p1' ? 0 : 1];
      // the rest of a generator's or a processor's card: without its number, which the card's title says
      m = /^[gx]\d_(.+)$/.exec(d.name);
      if (m) return d.name[0] === 'g' && m[1] === 'type' ? 'Process' : m[1] === 'fm' ? 'FM' : human(m[1]);
      return undefined;
    },
    // where a parameter goes in its card: 'main', 'more' (folded) or 'hide' (it does nothing as things are set)
    place(d, s) {
      let m = /^g(\d)_(.+)$/.exec(d.name);
      if (m) {
        const g = +m[1], k = m[2], next = (g % 4) + 1;
        // a generator at level 0 that modulates nobody: only its process and its level
        const silent = s.getP(`g${g}_level`) <= 0 && s.getP(`g${next}_fm`) <= 0 && s.getP(`g${next}_ring`) <= 0;
        if (k === 'type' || k === 'level') return 'main';
        if (silent) return 'hide';
        if (k === 'pitch' || k === 'a' || k === 'b' || k === 'life') return 'main';
        if (k === 'life_rate' || k === 'life_shape') return choice(s, `g${g}_life`) === 'steady' ? 'hide' : 'main';
        return 'more';
      }
      m = /^x(\d)_(.+)$/.exec(d.name);
      if (m && m[2] !== 'type') return choice(s, `x${m[1]}_type`) === 'off' ? 'hide' : 'main';
      if (d.name === 'mutate_depth') return s.getP('mutate') > 0 ? 'main' : 'hide';
      if (['velocity', 'width', 'bend_range'].includes(d.name)) return 'more';
      return 'main';
    },
  },
};
