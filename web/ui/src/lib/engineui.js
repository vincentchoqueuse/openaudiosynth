// engineui.js — what the generic parameter cards cannot guess about an instrument: the name of a control whose
// meaning depends on another setting, and which controls are the main ones of a card (the others fold under
// "More"). One entry per engine that needs it; an engine without entry shows every parameter under its own name.

import { human } from './naming.js';

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

// masks kept as numbers, drawn as named toggles (Param.svelte): micro's gate LFOs open, on their open steps
// (open) or their closed ones (open_not), the layers and events of these bits
export const MASKS = {
  '^pulse\\d_open(_not)?$': ['main', 'sine 2', 'noise', 'low', 'ortho', 'low hit', 'noise env'],
};

export const ENGINE_UI = {
  // va: the two oscillators, the filters, the envelopes and the LFO in view; fine tuning, routing and the analog
  // imperfections under More; what the chosen waves and filters do not use is hidden
  va: {
    place: fold(
      [/^osc\d_semi$/, 'unison_motion', 'unison_motion_rate', /^osc\d_unison_blend$/, 'keytrack', 'vel_cutoff', 'oversample', 'filter2_character',
       'filter_routing', 'amp_velocity', 'pan_spread', 'lfo_pwm', 'wheel_vibrato', /^lfo[234]_/, 'tolerance', 'drift_time',
       'jitter', 'glide_mode', 'bend_range'],
      (n, s) => {
        let m = /^osc(\d)_pw$/.exec(n);
        if (m) return choice(s, `osc${m[1]}_wave`) !== 'pulse';
        if (/^osc\d_unison_/.test(n)) return s.getP(n.slice(0, 4) + '_unison') < 2; // a unison of one voice: nothing to set
        if (n.startsWith('unison_motion')) return s.getP('osc1_unison') < 2 && s.getP('osc2_unison') < 2;
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
      ['fine', 'phase', 'phase_reset', 'burst', 'osc2_split', 'osc2_pitch_env', 'noise_q', 'noise_pan',
       'low_phase', 'low_drop_time', 'low_attack', 'low_hold', 'low_curve', 'low_drive', 'low_beat',
       'low_beat_mix', 'amp_hold', 'attack_curve', 'decay_curve', 'velocity', 'pan', 'pitch_curve',
       'bend_range', /^pulse\d_(offset|edge|depth)$/],
      (n, s) => {
        if (n === 'freq') return choice(s, 'pitch_mode') !== 'fixed';
        if (n === 'ortho_mode') return s.getP('ortho') === 0; // a pure sine: no kind of noise to choose
        if (n === 'osc2_ratio') return choice(s, 'osc2_mode') !== 'ratio';
        if (n === 'osc2_offset') return choice(s, 'osc2_mode') !== 'hz';
        if (n === 'noise_decay') return choice(s, 'noise_env') === 'amp';
        if (n === 'noise_rate') return choice(s, 'noise_type') === 'white';
        if (n === 'low_beat_mix') return s.getP('low_beat') === 0;
        if (/^click_(level|tone|decay)$/.test(n)) return choice(s, 'click_mode') === 'off';
        let m = /^pulse(\d)_(div|hz)$/.exec(n); // a pulse shows its step length on the beat, or its rate when free
        if (m) return choice(s, `pulse${m[1]}_mode`) !== (m[2] === 'div' ? 'sync' : 'free');
        return false;
      }),
  },
  // fm6: of each operator its level, its frequency and its velocity; of the voice the algorithm, the feedback and
  // the LFO depths; the rate / level envelopes, the keyboard scaling and the pitch envelope under More
  fm6: {
    place: (d) => (/^op\d_(level|coarse|fine|detune|velocity)$/.test(d.name) ||
      ['algorithm', 'feedback', 'volume', 'lfo_speed', 'lfo_pmd', 'lfo_amd'].includes(d.name) ? 'main' : 'more'),
  },
};
