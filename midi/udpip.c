/* udpip.c — Ethernet II, ARP, IPv4, ICMP echo and UDP for one port (udpip.h). Every field is read byte by
 * byte in network order after its length is checked; a frame that is not exactly what we expect is
 * dropped and counted. References: RFC 791, 826, 768, 792, 1071, 3927. */
#include "udpip.h"

#define ETH_HDR 14
#define IP_HDR 20
#define UDP_HDR 8
#define ETH_ARP 0x0806u
#define ETH_IPV4 0x0800u
#define IP_ICMP 1u
#define IP_UDP 17u

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static void copy(uint8_t *d, const uint8_t *s, int n)
{
    for (int i = 0; i < n; i++)
        d[i] = s[i];
}

uint16_t udpip_checksum(uint32_t sum, const uint8_t *p, int n)
{
    for (int i = 0; i + 1 < n; i += 2)
        sum += rd16(p + i);
    if (n & 1)
        sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

/* sum of the UDP pseudo-header (source, destination, protocol, length), not folded */
static uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint32_t len)
{
    return (src >> 16) + (src & 0xFFFFu) + (dst >> 16) + (dst & 0xFFFFu) + IP_UDP + len;
}

void udpip_init(udpip_t *u, const uint8_t mac[6], uint32_t ip, uint32_t mask, uint16_t port, uint32_t group,
                udpip_send_fn send, udpip_recv_fn recv, void *ctx)
{
    copy(u->mac, mac, 6);
    u->ip = ip;
    u->mask = mask;
    u->group = group;
    u->port = port;
    u->ip_id = 1;
    u->send = send;
    u->recv = recv;
    u->ctx = ctx;
    u->rx_frames = u->rx_udp = u->rx_dropped = u->rx_bad = u->tx_frames = 0;
}

static void transmit(udpip_t *u, int len)
{
    for (; len < 60; len++) /* minimum Ethernet frame */
        u->tx[len] = 0;
    u->send(u->ctx, u->tx, len);
    u->tx_frames++;
}

static void eth_header(udpip_t *u, const uint8_t *dst, uint32_t type)
{
    copy(u->tx, dst, 6);
    copy(u->tx + 6, u->mac, 6);
    wr16(u->tx + 12, type);
}

/* IPv4 header at tx + ETH_HDR for a payload of n bytes */
static void ip_header(udpip_t *u, uint32_t dst, uint32_t proto, int n)
{
    uint8_t *ip = u->tx + ETH_HDR;
    ip[0] = 0x45; /* version 4, 5 words */
    ip[1] = 0;
    wr16(ip + 2, (uint32_t)(IP_HDR + n));
    wr16(ip + 4, u->ip_id++);
    wr16(ip + 6, 0x4000); /* don't fragment */
    ip[8] = 64;           /* TTL */
    ip[9] = (uint8_t)proto;
    wr16(ip + 10, 0);
    wr32(ip + 12, u->ip);
    wr32(ip + 16, dst);
    wr16(ip + 10, udpip_checksum(0, ip, IP_HDR));
}

static void arp_input(udpip_t *u, const uint8_t *a, int len)
{
    /* Ethernet/IPv4 request for our address */
    if (len < 28 || rd16(a) != 1 || rd16(a + 2) != ETH_IPV4 || a[4] != 6 || a[5] != 4 || rd16(a + 6) != 1 ||
        u->ip == 0 || rd32(a + 24) != u->ip) {
        u->rx_dropped++;
        return;
    }
    eth_header(u, a + 8, ETH_ARP);
    uint8_t *r = u->tx + ETH_HDR;
    wr16(r, 1);
    wr16(r + 2, ETH_IPV4);
    r[4] = 6;
    r[5] = 4;
    wr16(r + 6, 2); /* reply */
    copy(r + 8, u->mac, 6);
    wr32(r + 14, u->ip);
    copy(r + 18, a + 8, 10); /* the asker's MAC and IP */
    transmit(u, ETH_HDR + 28);
}

static void icmp_input(udpip_t *u, const uint8_t *src_mac, uint32_t src, const uint8_t *m, int n)
{
    if (n < 8 || m[0] != 8 || udpip_checksum(0, m, n) != 0 || ETH_HDR + IP_HDR + n > UDPIP_FRAME_MAX) {
        u->rx_dropped++;
        return;
    }
    eth_header(u, src_mac, ETH_IPV4);
    uint8_t *r = u->tx + ETH_HDR + IP_HDR;
    copy(r, m, n);
    r[0] = 0; /* echo reply, same identifier, sequence and data */
    wr16(r + 2, 0);
    wr16(r + 2, udpip_checksum(0, r, n));
    ip_header(u, src, IP_ICMP, n);
    transmit(u, ETH_HDR + IP_HDR + n);
}

/* l2_bcast: the frame went to every station (Ethernet broadcast): then any IPv4 broadcast is ours, whatever the
 * sender's subnet (a host with a fixed address, 192.168.100.255, reaches a link-local board) */
