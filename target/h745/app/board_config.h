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

#endif /* BOARD_CONFIG_H */
