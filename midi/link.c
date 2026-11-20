/* link.c — encoding and decoding of the board link's datagrams (link.h, docs/link.md): little-endian fields
 * written byte by byte (no alignment or endianness assumption), every length checked before it is read.
 * CRC-32 as IEEE 802.3 (reflected polynomial 0xEDB88320), one table lookup per byte (Sarwate): about 8 cycles
 * a byte on a Cortex-M4 instead of ~64 bit by bit, so a 1 KB transfer chunk costs some 40 us in the Ethernet
 * interrupt. */
#include "link.h"

#define LINK_MAGIC0 0x4F /* 'O' */
#define LINK_MAGIC1 0x4C /* 'L' */

/* ---------------- byte helpers ---------------- */

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void putf(uint8_t *p, float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    put32(p, v.u);
}
static float getf(const uint8_t *p)
{
    union { float f; uint32_t u; } v;
    v.u = get32(p);
    return v.f;
}
/* a name field: n bytes, NUL-padded, the last one always NUL */
static void put_name(uint8_t *p, const char *s, int n)
{
    int i = 0;
    for (; s && s[i] && i < n - 1; i++)
        p[i] = (uint8_t)s[i];
    for (; i < n; i++)
        p[i] = 0;
}
static void get_name(const uint8_t *p, char *s, int n)
{
    for (int i = 0; i < n; i++)
        s[i] = (char)p[i];
    s[n - 1] = 0;
}

/* ---------------- header ---------------- */

void link_put_header(uint8_t *out, const link_header_t *h)
{
    out[0] = LINK_MAGIC0;
    out[1] = LINK_MAGIC1;
    out[2] = LINK_VERSION;
    out[3] = h->type;
    put32(out + 4, h->dest);
    put32(out + 8, h->seq);
}

int link_get_header(const uint8_t *in, int len, link_header_t *h)
{
    if (len < LINK_HEADER || len > LINK_MAX_DATAGRAM || in[0] != LINK_MAGIC0 || in[1] != LINK_MAGIC1 ||
        in[2] != LINK_VERSION)
        return -1;
    h->type = in[3];
    h->dest = get32(in + 4);
    h->seq = get32(in + 8);
    return len - LINK_HEADER;
}

uint32_t link_board_id(const uint8_t *uid, int len)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++)
        h = (h ^ uid[i]) * 16777619u;
    return h ? h : 1u;
}

/* ---------------- encoders ---------------- */

static uint8_t *begin(uint8_t *out, const link_header_t *h, uint8_t type)
{
    link_header_t t = *h;
    t.type = type;
    link_put_header(out, &t);
    return out + LINK_HEADER;
}
static int end(const uint8_t *out, const uint8_t *p) { return (int)(p - out); }

#define HELLO_SIZE (4 + 12 + 4 + 4 + 2 + 2 + 1 + 1 + 4 + 4 + 16 + LINK_NAME)

int link_encode_hello(uint8_t *out, const link_header_t *h, const link_hello_t *m)
{
    uint8_t *p = begin(out, h, LINK_HELLO);
    put32(p, m->id), p += 4;
    for (int i = 0; i < 12; i++)
        *p++ = m->uid[i];
    put32(p, m->ip), p += 4;
    put32(p, m->catalog), p += 4;
    put16(p, (uint16_t)m->preset), p += 2;
    put16(p, m->preset_count), p += 2;
    *p++ = m->load_pct;
    *p++ = m->peak_pct;
    put32(p, m->overruns), p += 4;
    put32(p, m->kit_size), p += 4;
    put_name(p, m->board, 16), p += 16;
    put_name(p, m->playing, LINK_NAME), p += LINK_NAME;
    return end(out, p);
}

int link_encode_ack(uint8_t *out, const link_header_t *h, uint32_t seq, uint8_t status)
{
    uint8_t *p = begin(out, h, LINK_ACK);
    put32(p, seq), p += 4;
    *p++ = status;
    return end(out, p);
}

int link_encode_midi(uint8_t *out, const link_header_t *h, const uint8_t *bytes, int len)
{
    if (len < 0 || len > LINK_MAX_PAYLOAD)
        return -1;
    uint8_t *p = begin(out, h, LINK_MIDI);
    for (int i = 0; i < len; i++)
        *p++ = bytes[i];
    return end(out, p);
}

