/* link.h — the board link: how a host (the Mac, later the Pi hub) drives a synth board over Ethernet with
 * UDP datagrams. One datagram = a 12-byte header + one message: MIDI, parameter values, a preset, the choice
 * of a built-in preset, or a chunk of a large transfer (a sample kit). Specified in docs/link.md.
 * Encoding and decoding only (no sockets, no allocation, little-endian fields), shared by the board and the
 * host tools; the transport is udpip.h on the board and BSD sockets on the host. */
#ifndef LINK_H
#define LINK_H

#include <stddef.h>
#include <stdint.h>

#define LINK_PORT 47100u        /* UDP port of the boards (and of the replies) */
#define LINK_GROUP 0xEFFF4701u  /* 239.255.71.1: multicast group every board listens to (the hub's) */
#define LINK_VERSION 1u
#define LINK_HEADER 12
#define LINK_MAX_DATAGRAM 1472  /* UDP payload of a 1500-byte Ethernet frame: never fragmented */
#define LINK_MAX_PAYLOAD (LINK_MAX_DATAGRAM - LINK_HEADER)
#define LINK_ALL 0u             /* destination: every board */
#define LINK_NAME 32            /* bytes of a name field (NUL-padded, the last byte always NUL) */
#define LINK_PARAMS_MAX 230     /* parameters in one preset message (6 bytes each: the datagram's limit) */
#define LINK_CHUNK 1024         /* data bytes in one transfer chunk */
#define LINK_HEARTBEAT_MS 100   /* a host that plays sends something (MIDI or HEARTBEAT) at least this often */
#define LINK_SILENCE_MS 500     /* a board that received MIDI and then nothing for this long stops every note */

typedef enum {
    LINK_DISCOVER = 0x01, /* host -> boards: who is there? (no payload) */
    LINK_HELLO = 0x02,    /* board -> host: link_hello_t, the answer to DISCOVER (also sent at start-up) */
    LINK_ACK = 0x03,      /* board -> host: {seq u32, status u8}: the outcome of a SELECT or a PRESET */
    LINK_HEARTBEAT = 0x04, /* host -> boards: no payload, "still here" (every LINK_HEARTBEAT_MS while playing) */
    LINK_MIDI = 0x10,     /* MIDI 1.0 messages back to back, each with its status byte (no running status) */
    LINK_PARAM = 0x11,    /* {catalog u32, count u8, count x {id u16, value f32}}: synth_set_param, 0..1 */
    LINK_SELECT = 0x12,   /* {name[32]}: play the built-in preset of this name */
    LINK_PRESET = 0x13,   /* {catalog u32, engine u8, count u8, name[32], count x {id u16, value f32}} */
    LINK_XFER_BEGIN = 0x20, /* {xfer u32, kind u8, size u32, crc u32, name[32]}: a large transfer starts */
    LINK_XFER_DATA = 0x21,  /* {xfer u32, offset u32, bytes}: in order; others are ignored and re-acked */
    LINK_XFER_ACK = 0x22,   /* board -> host: {xfer u32, next u32, status u8}: bytes received in order */
} link_type_t;

typedef enum {
    LINK_OK = 0,
    LINK_BUSY = 1,     /* the board is still applying the previous preset: send again */
    LINK_BAD = 2,      /* malformed message */
    LINK_CATALOG = 3,  /* the parameter ids are of another build (catalog hash differs) */
    LINK_UNKNOWN = 4,  /* no built-in preset of this name */
    LINK_TOO_BIG = 5,  /* the transfer does not fit in the board's memory */
    LINK_CRC = 6,      /* the transfer arrived whole but corrupted: start again */
    LINK_PENDING = 7,  /* transfer in progress */
} link_status_t;

typedef enum { LINK_KIND_KIT = 1 } link_kind_t;

/* ---------------- header ---------------- */

typedef struct {
    uint8_t type;   /* link_type_t */
    uint32_t dest;  /* board id (link_board_id), LINK_ALL for every board; the sender's id in replies */
    uint32_t seq;   /* chosen by the sender, echoed by LINK_ACK */
} link_header_t;

/* Writes the header into out (LINK_HEADER bytes). */
void link_put_header(uint8_t *out, const link_header_t *h);
/* Reads a datagram's header; returns the payload length, or -1 if it is not a link datagram of this version. */
int link_get_header(const uint8_t *in, int len, link_header_t *h);
/* 1 if a datagram with this destination is for the board of this id. */
static inline int link_for_me(uint32_t dest, uint32_t id) { return dest == LINK_ALL || dest == id; }

/* Board id: FNV-1a of the MCU's unique id (never 0). */
uint32_t link_board_id(const uint8_t *uid, int len);

/* ---------------- messages ---------------- */

