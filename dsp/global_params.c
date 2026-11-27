/* global_params.c — the descriptor tables of the synth's own parameters (see global_params.h): the master level,
 * the tempo and tuning, the output filter and crush of every part, the saturation and limiter of the master, the
 * mixer strip of a part; the effect bus's are in fx/rack.c and a part's modulation in mod/partmod.c. Also the
 * effect chain as stages and the CCs the synth handles itself, for UIs and catalogs. synth.c reads them through
 * synth_global_param and synth_part_param_info. */
#include "global_params.h"
#include "fx/rack.h"
#include "mod/partmod.h"
#include "mod/tempo.h"

const engine_stage_t synth_fx_stages[] = {
    {"outfilter", "Output filter", "fx", "out_", "engine", NULL, "out_filter", 0.0f, 1.0f},
    {"crush", "Crush", "fx", "crush_", "outfilter", NULL, "crush_bits", 24.0f, 8.0f},
    {"phaser", "Phaser", "fx", "phaser_", "crush", NULL, "phaser_mix", 0.0f, 0.6f},
    {"flanger", "Flanger", "fx", "flanger_", "phaser", NULL, "flanger_mix", 0.0f, 0.6f},
    {"chorus", "Chorus", "fx", "chorus_", "flanger", NULL, "chorus_mode", 0.0f, 1.0f},
    {"delay", "Delay", "fx", "delay_", "chorus", NULL, "delay_mix", 0.0f, 0.25f},
    {"reverb", "Reverb", "fx", "reverb_", "delay", NULL, "reverb_mix", 0.0f, 0.3f},
    {"tape", "Tape", "fx", "tape tape_", "reverb", NULL, "tape", 0.0f, 1.0f},
    {"gain", "Gain", "fx", "gain_", "tape", NULL, "gain_level", 0.0f, 6.0f},
    {"master", "Master", "fx", "master", "gain", NULL, NULL, 0.0f, 0.0f},
    {"saturation", "Saturation", "fx", "saturation saturation_", "master", NULL, "saturation", 0.0f, 1.0f},
    {"limiter", "Limiter", "fx", "limiter limiter_", "saturation", NULL, "limiter", 0.0f, 1.0f},
};
const int synth_fx_stage_count = (int)(sizeof synth_fx_stages / sizeof synth_fx_stages[0]);

static const param_desc_t master_desc =
    PARAM(SYNTH_PARAM_MASTER_DB, "master", "master.level", -60.0f, 6.0f, 0.0f, PARAM_DB, "dB", 0, NULL,
          "master output level (after the effects, before the soft clipper)");

/* Documentation of the CCs handled here (handle_midi below implements them). */
const synth_cc_doc_t synth_midi_cc[] = {
    {1, "mod_wheel", "engine modulation (va: vibrato depth)"},
    {64, "sustain", "sustain pedal (>= 64 on)"},
    {69, "reverb_freeze", "freeze the reverb tail (>= 64 on)"},
    {91, "reverb_mix", "reverb level"},
    {93, "chorus_mix", "chorus level (switches the chorus on if it was off)"},
    {94, "delay_mix", "tape delay level"},
    {120, "all_sound_off", "silence every voice (3 ms fade)"},
    {123, "all_notes_off", "release every note"},
};
const int synth_midi_cc_count = (int)(sizeof synth_midi_cc / sizeof synth_midi_cc[0]);

static const char *const clock_labels[] = {"internal", "midi", NULL};
static const char *const tuning_labels[] = {"equal", "just", "pythagorean", "meantone", "werckmeister", "custom", NULL};
static const char *const root_labels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
#define TUNE(K) PARAM(SYNTH_PARAM_TUNE_0 + K, "tune_" #K, "tuning.degree" #K ".offset", -100.0f, 100.0f, 0.0f, PARAM_LIN, "cent", 0, \
                      NULL, "custom tuning: offset of degree " #K " above the root")
