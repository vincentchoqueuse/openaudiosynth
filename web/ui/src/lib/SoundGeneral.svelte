<!-- SoundGeneral.svelte — the General tab of the Synth view: the sound as a record (FieldTable): its name, its
     description, its instrument and what that is, how many voices, what is in use (the effects, the sources of
     modulation, the macros), then its signal path drawn (ChainView: only what is active; a click on a block opens it). -->
<script>
  import FieldTable from './FieldTable.svelte';
  import ChainView from './ChainView.svelte';
  import { session as s } from './session.svelte.js';
  import { modRows } from './mods.js';
  import { fxStages, stageOn } from './stages.js';
  import { isOn } from './onoff.js';
  let { onpick } = $props();

  const fields = $derived.by(() => {
    const fx = fxStages(s).filter((r) => r.st?.enable && stageOn(s, r.st)).map((r) => r.title);
    const mods = modRows().filter((m) => isOn(s, { ...m, kind: 'mod' })).map((m) => m.label);
    const macros = s.macroNames.map((n, k) => (n || s.macroMaps.some((m) => m.macro === k && m.dst) ? s.macroName(k) : '')).filter(Boolean);
    return [
      ['Name', s.label],
      ['Description', s.description || '—'],
      ['Instrument', s.engine ? `${s.engine.name} — ${s.engine.description ?? ''}` : '—'],
      ...(s.engine?.voices ? [['Voices', String(s.engine.voices)]] : []),
      ...(s.engine?.name === 'sampler' ? [['Kit', `${s.kitName || 'dropped files'} · ${s.kit?.entries?.length ?? 0} samples`]] : []),
      ['Effects', fx.join(', ') || 'none'],
      ['Modulation', mods.join(', ') || 'none'],
      ['Macros', macros.join(', ') || 'none'],
      ['Settings moved', s.changedCount ? `${s.changedCount} since it was loaded` : 'none'],
    ];
  });
</script>

<FieldTable label="The sound" {fields}><ChainView {onpick} /></FieldTable>