typedef struct {
    uint32_t id;           /* board id */
    uint8_t uid[12];       /* MCU unique id */
    uint32_t ip;           /* its IPv4 address (host order) */
    uint32_t catalog;      /* hash of the parameter catalog it was built with */
    int16_t preset;        /* built-in preset playing, -1 none, -2 a preset received over the link */
    uint16_t preset_count; /* built-in presets */
    uint8_t load_pct;      /* DSP load, average */
    uint8_t peak_pct;      /* DSP load, worst buffer */
    uint32_t overruns;     /* audio buffers late since start */
    uint32_t kit_size;     /* bytes of the sample kit in memory, 0 none */
    char board[16];        /* "h745" */
    char playing[LINK_NAME];
} link_hello_t;

typedef struct {
    uint16_t id;
    float value; /* normalized 0..1 */
} link_param_t;

typedef struct {
    uint32_t catalog;
    uint8_t engine; /* synth_engine_t */
    uint8_t count;
    char name[LINK_NAME];
    link_param_t params[LINK_PARAMS_MAX];
} link_preset_t;

typedef struct {
    uint32_t xfer;  /* transfer id, chosen by the host */
    uint8_t kind;   /* link_kind_t */
    uint32_t size;  /* bytes */
    uint32_t crc;   /* link_crc32 of the whole content */
    char name[LINK_NAME];
} link_xfer_t;

/* Encoders: write the payload after the header into out (LINK_MAX_DATAGRAM bytes), return the datagram's
 * length, or -1 if it does not fit. */
int link_encode_hello(uint8_t *out, const link_header_t *h, const link_hello_t *m);
int link_encode_ack(uint8_t *out, const link_header_t *h, uint32_t seq, uint8_t status);
int link_encode_midi(uint8_t *out, const link_header_t *h, const uint8_t *bytes, int len);
int link_encode_param(uint8_t *out, const link_header_t *h, uint32_t catalog, const link_param_t *p, int count);
int link_encode_select(uint8_t *out, const link_header_t *h, const char *name);
int link_encode_preset(uint8_t *out, const link_header_t *h, const link_preset_t *m);
int link_encode_xfer_begin(uint8_t *out, const link_header_t *h, const link_xfer_t *m);
int link_encode_xfer_data(uint8_t *out, const link_header_t *h, uint32_t xfer, uint32_t offset,
                          const uint8_t *data, int len);
int link_encode_xfer_ack(uint8_t *out, const link_header_t *h, uint32_t xfer, uint32_t next, uint8_t status);

/* Decoders: read a payload (after link_get_header); return 0, or -1 if malformed. */
int link_decode_hello(const uint8_t *p, int len, link_hello_t *m);
int link_decode_ack(const uint8_t *p, int len, uint32_t *seq, uint8_t *status);
int link_decode_param(const uint8_t *p, int len, uint32_t *catalog, link_param_t *out, int max, int *count);
int link_decode_select(const uint8_t *p, int len, char name[LINK_NAME]);
int link_decode_preset(const uint8_t *p, int len, link_preset_t *m);
int link_decode_xfer_begin(const uint8_t *p, int len, link_xfer_t *m);
int link_decode_xfer_data(const uint8_t *p, int len, uint32_t *xfer, uint32_t *offset, const uint8_t **data,
                          int *n);
int link_decode_xfer_ack(const uint8_t *p, int len, uint32_t *xfer, uint32_t *next, uint8_t *status);

/* Splits the payload of a LINK_MIDI message: returns the length of the message at p[*pos] and advances *pos,
 * 0 at the end, -1 if malformed (a data byte where a status is expected, or a message cut short). Channel
 * messages and the one-byte system real-time ones (clock, start, stop...) only. */
int link_midi_next(const uint8_t *p, int len, int *pos);

/* ---------------- transfers (receiver side) ---------------- */

/* CRC-32 (IEEE 802.3, reflected, as zlib's crc32): crc = link_crc32(0, data, n), chainable. */
uint32_t link_crc32(uint32_t crc, const uint8_t *data, size_t n);

/* Receives a transfer into the caller's memory, in order only: a chunk at another offset (lost, repeated or
 * early) is ignored and the receiver acknowledges what it has, so the sender resends from there. */
typedef struct {
    uint8_t *mem;      /* destination */
    uint32_t capacity; /* bytes */
    link_xfer_t info;  /* the transfer in progress */
    uint32_t next;     /* bytes received in order */
    uint32_t crc;      /* running CRC of mem[0..next) */
    uint8_t status;    /* LINK_PENDING, LINK_OK (complete and checked), LINK_CRC, LINK_TOO_BIG; 0xFF idle */
} link_rx_t;

void link_rx_init(link_rx_t *r, uint8_t *mem, uint32_t capacity);
/* A BEGIN: starts over (even for the same id). Returns the status to acknowledge. */
uint8_t link_rx_begin(link_rx_t *r, const link_xfer_t *x);
/* A DATA chunk. Returns the status to acknowledge (with r->next); LINK_OK once, when the last byte arrives
 * and the CRC matches. */
uint8_t link_rx_data(link_rx_t *r, uint32_t xfer, uint32_t offset, const uint8_t *data, int n);

/* A transfer of kind LINK_KIND_KIT carries a sample kit in the format of dsp/core/kit.h. */

#endif /* LINK_H */
