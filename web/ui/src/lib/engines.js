// engines.js — how the engines are presented: the order of the families and one color each, the same as on
// the board's screen (target/h745/app/m4/app_m4.c), so an instrument looks the same everywhere.
export const ENGINE_ORDER = ['va', 'mono', 'poly', 'acid', 'wt', 'fm6', 'modal', 'drums', 'sampler', 'coupled', 'selfosc', 'sine'];

const COLORS = {
  va: '#5ab4e0', mono: '#e5a33d', fm6: '#9b8cff', modal: '#4fd1c5', poly: '#f2777a', acid: '#b8e05a',
  drums: '#e5534b', coupled: '#7fa7ff', selfosc: '#d98cff', wt: '#56d39a', sine: '#9aa5b1', sampler: '#f08fb8',
};

export const engineColor = (name) => COLORS[name] ?? '#9aa5b1';

// rank of an engine in the presentation order (unknown ones last, by name)
export const engineRank = (name) => {
  const i = ENGINE_ORDER.indexOf(name);
  return i < 0 ? ENGINE_ORDER.length : i;
};