int link_encode_param(uint8_t *out, const link_header_t *h, uint32_t catalog, const link_param_t *prm, int count)
{
    if (count < 0 || count > 255 || 5 + 6 * count > LINK_MAX_PAYLOAD)
        return -1;
    uint8_t *p = begin(out, h, LINK_PARAM);
    put32(p, catalog), p += 4;
    *p++ = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        put16(p, prm[i].id), p += 2;
        putf(p, prm[i].value), p += 4;
    }
    return end(out, p);
}

int link_encode_select(uint8_t *out, const link_header_t *h, const char *name)
{
    uint8_t *p = begin(out, h, LINK_SELECT);
    put_name(p, name, LINK_NAME), p += LINK_NAME;
    return end(out, p);
}

int link_encode_preset(uint8_t *out, const link_header_t *h, const link_preset_t *m)
{
    if (m->count > LINK_PARAMS_MAX)
        return -1;
    uint8_t *p = begin(out, h, LINK_PRESET);
    put32(p, m->catalog), p += 4;
    *p++ = m->engine;
    *p++ = m->count;
    put_name(p, m->name, LINK_NAME), p += LINK_NAME;
    for (int i = 0; i < m->count; i++) {
        put16(p, m->params[i].id), p += 2;
        putf(p, m->params[i].value), p += 4;
    }
    return end(out, p);
}

int link_encode_xfer_begin(uint8_t *out, const link_header_t *h, const link_xfer_t *m)
{
    uint8_t *p = begin(out, h, LINK_XFER_BEGIN);
    put32(p, m->xfer), p += 4;
    *p++ = m->kind;
    put32(p, m->size), p += 4;
    put32(p, m->crc), p += 4;
    put_name(p, m->name, LINK_NAME), p += LINK_NAME;
    return end(out, p);
}

int link_encode_xfer_data(uint8_t *out, const link_header_t *h, uint32_t xfer, uint32_t offset,
                          const uint8_t *data, int len)
{
    if (len < 0 || len > LINK_CHUNK)
        return -1;
    uint8_t *p = begin(out, h, LINK_XFER_DATA);
    put32(p, xfer), p += 4;
    put32(p, offset), p += 4;
    for (int i = 0; i < len; i++)
        *p++ = data[i];
    return end(out, p);
}

int link_encode_xfer_ack(uint8_t *out, const link_header_t *h, uint32_t xfer, uint32_t next, uint8_t status)
{
    uint8_t *p = begin(out, h, LINK_XFER_ACK);
    put32(p, xfer), p += 4;
    put32(p, next), p += 4;
    *p++ = status;
    return end(out, p);
}

/* ---------------- decoders ---------------- */

int link_decode_hello(const uint8_t *p, int len, link_hello_t *m)
{
    if (len < HELLO_SIZE)
        return -1;
    m->id = get32(p), p += 4;
    for (int i = 0; i < 12; i++)
        m->uid[i] = *p++;
    m->ip = get32(p), p += 4;
    m->catalog = get32(p), p += 4;
    m->preset = (int16_t)get16(p), p += 2;
    m->preset_count = (uint16_t)get16(p), p += 2;
    m->load_pct = *p++;
    m->peak_pct = *p++;
    m->overruns = get32(p), p += 4;
    m->kit_size = get32(p), p += 4;
    get_name(p, m->board, 16), p += 16;
    get_name(p, m->playing, LINK_NAME);
    return 0;
}

int link_decode_ack(const uint8_t *p, int len, uint32_t *seq, uint8_t *status)
{
    if (len < 5)
        return -1;
    *seq = get32(p);
    *status = p[4];
    return 0;
}

int link_decode_param(const uint8_t *p, int len, uint32_t *catalog, link_param_t *out, int max, int *count)
{
    if (len < 5)
        return -1;
    const int n = p[4];
    if (n > max || len < 5 + 6 * n)
        return -1;
    *catalog = get32(p);
    p += 5;
    for (int i = 0; i < n; i++, p += 6) {
        out[i].id = (uint16_t)get16(p);
        out[i].value = getf(p + 2);
    }
    *count = n;
    return 0;
}

int link_decode_select(const uint8_t *p, int len, char name[LINK_NAME])
{
    if (len < LINK_NAME)
        return -1;
    get_name(p, name, LINK_NAME);
    return 0;
}

