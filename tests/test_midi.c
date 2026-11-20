/* test_midi.c — USB-MIDI decoding (CIN, cables) and DIN stream (running status, real-time, SysEx). */
#include "midi.h"
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
}
