/* udpip.h — the smallest IPv4 stack a synth board needs: Ethernet II frames in and out, ARP replies, ICMP echo
 * (ping), and UDP on one port, unicast, broadcast or one multicast group. No TCP, no fragments, no routing,
 * no ARP table: a reply goes back to the MAC and IP the request came from. Static memory only; the driver
 * gives it received frames and sends the frames it builds. RFC 791 (IPv4), 826 (ARP), 768 (UDP), 792 (ICMP),
 * 3927 (link-local addresses). */
#ifndef UDPIP_H
#define UDPIP_H

#include <stdint.h>

#define UDPIP_FRAME_MAX 1514 /* Ethernet frame without the FCS */
#define UDPIP_BROADCAST 0xFFFFFFFFu

typedef struct {
    uint8_t mac[6];
    uint32_t ip; /* host order */
    uint16_t port;
} udpip_peer_t;

typedef void (*udpip_send_fn)(void *ctx, const uint8_t *frame, int len);
typedef void (*udpip_recv_fn)(void *ctx, const udpip_peer_t *from, const uint8_t *data, int len);

typedef struct {
    uint8_t mac[6];
    uint32_t ip, mask; /* host order */
    uint32_t group;    /* multicast group accepted (0 none) */
    uint16_t port;     /* UDP port served */
    uint16_t ip_id;
    udpip_send_fn send;
    udpip_recv_fn recv;
    void *ctx;
    /* counters */
    uint32_t rx_frames, rx_udp, rx_dropped, rx_bad, tx_frames;
    uint8_t tx[UDPIP_FRAME_MAX];
} udpip_t;

void udpip_init(udpip_t *u, const uint8_t mac[6], uint32_t ip, uint32_t mask, uint16_t port, uint32_t group,
                udpip_send_fn send, udpip_recv_fn recv, void *ctx);
/* A received frame (without FCS). Calls recv for each UDP datagram to our port, answers ARP and ping. */
void udpip_input(udpip_t *u, const uint8_t *frame, int len);
/* Sends a UDP datagram from our port to a peer (to->mac all 0xFF and to->ip UDPIP_BROADCAST for a
 * broadcast). Returns 0, or -1 if it does not fit in one frame. */
int udpip_send(udpip_t *u, const udpip_peer_t *to, const uint8_t *data, int len);

/* A link-local address 169.254.1.0 .. 169.254.254.255 (RFC 3927) and a locally administered unicast MAC
 * (02:xx:...), both derived from a board id, so a board keeps them across resets. */
uint32_t udpip_link_local(uint32_t id);
void udpip_mac_from_id(uint32_t id, uint8_t mac[6]);
/* The Internet checksum (RFC 1071) of n bytes, added to sum (pass 0). */
uint16_t udpip_checksum(uint32_t sum, const uint8_t *p, int n);

#endif /* UDPIP_H */