int link_decode_preset(const uint8_t *p, int len, link_preset_t *m)
{
    if (len < 6 + LINK_NAME)
        return -1;
    const int n = p[5];
    if (n > LINK_PARAMS_MAX || len < 6 + LINK_NAME + 6 * n)
        return -1;
    m->catalog = get32(p);
    m->engine = p[4];
    m->count = (uint8_t)n;
    get_name(p + 6, m->name, LINK_NAME);
    p += 6 + LINK_NAME;
    for (int i = 0; i < n; i++, p += 6) {
        m->params[i].id = (uint16_t)get16(p);
        m->params[i].value = getf(p + 2);
    }
    return 0;
}

int link_decode_xfer_begin(const uint8_t *p, int len, link_xfer_t *m)
{
    if (len < 13 + LINK_NAME)
        return -1;
    m->xfer = get32(p);
    m->kind = p[4];
    m->size = get32(p + 5);
    m->crc = get32(p + 9);
    get_name(p + 13, m->name, LINK_NAME);
    return 0;
}

int link_decode_xfer_data(const uint8_t *p, int len, uint32_t *xfer, uint32_t *offset, const uint8_t **data,
                          int *n)
{
    if (len < 8 || len - 8 > LINK_CHUNK)
        return -1;
    *xfer = get32(p);
    *offset = get32(p + 4);
    *data = p + 8;
    *n = len - 8;
    return 0;
}

int link_decode_xfer_ack(const uint8_t *p, int len, uint32_t *xfer, uint32_t *next, uint8_t *status)
{
    if (len < 9)
        return -1;
    *xfer = get32(p);
    *next = get32(p + 4);
    *status = p[8];
    return 0;
}

int link_midi_next(const uint8_t *p, int len, int *pos)
{
    const int i = *pos;
    if (i >= len)
        return 0;
    const uint8_t st = p[i];
    int n;
    if (st < 0x80)
        return -1; /* a data byte where a status is expected */
    if (st < 0xF0)
        n = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 2 : 3;
    else if (st >= 0xF8)
        n = 1; /* system real-time */
    else
        return -1; /* system common and SysEx: not carried */
    if (i + n > len)
        return -1;
    for (int k = 1; k < n; k++)
        if (p[i + k] >= 0x80)
            return -1;
    *pos = i + n;
    return n;
}

/* ---------------- transfers ---------------- */

static const uint32_t crc_table[256] = { /* crc_table[n]: CRC of the byte n (reflected 0xEDB88320) */
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u, 0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u, 0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

uint32_t link_crc32(uint32_t crc, const uint8_t *data, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

void link_rx_init(link_rx_t *r, uint8_t *mem, uint32_t capacity)
{
    r->mem = mem;
    r->capacity = capacity;
    r->info.xfer = 0;
    r->info.size = 0;
    r->next = 0;
    r->crc = 0;
    r->status = 0xFF;
}

uint8_t link_rx_begin(link_rx_t *r, const link_xfer_t *x)
{
    r->info = *x;
    r->next = 0;
    r->crc = 0;
    r->status = x->size > r->capacity ? LINK_TOO_BIG : x->size == 0 ? LINK_BAD : LINK_PENDING;
    return r->status;
}

uint8_t link_rx_data(link_rx_t *r, uint32_t xfer, uint32_t offset, const uint8_t *data, int n)
{
    if (r->status == 0xFF || xfer != r->info.xfer)
        return LINK_BAD; /* no such transfer: the sender must BEGIN */
    if (r->status != LINK_PENDING || offset != r->next || n <= 0)
        return r->status; /* finished, failed, or out of order: acknowledge what we have */
    if ((uint32_t)n > r->info.size - r->next)
        return r->status = LINK_BAD;
    /* 32-bit writes where the destination is aligned (4 times fewer bus writes to an uncached SDRAM); the
     * source sits at any offset in the frame, so it is read byte by byte (little-endian, like every target) */
    uint8_t *dst = r->mem + r->next;
    int i = 0;
    for (; i < n && ((uintptr_t)(dst + i) & 3u); i++)
        dst[i] = data[i];
    for (; i + 4 <= n; i += 4)
        *(uint32_t *)(void *)(dst + i) = (uint32_t)data[i] | (uint32_t)data[i + 1] << 8 |
                                         (uint32_t)data[i + 2] << 16 | (uint32_t)data[i + 3] << 24;
    for (; i < n; i++)
        dst[i] = data[i];
    r->crc = link_crc32(r->crc, data, (size_t)n);
    r->next += (uint32_t)n;
    if (r->next == r->info.size)
        r->status = r->crc == r->info.crc ? LINK_OK : LINK_CRC;
    return r->status;
}
