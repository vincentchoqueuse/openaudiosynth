/* usbdesc.c — reads the descriptors of a USB MIDI device (usbdesc.h): device, configuration with its
 * MIDIStreaming interfaces and their IN endpoints, product name. Bounded by the bytes given: a malformed
 * descriptor (length 0, past the end) stops the walk. */
#include "usbdesc.h"

enum { DT_DEVICE = 1, DT_CONFIG = 2, DT_STRING = 3, DT_INTERFACE = 4, DT_ENDPOINT = 5 };
enum { CLASS_AUDIO = 1, SUBCLASS_MIDISTREAMING = 3 };

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int usbdesc_device(const uint8_t *d, int len, usbdesc_device_t *out)
{
    if (len < 8 || d[1] != DT_DEVICE || d[0] < 8)
        return 0;
    out->mps0 = d[7];
    out->vid = out->pid = 0;
    out->i_product = 0;
    if (len >= 18 && d[0] >= 18) {
        out->vid = le16(d + 8);
        out->pid = le16(d + 10);
        out->i_product = d[15];
    }
    return 1;
}

int usbdesc_config_total(const uint8_t *d, int len)
{
    if (len < 9 || d[1] != DT_CONFIG || d[0] < 9)
        return 0;
    return le16(d + 2);
}

int usbdesc_find_midi(const uint8_t *d, int len, usbdesc_midi_t *out)
{
    out->config = 0;
    out->n_in = 0;
    if (!usbdesc_config_total(d, len))
        return -1;
    const int total = le16(d + 2);
    if (len > total)
        len = total;
    out->config = d[5];
    int midi = 0; /* inside a MIDIStreaming interface, alternate setting 0 */
    for (int pos = 0; pos + 2 <= len;) {
        const int bl = d[pos];
        if (bl < 2 || pos + bl > len)
            break;
        const uint8_t *p = d + pos;
        if (p[1] == DT_INTERFACE && bl >= 9) {
            midi = p[5] == CLASS_AUDIO && p[6] == SUBCLASS_MIDISTREAMING && p[3] == 0;
        } else if (p[1] == DT_ENDPOINT && bl >= 7 && midi) {
            const int type = p[3] & 3; /* 2 bulk, 3 interrupt */
            if ((p[2] & 0x80) && (type == 2 || type == 3) && out->n_in < USBDESC_MAX_IN) {
                out->ep_in[out->n_in] = p[2];
                out->mps_in[out->n_in] = (uint16_t)(le16(p + 4) & 0x7FF);
                out->n_in++;
            }
        }
        pos += bl;
    }
    return out->n_in;
}

int usbdesc_string(const uint8_t *d, int len, char *out, int out_size)
{
    if (out_size < 1)
        return 0;
    out[0] = 0;
    if (len < 2 || d[1] != DT_STRING)
        return 0;
    int bl = d[0] < len ? d[0] : len;
    int n = 0;
    for (int i = 2; i + 1 < bl && n < out_size - 1; i += 2) {
        const uint16_t c = le16(d + i);
        out[n++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
    }
    out[n] = 0;
    return n;
}
