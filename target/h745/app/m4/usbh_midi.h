/* usbh_midi.h — the board as a USB host for one MIDI device (a class-compliant controller: Launchpad, MiniLab…)
 * plugged straight into the USB OTG FS connector, on the M4: enumeration, then its MIDIStreaming IN endpoints
 * read every millisecond in the USB interrupt, each message pushed to the M7's event queue (shared.h, LINKQ),
 * like the network's. Built on the HAL's host controller driver (HCD) only; descriptors read by
 * midi/usbdesc.h. No hub: the device must be plugged in directly. */
#ifndef USBH_MIDI_H
#define USBH_MIDI_H

#include <stdint.h>

typedef enum {
    USBH_MIDI_OFF,      /* not started (disabled in the build, or the clock failed) */
    USBH_MIDI_EMPTY,    /* nothing plugged in */
    USBH_MIDI_ENUM,     /* a device is being set up */
    USBH_MIDI_RUNNING,  /* a MIDI device plays */
    USBH_MIDI_NOT_MIDI, /* the device plugged in has no MIDI input */
    USBH_MIDI_ERROR,    /* the device did not answer; it is set up again after a while */
} usbh_midi_state_t;

typedef struct {
    usbh_midi_state_t state;
    uint16_t vid, pid;
    char name[24];      /* the product name the device gives, "" if none */
    uint32_t messages;  /* MIDI messages received */
    uint32_t lost;      /* dropped because the M7's queue was full */
    uint8_t last[3];    /* the last message (status 0: none yet), to see what a controller sends */
} usbh_midi_status_t;

/* The last USBH_MIDI_LOG messages received, oldest first (a debugger reads them as usbh_midi_log too). */
#define USBH_MIDI_LOG 16

/* PLL3 for both the display (R: 9.6 MHz pixel clock) and USB (Q: 48 MHz). Called by the display's set-up (it
 * replaces the board support package's MX_LTDC_ClockConfig), or by usbh_midi_init if the display is off. */
int usbh_midi_clock(void);
/* Starts the host and powers the connector (VBUS). */
void usbh_midi_init(void);
/* Main loop: resets the port of a device just plugged in (100 ms busy, once), restarts after an unplug. */
void usbh_midi_poll(void);
/* For the screen. */
usbh_midi_status_t usbh_midi_status(void);

#endif /* USBH_MIDI_H */
