/* test_midi.c — USB-MIDI decoding (CIN, cables), DIN stream (running status, real-time, SysEx), and the USB
 * descriptors a host reads from a MIDI device (usbdesc.h; layout of the MIDI 1.0 class spec., appendix B). */
#include "midi.h"
#include "usbdesc.h"
#include "test.h"

void suite_midi(void)
{
    midi_msg_t m;

    /* USB: note-on on cable 1 */
    const uint8_t p1[4] = {0x19, 0x90, 60, 100};
    CHECK(midi_usb_decode(p1, &m) && m.cable == 1 && m.len == 3 && m.data[0] == 0x90 && m.data[1] == 60 &&
              m.data[2] == 100, "USB note-on");
    /* CIN 0 ignored (MiniLab 3 padding) */
    const uint8_t p0[4] = {0x00, 0x00, 0x00, 0x00};
    CHECK(!midi_usb_decode(p0, &m), "CIN 0 not ignored");
    /* program change: 2 bytes */
    const uint8_t pc[4] = {0x2C, 0xC3, 5, 0};
    CHECK(midi_usb_decode(pc, &m) && m.cable == 2 && m.len == 2 && m.data[1] == 5, "USB program change");
    /* CIN/status mismatch rejected */
    const uint8_t bad[4] = {0x09, 0x80, 60, 0};
    CHECK(!midi_usb_decode(bad, &m), "CIN 9 with status 0x80 accepted");
    /* data byte with bit 7 set rejected */
    const uint8_t bad2[4] = {0x0B, 0xB0, 0x81, 0};
    CHECK(!midi_usb_decode(bad2, &m), "data >= 0x80 accepted");
    /* SysEx ignored, clock accepted */
    const uint8_t sx[4] = {0x04, 0xF0, 0x43, 0x00};
    CHECK(!midi_usb_decode(sx, &m), "SysEx not ignored");
    const uint8_t clk[4] = {0x0F, 0xF8, 0, 0};
    CHECK(midi_usb_decode(clk, &m) && m.len == 1 && m.data[0] == 0xF8, "USB clock");

    /* DIN: running status + interleaved real-time + SysEx ignored */
    {
        midi_din_t d;
        midi_din_init(&d);
        const uint8_t stream[] = {0x90, 60, 100, 62, 0xF8, 90, 0xF0, 0x43, 0x10, 0xF7, 0x90, 64, 0,
                                  0xB1, 74, 0x40, 0xE0, 0x00, 0x40, 0xC2, 7, 8};
        midi_msg_t out[16];
        int n = 0;
        for (size_t i = 0; i < sizeof stream; i++)
            if (midi_din_feed(&d, stream[i], &out[n]))
                n++;
        CHECK(n == 8, "DIN: %d messages instead of 8", n);
        if (n == 8) {
            CHECK(out[0].data[0] == 0x90 && out[0].data[1] == 60 && out[0].data[2] == 100, "DIN msg 0");
            CHECK(out[1].data[0] == 0xF8 && out[1].len == 1, "DIN interleaved real-time");
            CHECK(out[2].data[0] == 0x90 && out[2].data[1] == 62 && out[2].data[2] == 90, "DIN running status");
            CHECK(out[3].data[1] == 64 && out[3].data[2] == 0, "DIN after SysEx");
            CHECK(out[4].data[0] == 0xB1 && out[4].data[1] == 74, "DIN CC");
            CHECK(out[5].data[0] == 0xE0 && out[5].data[2] == 0x40, "DIN pitch bend");
            CHECK(out[6].len == 2 && out[6].data[1] == 7, "DIN program change");
            CHECK(out[7].len == 2 && out[7].data[0] == 0xC2 && out[7].data[1] == 8, "DIN 2-byte running status");
        }
    }
    /* Data without status: ignored */
    {
        midi_din_t d;
        midi_din_init(&d);
        int got = 0;
        got += midi_din_feed(&d, 60, &m);
        got += midi_din_feed(&d, 100, &m);
        CHECK(got == 0, "data without status accepted");
    }

    /* USB descriptors: a MIDI device as in the class spec. (appendix B): an audio control interface, then a
     * MIDIStreaming interface with its jacks, a bulk OUT and a bulk IN endpoint (9 bytes each, followed by
     * their class-specific descriptors) */
    {
        static const uint8_t dev[18] = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x35, 0x12, 0x03, 0x01, 0, 1, 1, 2, 3, 1};
        usbdesc_device_t dv;
        CHECK(usbdesc_device(dev, 8, &dv) && dv.mps0 == 64 && dv.vid == 0, "device descriptor, 8 bytes");
        CHECK(usbdesc_device(dev, 18, &dv) && dv.vid == 0x1235 && dv.pid == 0x0103 && dv.i_product == 2,
              "device descriptor, 18 bytes");
        CHECK(!usbdesc_device(dev, 7, &dv), "short device descriptor accepted");

        static const uint8_t cfg[] = {
            9, 2, 101, 0, 2, 1, 0, 0x80, 50,                     /* configuration 1, 101 bytes, 2 interfaces */
            9, 4, 0, 0, 0, 1, 1, 0, 0,                           /* interface 0: audio control */
            9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,                  /* class-specific AC header */
            9, 4, 1, 0, 2, 1, 3, 0, 0,                           /* interface 1: MIDIStreaming, 2 endpoints */
            7, 0x24, 1, 0x00, 0x01, 65, 0,                       /* class-specific MS header */
            6, 0x24, 2, 1, 1, 0, 6, 0x24, 2, 2, 2, 0,            /* MIDI IN jacks */
            9, 0x24, 3, 1, 3, 1, 2, 1, 0, 9, 0x24, 3, 2, 4, 1, 1, 1, 0, /* MIDI OUT jacks */
            9, 5, 0x01, 2, 64, 0, 0, 0, 0,                       /* bulk OUT endpoint 1 */
            5, 0x25, 1, 1, 1,                                    /* its class-specific descriptor */
            9, 5, 0x81, 2, 64, 0, 0, 0, 0,                       /* bulk IN endpoint 1 */
            5, 0x25, 1, 1, 3,
        };
        CHECK(sizeof cfg == 101, "test descriptor is %d bytes", (int)sizeof cfg);
        usbdesc_midi_t md;
        CHECK(usbdesc_config_total(cfg, 9) == 101, "wTotalLength");
        CHECK(usbdesc_find_midi(cfg, sizeof cfg, &md) == 1 && md.config == 1 && md.ep_in[0] == 0x81 &&
                  md.mps_in[0] == 64, "MIDIStreaming IN endpoint");
        /* only the first 9 bytes: a configuration without MIDI found yet */
        CHECK(usbdesc_find_midi(cfg, 9, &md) == 0, "truncated configuration");
        /* a broken length stops the walk instead of reading past the end */
        uint8_t bad[sizeof cfg];
        for (size_t i = 0; i < sizeof cfg; i++)
            bad[i] = cfg[i];
        bad[9 + 9 + 9] = 0; /* the MIDIStreaming interface: length 0 */
        CHECK(usbdesc_find_midi(bad, sizeof bad, &md) == 0, "length 0 not stopped");
        bad[9 + 9 + 9] = 200; /* past the end */
        CHECK(usbdesc_find_midi(bad, sizeof bad, &md) == 0, "length past the end not stopped");
        /* not a configuration */
        CHECK(usbdesc_find_midi(dev, sizeof dev, &md) == -1, "device descriptor taken as configuration");
        /* the endpoints of an interface that is not MIDIStreaming (a keyboard: HID, class 3) are not taken */
        static const uint8_t hid[] = {9, 2, 25, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 1, 3, 1, 1, 0,
                                      7, 5, 0x81, 3, 8, 0, 10};
        CHECK(usbdesc_find_midi(hid, sizeof hid, &md) == 0, "HID endpoint taken as MIDI");

        static const uint8_t str[] = {22, 3, 'L', 0, 'a', 0, 'u', 0, 'n', 0, 'c', 0, 'h', 0, 'p', 0, 'a', 0, 'd', 0,
                                      0xE9, 0};
        char name[8];
        CHECK(usbdesc_string(str, sizeof str, name, sizeof name) == 7 && name[0] == 'L' && name[7] == 0,
              "string descriptor, truncated to the buffer");
        char full[32];
        CHECK(usbdesc_string(str, sizeof str, full, sizeof full) == 10 && full[9] == '?', "non-ASCII character");
    }
}