static const param_desc_t tempo_desc[] = {
    PARAM(SYNTH_PARAM_TEMPO, "tempo", "tempo.bpm", 20.0f, 300.0f, 120.0f, PARAM_LIN, "BPM", 1, NULL,
          "tempo of the internal clock (synced LFOs, MSEGs and delay); ignored while following MIDI clock"),
    PARAM(SYNTH_PARAM_CLOCK, "clock", "tempo.clock", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, clock_labels,
          "internal = the tempo parameter; midi = follow the MIDI clock of a sequencer or drum machine"),
    PARAM(SYNTH_PARAM_DELAY_SYNC, "delay_sync", "fx.delay.sync", 0.0f, (float)(TEMPO_DIV_COUNT - 1), 0.0f, PARAM_INT,
          "", 0, tempo_div_labels, "off = delay_time in seconds; a division = the echo follows the tempo (1/8d = dotted eighth)"),
    PARAM(SYNTH_PARAM_TUNING, "tuning", "tuning.scale", 0.0f, 5.0f, 0.0f, PARAM_INT, "", 0, tuning_labels,
          "temperament: equal; just (pure thirds and fifths, from the root); pythagorean; quarter-comma meantone; "
          "werckmeister III; custom (tune_0..tune_11, e.g. from a Scala file with tools/scala_to_patch.py)"),
    PARAM(SYNTH_PARAM_TUNING_ROOT, "tuning_root", "tuning.root", 0.0f, 11.0f, 0.0f, PARAM_INT, "", 0, root_labels,
          "the note the tuning is built on (its intervals are pure from there)"),
    TUNE(0), TUNE(1), TUNE(2), TUNE(3), TUNE(4), TUNE(5), TUNE(6), TUNE(7), TUNE(8), TUNE(9), TUNE(10), TUNE(11),
    PARAM(SYNTH_PARAM_A4_HZ, "a4_hz", "tuning.a4", 415.0f, 466.0f, 440.0f, PARAM_LIN, "Hz", 0, NULL,
          "concert pitch: the frequency of A4, for every part and every tuning (432, 440, 442, 415 baroque...)"),
};
_Static_assert(SYNTH_PARAM_OUT_FILTER - SYNTH_PARAM_TEMPO == (int)(sizeof tempo_desc / sizeof tempo_desc[0]), "tempo ids");

/* the output filter of every part (macros and routes can move it), then the master's saturation and limiter */
static const char *const out_filter_labels[] = {"off", "lp", "hp", "bp", NULL};
static const char *const saturation_labels[] = {"off", "soft", "tube", "hard", NULL};
static const param_desc_t out_desc[] = {
    PARAM(SYNTH_PARAM_OUT_FILTER, "out_filter", "output.filter.mode", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 1, out_filter_labels,
          "a filter on the instrument's output, before the effects: off, low-pass, high-pass or band-pass (12 dB)"),
    PARAM(SYNTH_PARAM_OUT_CUTOFF, "out_cutoff", "output.filter.cutoff", 20.0f, 20000.0f, 2000.0f, PARAM_EXP, "Hz", 1, NULL,
          "cutoff (or centre) of the output filter"),
    PARAM(SYNTH_PARAM_OUT_RESO, "out_resonance", "output.filter.resonance", 0.0f, 1.0f, 0.2f, PARAM_LIN, "", 1, NULL,
          "resonance of the output filter: 0 = smooth, 1 = sharp peak"),
    PARAM(SYNTH_PARAM_CRUSH_BITS, "crush_bits", "output.crush.bits", 2.0f, 24.0f, 24.0f, PARAM_INT, "bit", 1, NULL,
          "resolution of the instrument's output, its steps spanning the nominal level (-12 dBFS): 24 = clean, 8 = grainy, 2-4 = broken digital"),
    PARAM(SYNTH_PARAM_CRUSH_RATE, "crush_rate", "output.crush.rate", 200.0f, 48000.0f, 48000.0f, PARAM_EXP, "Hz", 1, NULL,
          "sample rate of the instrument's output (sample and hold): 48 kHz = clean, lower = metallic aliasing, a few hundred Hz = data noise"),
    PARAM(SYNTH_PARAM_SATURATION, "saturation", "master.saturation.mode", 0.0f, 3.0f, 0.0f, PARAM_INT, "", 1, saturation_labels,
          "a saturation on the whole mix, after the master level: soft (tanh, glue), tube (asymmetric: even harmonics, warmth), hard (cubic clip: brighter, flat tops)"),
    PARAM(SYNTH_PARAM_SATURATION_DRIVE, "saturation_drive", "master.saturation.drive", 0.0f, 24.0f, 6.0f, PARAM_LIN, "dB", 1, NULL,
          "gain into the saturation, the peaks kept at the same level (denser, so louder, as it squares them): 0-6 dB = glue, 12 dB and more = audibly driven"),
    PARAM(SYNTH_PARAM_SATURATION_TONE, "saturation_tone", "master.saturation.tone", 1000.0f, 20000.0f, 20000.0f, PARAM_EXP, "Hz", 1, NULL,
          "a low-pass after the saturation: 20 kHz = open, lower = darker, tape-like"),
    PARAM(SYNTH_PARAM_LIMITER, "limiter", "master.limiter.mode", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "a look-ahead peak limiter, last on the mix (1.5 ms of latency): no peak goes past the ceiling, no clipping"),
    PARAM(SYNTH_PARAM_LIMITER_GAIN, "limiter_gain", "master.limiter.gain", 0.0f, 18.0f, 0.0f, PARAM_LIN, "dB", 1, NULL,
          "gain into the limiter: louder, the peaks held at the ceiling (beyond 6-8 dB the mix starts to pump)"),
    PARAM(SYNTH_PARAM_LIMITER_CEILING, "limiter_ceiling", "master.limiter.ceiling", -12.0f, 0.0f, -1.0f, PARAM_LIN, "dB", 0, NULL,
          "the highest peak out of the limiter, in dBFS"),
    PARAM(SYNTH_PARAM_LIMITER_RELEASE, "limiter_release", "master.limiter.release", 0.01f, 1.0f, 0.15f, PARAM_EXP, "s", 0, NULL,
          "time to come back after a peak: short = loud but grainy on bass, long = smooth but pumping"),
};
_Static_assert(SYNTH_PARAM_PHASER_MIX - SYNTH_PARAM_OUT_FILTER == (int)(sizeof out_desc / sizeof out_desc[0]), "output filter ids");