static int ip_for_us(const udpip_t *u, uint32_t dst, int l2_bcast)
{
    if (dst == UDPIP_BROADCAST || (u->group && dst == u->group) || l2_bcast)
        return 1;
    return u->ip && (dst == u->ip || dst == (u->ip | ~u->mask));
}

static void ipv4_input(udpip_t *u, const uint8_t *src_mac, const uint8_t *ip, int len, int l2_bcast)
{
    if (len < IP_HDR || (ip[0] >> 4) != 4) {
        u->rx_bad++;
        return;
    }
    const int hl = (ip[0] & 15) * 4, total = (int)rd16(ip + 2);
    if (hl < IP_HDR || total < hl || total > len || udpip_checksum(0, ip, hl) != 0) {
        u->rx_bad++;
        return;
    }
    const uint32_t src = rd32(ip + 12), dst = rd32(ip + 16);
    if ((rd16(ip + 6) & 0x3FFFu) != 0 || !ip_for_us(u, dst, l2_bcast)) { /* a fragment, or not for us */
        u->rx_dropped++;
        return;
    }
    const uint8_t *pl = ip + hl;
    const int n = total - hl;
    if (ip[9] == IP_ICMP) {
        if (dst == u->ip)
            icmp_input(u, src_mac, src, pl, n);
        return;
    }
    if (ip[9] != IP_UDP || n < UDP_HDR || (int)rd16(pl + 4) < UDP_HDR || (int)rd16(pl + 4) > n) {
        u->rx_dropped++;
        return;
    }
    const int ulen = (int)rd16(pl + 4);
    if (rd16(pl + 6) != 0 && udpip_checksum(pseudo_sum(src, dst, (uint32_t)ulen), pl, ulen) != 0) {
        u->rx_bad++;
        return;
    }
    if (rd16(pl + 2) != u->port) {
        u->rx_dropped++;
        return;
    }
    udpip_peer_t from;
    copy(from.mac, src_mac, 6);
    from.ip = src;
    from.port = (uint16_t)rd16(pl);
    u->rx_udp++;
    u->recv(u->ctx, &from, pl + UDP_HDR, ulen - UDP_HDR);
}

void udpip_input(udpip_t *u, const uint8_t *f, int len)
{
    u->rx_frames++;
    if (len < ETH_HDR) {
        u->rx_bad++;
        return;
    }
    /* our MAC, broadcast, or an IPv4 multicast MAC (01:00:5e): the IP layer decides */
    int ours = 1, bcast = 1;
    for (int i = 0; i < 6; i++) {
        ours &= f[i] == u->mac[i];
        bcast &= f[i] == 0xFF;
    }
    const int mcast = f[0] == 0x01 && f[1] == 0x00 && f[2] == 0x5E;
    if (!ours && !bcast && !mcast) {
        u->rx_dropped++;
        return;
    }
    const uint32_t type = rd16(f + 12);
    if (type == ETH_ARP)
        arp_input(u, f + ETH_HDR, len - ETH_HDR);
    else if (type == ETH_IPV4)
        ipv4_input(u, f + 6, f + ETH_HDR, len - ETH_HDR, bcast);
    else
        u->rx_dropped++;
}

int udpip_send(udpip_t *u, const udpip_peer_t *to, const uint8_t *data, int len)
{
    if (len < 0 || ETH_HDR + IP_HDR + UDP_HDR + len > UDPIP_FRAME_MAX)
        return -1;
    eth_header(u, to->mac, ETH_IPV4);
    uint8_t *ud = u->tx + ETH_HDR + IP_HDR;
    const uint32_t ulen = (uint32_t)(UDP_HDR + len);
    wr16(ud, u->port);
    wr16(ud + 2, to->port);
    wr16(ud + 4, ulen);
    wr16(ud + 6, 0);
    copy(ud + UDP_HDR, data, len);
    uint16_t c = udpip_checksum(pseudo_sum(u->ip, to->ip, ulen), ud, (int)ulen);
    wr16(ud + 6, c ? c : 0xFFFFu); /* 0 means "no checksum" in UDP */
    ip_header(u, to->ip, IP_UDP, (int)ulen);
    transmit(u, ETH_HDR + IP_HDR + (int)ulen);
    return 0;
}

uint32_t udpip_link_local(uint32_t id)
{
    const uint32_t h = (id ^ (id >> 16)) * 0x45D9F3Bu;
    const uint32_t third = 1u + (h >> 8) % 254u; /* 1..254: 169.254.0.x and .255.x are reserved */
    return 0xA9FE0000u | third << 8 | (h & 0xFFu);
}

void udpip_mac_from_id(uint32_t id, uint8_t mac[6])
{
    mac[0] = 0x02; /* locally administered, unicast */
    mac[1] = 0x4F; /* 'O' */
    mac[2] = (uint8_t)(id >> 24);
    mac[3] = (uint8_t)(id >> 16);
    mac[4] = (uint8_t)(id >> 8);
    mac[5] = (uint8_t)id;
}
