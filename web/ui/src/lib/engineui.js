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

// a place from two lists of tests on the name (strings, or regular expressions): hidden, then folded under More
const listed = (list, name) => list.some((x) => (typeof x === 'string' ? x === name : x.test(name)));
const fold = (more, hide) => (d, s) => (hide?.(d.name, s) ? 'hide' : listed(more, d.name) ? 'more' : 'main');

export const ENGINE_UI = {
  // va: the two oscillators, the filters, the envelopes and the LFO in view; fine tuning, routing and the analog
  // imperfections under More; what the chosen waves and filters do not use is hidden
  va: {
    place: fold(
      [/^osc\d_semi$/, 'supersaw_motion', 'supersaw_motion_rate', 'keytrack', 'vel_cutoff', 'oversample', 'filter2_character',
       'filter_routing', 'amp_velocity', 'pan_spread', 'lfo_pwm', 'wheel_vibrato', /^lfo[234]_/, 'tolerance', 'drift_time',
       'jitter', 'glide_mode', 'bend_range'],
      (n, s) => {
        let m = /^osc(\d)_pw$/.exec(n);
        if (m) return choice(s, `osc${m[1]}_wave`) !== 'pulse';
        if (n.startsWith('supersaw_')) return choice(s, 'osc1_wave') !== 'supersaw' && choice(s, 'osc2_wave') !== 'supersaw';
        if (n === 'filter_character') return !['formant', 'scream'].includes(choice(s, 'filter_mode'));
        if (n === 'filter2_character') return !['formant', 'scream'].includes(choice(s, 'filter2_mode'));
        if (n === 'lfo_pwm') return choice(s, 'osc1_wave') !== 'pulse' && choice(s, 'osc2_wave') !== 'pulse';
        return false;
      }),
  },
  // wt: table, position, warp and level of each oscillator in view; the matrix shows its slots in use and one free
  wt: {
    place: fold(
      [/^[ab]_(semi|fine|unison|detune)$/, 'sub_route', 'keytrack', 'oversample', 'dist_tone', 'dist_place', 'velocity',
       'pan_spread', /^lfo\d_retrig$/, 'bend_range'],
      (n, s) => {
        let m = /^([ab])_warp$/.exec(n);
        if (m) return choice(s, `${m[1]}_warp_mode`) === 'off';
        m = /^([ab])_detune$/.exec(n);
        if (m) return s.getP(`${m[1]}_unison`) <= 1;
        m = /^mod(\d)_(source|destination|amount|via)$/.exec(n);
        if (m) {
          const used = (k) => choice(s, `mod${k}_source`) !== 'none';
          if (m[2] !== 'source') return !used(m[1]);
          for (let k = 1; k < +m[1]; k++) if (!used(k)) return !used(m[1]); // a free slot before it: only one is shown
        }
        return false;
      }),
  },
  // micro: each layer's level and its two or three shaping controls in view; curves, phases and patterns under More;
  // what the chosen modes do not use is hidden
  micro: {
    place: fold(
      ['fine', 'phase', 'phase_reset', 'burst', 'osc2_split', 'osc2_pitch_env', 'noise_q', 'noise_gate', 'noise_pan',
       'low_phase', 'low_drop_time', 'low_attack', 'low_hold', 'low_curve', 'low_drive', 'low_gate', 'low_pattern', 'low_beat',
       'low_beat_mix', 'amp_hold', 'attack_curve', 'decay_curve', 'velocity', 'pan', 'pitch_curve', 'gate_depth', 'gate_edge',
       'noise_pattern', 'bend_range'],
      (n, s) => {
        if (n === 'freq') return choice(s, 'pitch_mode') !== 'fixed';
        if (n === 'osc2_ratio') return choice(s, 'osc2_mode') !== 'ratio';
        if (n === 'osc2_offset') return choice(s, 'osc2_mode') !== 'hz';
        if (n === 'noise_decay') return choice(s, 'noise_env') === 'amp';
        if (n === 'noise_rate') return choice(s, 'noise_type') === 'white';
        if (n === 'noise_pattern') return choice(s, 'noise_gate') !== 'pattern';
        if (n === 'low_pattern') return choice(s, 'low_gate') !== 'pattern';
        if (n === 'low_beat_mix') return s.getP('low_beat') === 0;
        if (n === 'gate_div') return choice(s, 'gate_mode') !== 'sync';
        if (n === 'gate_hz') return choice(s, 'gate_mode') !== 'free';
        if (/^gate_(steps|pattern|depth|edge)$/.test(n)) return choice(s, 'gate_mode') === 'off';
        if (/^click_(level|tone|decay)$/.test(n)) return choice(s, 'click_mode') === 'off';
        return false;
      }),
  },
  // fm6: of each operator its level, its frequency and its velocity; of the voice the algorithm, the feedback and
  // the LFO depths; the rate / level envelopes, the keyboard scaling and the pitch envelope under More
  fm6: {
    place: (d) => (/^op\d_(level|coarse|fine|detune|velocity)$/.test(d.name) ||
      ['algorithm', 'feedback', 'volume', 'lfo_speed', 'lfo_pmd', 'lfo_amd'].includes(d.name) ? 'main' : 'more'),
  },
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
