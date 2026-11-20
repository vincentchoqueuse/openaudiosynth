/* board_config.h — the board's settings chosen at build time: edit, then `just flash`.
 * Read by both cores (the M4 takes MIDI in, the M7 plays it). */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Where the board takes MIDI from: 1 to listen, 0 to ignore.
 *   BOARD_MIDI_USB  a class-compliant USB MIDI controller (Launchpad, MiniLab…) plugged straight into the
 *                   USB OTG FS micro-USB connector (not the ST-LINK one), with an OTG adapter or cable; the board
 *                   powers it (m4/usbh_midi.c)
 *   BOARD_MIDI_NET  notes and controllers sent over Ethernet by a computer or the hub (docs/link.md)
 * Both at once is fine. Presets and sample kits come over the network in any case. */
#ifndef BOARD_MIDI_USB
#define BOARD_MIDI_USB 1
#endif
#ifndef BOARD_MIDI_NET
#define BOARD_MIDI_NET 1
#endif

/* The MIDI CCs that turn macros 1..8 (the knobs of a controller), on every preset the board plays: they replace
 * the presets' own (macro<k>_cc, CCs 102..109 by default). 0 keeps a macro's own CC; any channel is heard (part 0
 * is omni). Here the top row of knobs of a Launch Control in its default layout, which sends CCs 13..20 on
 * channel 1. Remove the line to keep the presets' CCs. */
#ifndef BOARD_MACRO_CC
#define BOARD_MACRO_CC {13, 14, 15, 16, 17, 18, 19, 20}
#endif

/* 1: a preset that gives its macros nothing to do gets eight targets (knobs.c): its engine's six main parameters
 * (cutoff, resonance, envelope...) on macros 1..6, the delay and reverb sends on 7 and 8. A knob then sets its
 * parameter over the whole range; the preset sounds as written until the knob is turned. 0: macros as written. */
#ifndef BOARD_KNOB_TARGETS
#define BOARD_KNOB_TARGETS 1
#endif

#endif /* BOARD_CONFIG_H */
