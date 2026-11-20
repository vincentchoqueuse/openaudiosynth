/* net.h — the board on the network (the M4's Ethernet: LAN8742 PHY in MII mode, docs/link.md): a link-local
 * address and a MAC derived from the MCU's unique id, the small UDP stack (midi/udpip.h) fed from the
 * Ethernet interrupt, and the board link (midi/link.h): MIDI and parameters go straight to the M7 through
 * the event queue (shared.h, LINKQ), presets through the mailbox, sample kits into the SDRAM. */
#ifndef NET_H
#define NET_H

#include <stdint.h>

typedef struct {
    int up;               /* cable connected, link negotiated */
    int phy_ok;           /* the PHY answered at start-up */
    uint32_t ip;          /* host order */
    uint32_t id;          /* board id (link_board_id) */
    uint32_t rx_datagrams, rx_bad, events_lost, tx_dropped;
    uint32_t kit_size, kit_count;
} net_status_t;

/* Starts Ethernet (after the SDRAM is set up when sdram_ok: kits are accepted only then). */
void net_init(int sdram_ok);
/* Main loop: link changes (checked every 250 ms). */
void net_poll(void);
/* Main loop: a preset the network asked for, to post to the M7 like a touch (an index in presets[], or
 * SHARED_PRESET_NET for the one in NET_PRESET), or -1; then net_request_posted once req_seq is bumped (the
 * network stays busy in between, and until the M7 has applied it). */
int net_request(void);
void net_request_posted(void);
/* For the screen. */
net_status_t net_status(void);

#endif /* NET_H */
