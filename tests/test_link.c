/* test_link.c — the board link (midi/link.h): datagram encoding round trips, malformed input rejected, CRC-32
 * check value, in-order transfers with loss and corruption, kit directories; the small IPv4 stack
 * (midi/udpip.h): ARP, ping and UDP between two instances, and every kind of frame it must drop; the
 * single-producer queue (midi/linkq.h). */
#include "core/kit.h"
#include "link.h"
#include "linkq.h"
#include "test.h"
#include "udpip.h"

/* ---- two stacks wired to each other through captured frames ---- */

typedef struct {
    uint8_t frame[UDPIP_FRAME_MAX];
    int len, sent;
    udpip_peer_t from;
    uint8_t data[LINK_MAX_DATAGRAM];
    int data_len, received;
} wire_t;

static void wire_send(void *ctx, const uint8_t *f, int len)
{
    wire_t *w = ctx;
    memcpy(w->frame, f, (size_t)len);
    w->len = len;
    w->sent++;
}
static void wire_recv(void *ctx, const udpip_peer_t *from, const uint8_t *d, int len)
{
    wire_t *w = ctx;
    w->from = *from;
    memcpy(w->data, d, (size_t)len);
    w->data_len = len;
    w->received++;
}

static void suite_codec(void)
{
    uint8_t buf[LINK_MAX_DATAGRAM];
    link_header_t h = {LINK_MIDI, 0x12345678u, 42}, g;

    /* header */
    link_put_header(buf, &h);
    CHECK(link_get_header(buf, 20, &g) == 8 && g.type == LINK_MIDI && g.dest == 0x12345678u && g.seq == 42,
          "header round trip");
    CHECK(link_get_header(buf, 11, &g) < 0, "short datagram accepted");
    buf[0] = 'X';
    CHECK(link_get_header(buf, 20, &g) < 0, "bad magic accepted");
    link_put_header(buf, &h);
    buf[2] = 9;
    CHECK(link_get_header(buf, 20, &g) < 0, "other version accepted");
    CHECK(link_for_me(LINK_ALL, 7) && link_for_me(7, 7) && !link_for_me(8, 7), "destination filter");
    const uint8_t uid[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(link_board_id(uid, 12) != 0 && link_board_id(uid, 12) != link_board_id(uid, 11), "board id");

    /* MIDI: two channel messages and a clock, then malformed ones */
    const uint8_t midi[] = {0x90, 60, 100, 0xF8, 0xC1, 5, 0x80, 60, 0};
    int n = link_encode_midi(buf, &h, midi, (int)sizeof midi);
    int len = link_get_header(buf, n, &g), pos = 0, k, count = 0, lens[8];
    const uint8_t *p = buf + LINK_HEADER;
    while ((k = link_midi_next(p, len, &pos)) > 0)
        lens[count++] = k;
    CHECK(k == 0 && count == 4 && lens[0] == 3 && lens[1] == 1 && lens[2] == 2 && lens[3] == 3, "MIDI split");
    const uint8_t cut[] = {0x90, 60}, data_first[] = {60, 0x90}, sysex[] = {0xF0, 1, 0xF7}, bad[] = {0xB0, 0x80, 1};
    pos = 0;
    CHECK(link_midi_next(cut, 2, &pos) < 0, "cut message accepted");
    pos = 0;
    CHECK(link_midi_next(data_first, 2, &pos) < 0, "data byte first accepted");
    pos = 0;
    CHECK(link_midi_next(sysex, 3, &pos) < 0, "SysEx accepted");
    pos = 0;
    CHECK(link_midi_next(bad, 3, &pos) < 0, "data byte >= 0x80 accepted");

    /* parameters */
    const link_param_t prm[3] = {{3, 0.25f}, {700, 1.0f}, {65535, 0.0f}};
    link_param_t got[4];
    uint32_t cat;
    n = link_encode_param(buf, &h, 0xCAFEu, prm, 3);
    len = link_get_header(buf, n, &g);
    CHECK(g.type == LINK_PARAM && link_decode_param(buf + LINK_HEADER, len, &cat, got, 4, &count) == 0 &&
              cat == 0xCAFEu && count == 3 && got[1].id == 700 && got[0].value == 0.25f && got[2].id == 65535,
          "param round trip");
    CHECK(link_decode_param(buf + LINK_HEADER, len - 1, &cat, got, 4, &count) < 0, "truncated params accepted");
    CHECK(link_decode_param(buf + LINK_HEADER, len, &cat, got, 2, &count) < 0, "params beyond max accepted");

    /* preset, the largest one */
    static link_preset_t ps, pr;
    ps.catalog = 0xABCDEF01u;
    ps.engine = 6;
    ps.count = LINK_PARAMS_MAX;
    snprintf(ps.name, sizeof ps.name, "%s", "a_name_longer_than_the_thirty_two_bytes_field");
    for (int i = 0; i < LINK_PARAMS_MAX; i++)
        ps.params[i] = (link_param_t){(uint16_t)(i * 3), (float)i / 256.0f};
    n = link_encode_preset(buf, &h, &ps);
    len = link_get_header(buf, n, &g);
    CHECK(n > 0 && n <= LINK_MAX_DATAGRAM && g.type == LINK_PRESET, "largest preset fits: %d bytes", n);
    CHECK(link_decode_preset(buf + LINK_HEADER, len, &pr) == 0 && pr.catalog == ps.catalog && pr.engine == 6 &&
              pr.count == LINK_PARAMS_MAX && pr.params[179].id == 537 && pr.params[100].value == 100.0f / 256.0f &&
              strlen(pr.name) == LINK_NAME - 1 && !strncmp(pr.name, ps.name, LINK_NAME - 1),
          "preset round trip (name cut to 31 bytes)");
    CHECK(link_decode_preset(buf + LINK_HEADER, len - 6, &pr) < 0, "truncated preset accepted");

    /* select, hello, ack */
    char name[LINK_NAME];
    n = link_encode_select(buf, &h, "mono_d_lead");
    len = link_get_header(buf, n, &g);
    CHECK(link_decode_select(buf + LINK_HEADER, len, name) == 0 && !strcmp(name, "mono_d_lead"), "select");
    link_hello_t he = {0}, hr;
    he.id = 99;
    he.uid[11] = 7;
    he.ip = 0xA9FE0102u;
    he.catalog = 5;
    he.preset = -2;
    he.preset_count = 75;
    he.load_pct = 23;
    he.peak_pct = 41;
    he.overruns = 3;
    he.kit_size = 123456;
    snprintf(he.board, sizeof he.board, "h745");
    snprintf(he.playing, sizeof he.playing, "piano_tension");
    n = link_encode_hello(buf, &h, &he);
    len = link_get_header(buf, n, &g);
    CHECK(link_decode_hello(buf + LINK_HEADER, len, &hr) == 0 && hr.id == 99 && hr.uid[11] == 7 &&
              hr.ip == he.ip && hr.preset == -2 && hr.preset_count == 75 && hr.peak_pct == 41 &&
              hr.overruns == 3 && hr.kit_size == 123456 && !strcmp(hr.board, "h745") &&
              !strcmp(hr.playing, "piano_tension"),
          "hello round trip");
    uint32_t seq;
    uint8_t st;
    n = link_encode_ack(buf, &h, 77, LINK_BUSY);
    len = link_get_header(buf, n, &g);
    CHECK(link_decode_ack(buf + LINK_HEADER, len, &seq, &st) == 0 && seq == 77 && st == LINK_BUSY, "ack");
}

static void suite_transfer(void)
{
    /* CRC-32 check value (the "123456789" test of every CRC catalogue), and chaining */
    const uint8_t digits[] = "123456789";
    CHECK(link_crc32(0, digits, 9) == 0xCBF43926u, "CRC-32 check value %08x", (unsigned)link_crc32(0, digits, 9));
    CHECK(link_crc32(link_crc32(0, digits, 4), digits + 4, 5) == 0xCBF43926u, "CRC-32 chaining");

    static uint8_t src[5000], dst[8192];
    for (int i = 0; i < 5000; i++)
        src[i] = (uint8_t)(i * 7 + (i >> 8));
    link_rx_t r;
    link_rx_init(&r, dst, sizeof dst);
    link_xfer_t x = {11, LINK_KIND_KIT, 5000, link_crc32(0, src, 5000), "kit"};
    CHECK(link_rx_data(&r, 11, 0, src, 100) == LINK_BAD, "data before begin accepted");
    CHECK(link_rx_begin(&r, &x) == LINK_PENDING, "begin");
    /* chunk 0, chunk 2 (early: chunk 1 lost), chunk 0 again (duplicate), then 1, 2, 3, 4 */
    CHECK(link_rx_data(&r, 11, 0, src, 1024) == LINK_PENDING && r.next == 1024, "chunk 0");
    CHECK(link_rx_data(&r, 11, 2048, src + 2048, 1024) == LINK_PENDING && r.next == 1024, "early chunk taken");
    CHECK(link_rx_data(&r, 11, 0, src, 1024) == LINK_PENDING && r.next == 1024, "duplicate chunk taken");
    CHECK(link_rx_data(&r, 12, 1024, src + 1024, 1024) == LINK_BAD && r.next == 1024, "other transfer taken");
    uint8_t s = 0;
    for (uint32_t off = 1024; off < 5000; off += 1024)
        s = link_rx_data(&r, 11, off, src + off, 5000 - off < 1024 ? (int)(5000 - off) : 1024);
    CHECK(s == LINK_OK && r.next == 5000 && !memcmp(src, dst, 5000), "transfer complete and checked");
    CHECK(link_rx_data(&r, 11, 4096, src + 4096, 904) == LINK_OK, "late duplicate changes the outcome");

    /* corruption: one byte flipped on the way */
    link_rx_begin(&r, &x);
    for (uint32_t off = 0; off < 5000; off += 1024) {
        uint8_t chunk[1024];
        const int n = 5000 - off < 1024 ? (int)(5000 - off) : 1024;
        memcpy(chunk, src + off, (size_t)n);
        if (off == 3072)
            chunk[17] ^= 0x10;
        s = link_rx_data(&r, 11, off, chunk, n);
    }
    CHECK(s == LINK_CRC, "corrupted transfer accepted");
    /* too big, and data running past the announced size */
    link_xfer_t big = {12, LINK_KIND_KIT, 9000, 0, "big"};
    CHECK(link_rx_begin(&r, &big) == LINK_TOO_BIG && link_rx_data(&r, 12, 0, src, 10) == LINK_TOO_BIG,
          "transfer larger than the memory accepted");
    link_xfer_t small = {13, LINK_KIND_KIT, 10, 0, "small"};
    link_rx_begin(&r, &small);
    CHECK(link_rx_data(&r, 13, 0, src, 20) == LINK_BAD, "data past the end accepted");

    /* kits */
    static uint8_t kit[4096];
    kit_entry_t e[2] = {{"kick", 36, 0, 0, 100}, {"snare", 38, 0, 0, 50}};
    const int dir = KIT_HEADER + 2 * KIT_ENTRY;
    e[0].offset = (uint32_t)dir;
    e[1].offset = (uint32_t)dir + 200;
    CHECK(kit_write_directory(kit, e, 2) == dir, "kit directory size");
    const uint32_t size = (uint32_t)dir + 300;
    kit_entry_t f;
    CHECK(kit_count(kit, size) == 2, "kit valid");
    kit_entry(kit, 1, &f);
    CHECK(!strcmp(f.name, "snare") && f.note == 38 && f.offset == (uint32_t)dir + 200 && f.frames == 50, "kit entry");
    CHECK(kit_count(kit, size - 2) < 0, "sample past the end of the kit accepted");
    e[1].offset++;
    kit_write_directory(kit, e, 2);
    CHECK(kit_count(kit, size) < 0, "odd sample offset accepted");
    e[1].offset = 4;
    kit_write_directory(kit, e, 2);
    CHECK(kit_count(kit, size) < 0, "sample inside the directory accepted");
    kit[0] = 'X';
    CHECK(kit_count(kit, size) < 0, "bad kit magic accepted");
}

static void suite_udpip(void)
{
    static udpip_t board, host;
    static wire_t wb, wh; /* frames sent by the board / by the host */
    uint8_t bmac[6], hmac[6] = {0x02, 0, 0, 0, 0, 0x99};
    const uint32_t id = 0x1234ABCDu, bip = udpip_link_local(id), hip = 0xA9FE6363u;
    udpip_mac_from_id(id, bmac);
    CHECK((bip >> 16) == 0xA9FEu && ((bip >> 8) & 0xFFu) >= 1 && ((bip >> 8) & 0xFFu) <= 254, "link-local address");
    CHECK(bmac[0] == 0x02 && bmac[5] == 0xCD, "locally administered MAC");
    udpip_init(&board, bmac, bip, 0xFFFF0000u, LINK_PORT, LINK_GROUP, wire_send, wire_recv, &wb);
    udpip_init(&host, hmac, hip, 0xFFFF0000u, 50000, 0, wire_send, wire_recv, &wh);

    /* ARP: who has the board's address? */
    uint8_t f[UDPIP_FRAME_MAX] = {0};
    memset(f, 0xFF, 6);
    memcpy(f + 6, hmac, 6);
    f[12] = 0x08, f[13] = 0x06;
    const uint8_t arp[28] = {0, 1, 8, 0, 6, 4, 0, 1, 0x02, 0, 0, 0, 0, 0x99, 0xA9, 0xFE, 0x63, 0x63,
                             0, 0, 0, 0, 0, 0, (uint8_t)(bip >> 24), (uint8_t)(bip >> 16), (uint8_t)(bip >> 8),
                             (uint8_t)bip};
    memcpy(f + 14, arp, 28);
    udpip_input(&board, f, 42);
    CHECK(wb.sent == 1 && wb.len == 60 && !memcmp(wb.frame, hmac, 6) && wb.frame[12] == 0x08 &&
              wb.frame[13] == 0x06 && wb.frame[21] == 2 && !memcmp(wb.frame + 22, bmac, 6) &&
              !memcmp(wb.frame + 32, hmac, 6),
          "ARP reply");
    f[41] ^= 1; /* another address */
    udpip_input(&board, f, 42);
    CHECK(wb.sent == 1, "ARP for another address answered");

    /* ping: echo request with 5 bytes of data (odd length) */
    memcpy(f, bmac, 6);
    f[12] = 0x08, f[13] = 0x00;
    uint8_t *ip = f + 14, *icmp = ip + 20;
    const int icmp_len = 13;
    memset(ip, 0, 20);
    ip[0] = 0x45, ip[2] = 0, ip[3] = 20 + icmp_len, ip[8] = 64, ip[9] = 1;
    ip[12] = 0xA9, ip[13] = 0xFE, ip[14] = 0x63, ip[15] = 0x63;
    ip[16] = (uint8_t)(bip >> 24), ip[17] = (uint8_t)(bip >> 16), ip[18] = (uint8_t)(bip >> 8), ip[19] = (uint8_t)bip;
    const uint16_t ipc = udpip_checksum(0, ip, 20);
    ip[10] = (uint8_t)(ipc >> 8), ip[11] = (uint8_t)ipc;
    const uint8_t echo[13] = {8, 0, 0, 0, 0x12, 0x34, 0, 1, 'h', 'e', 'l', 'l', 'o'};
    memcpy(icmp, echo, 13);
    const uint16_t ic = udpip_checksum(0, icmp, icmp_len);
    icmp[2] = (uint8_t)(ic >> 8), icmp[3] = (uint8_t)ic;
    udpip_input(&board, f, 14 + 20 + icmp_len);
    const uint8_t *rip = wb.frame + 14, *ricmp = rip + 20;
    CHECK(wb.sent == 2 && rip[9] == 1 && ricmp[0] == 0 && udpip_checksum(0, rip, 20) == 0 &&
              udpip_checksum(0, ricmp, icmp_len) == 0 && !memcmp(ricmp + 4, echo + 4, 9) &&
              !memcmp(rip + 16, ip + 12, 4),
          "ping reply");
    ip[10] ^= 1; /* corrupted IP header */
    udpip_input(&board, f, 14 + 20 + icmp_len);
    CHECK(wb.sent == 2 && board.rx_bad == 1, "corrupted IP header answered");

    /* UDP host -> board, unicast, then the board's reply to the sender */
    udpip_peer_t to = {{0}, bip, LINK_PORT};
    memcpy(to.mac, bmac, 6);
    const uint8_t msg[] = "note on";
    udpip_send(&host, &to, msg, (int)sizeof msg);
    udpip_input(&board, wh.frame, wh.len);
    CHECK(wb.received == 1 && wb.data_len == (int)sizeof msg && !memcmp(wb.data, msg, sizeof msg) &&
              wb.from.ip == hip && wb.from.port == 50000 && !memcmp(wb.from.mac, hmac, 6),
          "UDP unicast received");
    udpip_send(&board, &wb.from, (const uint8_t *)"ack", 3);
    udpip_input(&host, wb.frame, wb.len);
    CHECK(wh.received == 1 && wh.data_len == 3 && wh.from.port == LINK_PORT && wh.from.ip == bip, "reply received");

    /* broadcast (limited, directed, another subnet's) and the multicast group */
    udpip_peer_t bc = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, UDPIP_BROADCAST, LINK_PORT};
    udpip_send(&host, &bc, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    bc.ip = 0xA9FEFFFFu;
    udpip_send(&host, &bc, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    bc.ip = 0xC0A864FFu; /* 192.168.100.255: the directed broadcast of a host with a fixed address */
    udpip_send(&host, &bc, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    udpip_peer_t mc = {{0x01, 0x00, 0x5E, 0x7F, 0x47, 0x01}, LINK_GROUP, LINK_PORT};
    udpip_send(&host, &mc, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    CHECK(wb.received == 5, "broadcast or multicast not received (%d)", wb.received);

    /* dropped: another port, another unicast IP, another MAC, a fragment, a bad UDP checksum */
    const int before = wb.received;
    to.port = 9999;
    udpip_send(&host, &to, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    to.port = LINK_PORT;
    to.ip = bip + 1;
    udpip_send(&host, &to, msg, 4);
    udpip_input(&board, wh.frame, wh.len);
    to.ip = bip;
    udpip_send(&host, &to, msg, 4);
    wh.frame[5] ^= 1;
    udpip_input(&board, wh.frame, wh.len);
    udpip_send(&host, &to, msg, 4);
    wh.frame[14 + 6] |= 0x20; /* more fragments, with a valid header checksum */
    wh.frame[14 + 10] = wh.frame[14 + 11] = 0;
    {
        const uint16_t c = udpip_checksum(0, wh.frame + 14, 20);
        wh.frame[14 + 10] = (uint8_t)(c >> 8), wh.frame[14 + 11] = (uint8_t)c;
    }
    udpip_input(&board, wh.frame, wh.len);
    udpip_send(&host, &to, msg, 4);
    wh.frame[14 + 20 + 8] ^= 0xFF; /* payload changed, checksum not */
    udpip_input(&board, wh.frame, wh.len);
    udpip_send(&host, &to, msg, 4);
    udpip_input(&board, wh.frame, 14 + 20 + 4); /* truncated */
    CHECK(wb.received == before, "a frame to drop was received");
    CHECK(udpip_send(&host, &to, f, UDPIP_FRAME_MAX) < 0, "oversized datagram sent");
}

static void suite_queue(void)
{
    static linkq_t q;
    linkq_init(&q);
    linkq_event_t e = {0}, o;
    CHECK(!linkq_pop(&q, &o), "empty queue popped");
    int pushed = 0;
    for (int i = 0; i < 300; i++) {
        e.kind = LINKQ_PARAM;
        e.id = (uint16_t)i;
        pushed += linkq_push(&q, &e);
    }
    CHECK(pushed == (int)LINKQ_LEN && q.lost == 300 - LINKQ_LEN, "capacity");
    int ok = 1;
    for (int i = 0; i < 100; i++)
        ok &= linkq_pop(&q, &o) && o.id == i;
    for (int i = 0; i < 50; i++) { /* wrap around */
        e.id = (uint16_t)(1000 + i);
        ok &= linkq_push(&q, &e);
    }
    for (int i = 100; i < (int)LINKQ_LEN; i++)
        ok &= linkq_pop(&q, &o) && o.id == i;
    for (int i = 0; i < 50; i++)
        ok &= linkq_pop(&q, &o) && o.id == 1000 + i;
    CHECK(ok && !linkq_pop(&q, &o), "FIFO order across the wrap");
}

void suite_link(void)
{
    suite_codec();
    suite_transfer();
    suite_udpip();
    suite_queue();
}