const param_desc_t *synth_global_param(int index)
{
    if (index == 0)
        return &master_desc;
    if (index >= 1 && index <= RACK_P_BUS_COUNT)
        return &rack_params[index - 1];
    if (index > RACK_P_BUS_COUNT && index < SYNTH_PARAM_OUT_FILTER - SYNTH_PARAM_GLOBAL)
        return &tempo_desc[index - 1 - RACK_P_BUS_COUNT];
    if (index >= SYNTH_PARAM_OUT_FILTER - SYNTH_PARAM_GLOBAL && index < SYNTH_PARAM_PHASER_MIX - SYNTH_PARAM_GLOBAL)
        return &out_desc[index - (SYNTH_PARAM_OUT_FILTER - SYNTH_PARAM_GLOBAL)];
    if (index >= SYNTH_PARAM_PHASER_MIX - SYNTH_PARAM_GLOBAL && index < SYNTH_GLOBAL_COUNT)
        return &rack_params[rack_index(SYNTH_PARAM_GLOBAL + index)];
    return 0;
}

/* Mixer strip of a part. Defaults: 0 dB, centre, send 1 = exactly the single-engine path of old. */
static const char *const part_channel_labels[] = {"omni", "1", "2", "3", "4", "5", "6", "7", "8",
                                                  "9", "10", "11", "12", "13", "14", "15", "16", NULL};
static const param_desc_t part_desc[SYNTH_PART_COUNT] = {
    PARAM(SYNTH_PART_LEVEL, "part_level", "part.level", -60.0f, 12.0f, 0.0f, PARAM_DB, "dB", 1, NULL, "part level in the mix"),
    PARAM(SYNTH_PART_PAN, "part_pan", "part.pan", -1.0f, 1.0f, 0.0f, PARAM_LIN, "", 1, NULL,
          "part balance (-1 left, +1 right; centre keeps both sides at full level)"),
    PARAM(SYNTH_PART_MUTE, "part_mute", "part.mute", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels, "mute the part"),
    PARAM(SYNTH_PART_CHANNEL, "part_channel", "part.channel", 0.0f, 16.0f, 0.0f, PARAM_INT, "", 0, part_channel_labels,
          "MIDI channel the part listens to (omni = all)"),
    PARAM(SYNTH_PART_KEY_LO, "part_key_lo", "part.key_low", 0.0f, 127.0f, 0.0f, PARAM_INT, "", 0, NULL,
          "lowest MIDI note played by the part (keyboard splits)"),
    PARAM(SYNTH_PART_KEY_HI, "part_key_hi", "part.key_high", 0.0f, 127.0f, 127.0f, PARAM_INT, "", 0, NULL,
          "highest MIDI note played by the part"),
    PARAM(SYNTH_PART_TRANSPOSE, "part_transpose", "part.transpose", -48.0f, 48.0f, 0.0f, PARAM_INT, "st", 0, NULL,
          "transposition of the notes the part receives"),
    PARAM(SYNTH_PART_FX_SEND, "part_fx_send", "part.fx_send", 0.0f, 1.0f, 1.0f, PARAM_LIN, "", 1, NULL,
          "share of the part going through the effect bus (chorus, delay, reverb); the rest bypasses it"),
    PARAM(SYNTH_PART_MPE, "part_mpe", "part.mpe", 0.0f, 1.0f, 0.0f, PARAM_INT, "", 0, param_onoff_labels,
          "MPE: each note on its own channel (2..16) with its own pitch bend, pressure and slide (CC 74); channel 1 "
          "stays global. Pressure and slide are per-voice modulation sources"),
    PARAM(SYNTH_PART_MPE_BEND, "part_mpe_bend", "part.mpe_bend", 0.0f, 96.0f, 48.0f, PARAM_INT, "st", 0, NULL,
          "per-note pitch-bend range of an MPE controller"),
};

const param_desc_t *synth_part_param_info(int index)
{
    return index >= 0 && index < SYNTH_PART_COUNT ? &part_desc[index] : 0;
}

const int synth_mod_count = PM_COUNT;
const param_desc_t *synth_mod_param_info(int index) { return index >= 0 && index < PM_COUNT ? &partmod_params[index] : 0; }

