/* usbdesc.h — what a USB host needs from a MIDI device's descriptors: the configuration to select and the
 * IN endpoints of its MIDIStreaming interfaces (USB 2.0 spec. sec. 9.6; USB Device Class Definition for MIDI
 * Devices 1.0, sec. 6: interface class 1 audio, subclass 3 MIDIStreaming, bulk endpoints of 4-byte packets).
 * Platform independent: the board's USB host (target/h745/app/m4/usbh_midi.c) feeds it the bytes it read. */
#ifndef USBDESC_H
#define USBDESC_H

#include <stdint.h>

#define USBDESC_MAX_IN 2 /* MIDIStreaming IN endpoints kept (a device with several MIDI interfaces) */

typedef struct {
    uint8_t config;               /* bConfigurationValue, for SET_CONFIGURATION */
    uint8_t n_in;                 /* IN endpoints found, 0 if the device has no MIDI input */
    uint8_t ep_in[USBDESC_MAX_IN]; /* endpoint addresses (bit 7 set) */
    uint16_t mps_in[USBDESC_MAX_IN]; /* wMaxPacketSize (bits 0..10) */
} usbdesc_midi_t;

/* Fields of a device descriptor (18 bytes, or the first 8 for bMaxPacketSize0). Returns 0 if too short or not
 * a device descriptor. */
typedef struct {
    uint8_t mps0;      /* bMaxPacketSize0 */
    uint16_t vid, pid; /* 0 when only 8 bytes were given */
    uint8_t i_product; /* string index of the product name, 0 none */
} usbdesc_device_t;
int usbdesc_device(const uint8_t *d, int len, usbdesc_device_t *out);

/* wTotalLength of a configuration descriptor (its first 9 bytes at least), 0 if not one. */
int usbdesc_config_total(const uint8_t *d, int len);

/* Walks a whole configuration descriptor (wTotalLength bytes; a shorter read is walked as far as it goes).
 * Keeps the IN endpoints (bulk or interrupt) of the MIDIStreaming interfaces at alternate setting 0. Returns
 * the number of IN endpoints found, 0 if none (not a MIDI device), -1 if the bytes are not a configuration. */
int usbdesc_find_midi(const uint8_t *d, int len, usbdesc_midi_t *out);

/* A string descriptor (UTF-16LE) as printable ASCII in out (others become '?'), NUL-terminated. Returns its
 * length, 0 if not a string descriptor. */
int usbdesc_string(const uint8_t *d, int len, char *out, int out_size);

#endif /* USBDESC_H */
