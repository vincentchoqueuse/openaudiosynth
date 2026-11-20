/* midi.c — 4-byte USB-MIDI and DIN stream decoding (see midi.h).
 * Reference: USB MIDI 1.0 sec. 4 (Code Index Number table) and MIDI 1.0 Detailed Specification. */
#include "midi.h"

int midi_channel_data_len(uint8_t status)
{
    switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    default: return -1;
    }
}

int midi_usb_decode(const uint8_t pkt[4], midi_msg_t *out)
{
    const uint8_t cin = pkt[0] & 0x0F;
    int len;
    switch (cin) {
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xE:
        len = 3;
        if ((pkt[1] & 0xF0) != (uint8_t)(cin << 4))
            return 0; /* CIN and status mismatch */
        break;
    case 0xC: case 0xD:
        len = 2;
        if ((pkt[1] & 0xF0) != (uint8_t)(cin << 4))
            return 0;
        break;
    case 0x2: case 0x3:                       /* 2- and 3-byte system common */
        if (pkt[1] < 0xF0)
            return 0;
        len = cin == 0x2 ? 2 : 3;
        break;
    case 0x5:                                 /* 1-byte system common, or SysEx end */
        if (pkt[1] < 0xF1 || pkt[1] == 0xF7)
            return 0;
        len = 1;
        break;
    case 0xF:                                 /* single byte (real-time) */
        if (pkt[1] < 0xF8)
            return 0;
        len = 1;
        break;
    default:
        return 0; /* 0x0 reserved, 0x1 cable events, 0x4/0x6/0x7 SysEx */
    }
    if (pkt[1] < 0x80)
        return 0;
    for (int i = 1; i < len; i++)
        if (pkt[1 + i] & 0x80)
            return 0;
    out->cable = pkt[0] >> 4;
    out->len = (uint8_t)len;
    out->data[0] = pkt[1];
    out->data[1] = len > 1 ? pkt[2] : 0;
    out->data[2] = len > 2 ? pkt[3] : 0;
    return 1;
}

void midi_din_init(midi_din_t *p)
{
    p->status = 0;
    p->count = p->need = 0;
    p->in_sysex = 0;
}

int midi_din_feed(midi_din_t *p, uint8_t b, midi_msg_t *out)
{
    out->cable = 0;
    if (b >= 0xF8) { /* real-time: may be interleaved anywhere */
        out->len = 1;
        out->data[0] = b;
        out->data[1] = out->data[2] = 0;
        return 1;
    }
    if (b & 0x80) {
        p->count = 0;
        if (b == 0xF0) {
            p->in_sysex = 1;
            p->status = 0;
            return 0;
        }
        p->in_sysex = 0;
        if (b == 0xF7) {
            p->status = 0;
            return 0;
        }
        if (b >= 0xF0) { /* system common: cancels running status */
            p->status = 0;
            if (b == 0xF1 || b == 0xF3) {
                p->status = b;
                p->need = 1;
            } else if (b == 0xF2) {
                p->status = b;
                p->need = 2;
            } else if (b == 0xF6) {
                out->len = 1;
                out->data[0] = b;
                out->data[1] = out->data[2] = 0;
                return 1;
            }
            return 0;
        }
        p->status = b;
        p->need = (uint8_t)midi_channel_data_len(b);
        return 0;
    }
    /* data byte */
    if (p->in_sysex || p->status == 0)
        return 0;
    p->buf[p->count++] = b;
    if (p->count < p->need)
        return 0;
    out->len = (uint8_t)(1 + p->need);
    out->data[0] = p->status;
    out->data[1] = p->buf[0];
    out->data[2] = p->need > 1 ? p->buf[1] : 0;
    p->count = 0;
    if (p->status >= 0xF0)
        p->status = 0; /* no running status for system common */
    return 1;
}
