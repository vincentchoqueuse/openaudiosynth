/* midi.h — platform-independent MIDI parsing:
 * 4-byte USB-MIDI 1.0 packets (USB Device Class for MIDI Devices 1.0 specification, sec. 4)
 * and DIN byte stream with running status. SysEx is ignored for now. */
#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

typedef struct {
    uint8_t cable; /* USB cable (high nibble of the header), 0 for DIN */
    uint8_t len;   /* 1 to 3 */
    uint8_t data[3];
} midi_msg_t;

/* Decodes a USB-MIDI packet. Returns 1 if out holds a message, 0 otherwise (CIN 0/1, SysEx, invalid). */
int midi_usb_decode(const uint8_t pkt[4], midi_msg_t *out);

typedef struct {
    uint8_t status;  /* current status (running status if channel), 0 if none */
    uint8_t buf[2];
    uint8_t count, need;
    uint8_t in_sysex;
} midi_din_t;

void midi_din_init(midi_din_t *p);
/* Feeds one byte; returns 1 when a complete message is available in out.
 * Real-time messages (F8..FF) are returned immediately without disturbing the running status. */
int midi_din_feed(midi_din_t *p, uint8_t byte, midi_msg_t *out);

/* Number of data bytes of a channel message (0x80..0xEF), or -1. */
int midi_channel_data_len(uint8_t status);

#endif /* MIDI_H */
