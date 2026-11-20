/* net.c — the board on the network (net.h, docs/link.md). The HAL's Ethernet driver in MII mode with the
 * board's LAN8742 PHY (ST's component driver), received frames handled inside the Ethernet interrupt by the
 * small UDP stack (midi/udpip.h) and the board link (midi/link.h): a note reaches the M7's event queue a few
 * microseconds after its frame, whatever the screen is drawing. The main loop only watches the cable and
 * posts presets to the M7.
 * The Ethernet DMA cannot reach the M4's 0x10000000 alias of the D2 SRAM: descriptors and buffers are given
 * to it through their 0x30000000 address (same memory, the M4 has no data cache). */
#include "net.h"

#include <string.h>

#include "core/kit.h"
#include "lan8742.h"
#include "main.h"
#include "presets.h"
#include "shared.h"
#include "synth.h"
#include "udpip.h"

#define NET_RX_BUF 1536u           /* a whole frame per buffer (multiple of 32) */
#define NET_RX_POOL (ETH_RX_DESC_CNT + 2u)
#define NET_LINK_MS 250u
#define NET_KIT_BANK0 0xD0100000u  /* SDRAM after the display's frames (gfx.c): two banks of 7.5 MB */
#define NET_KIT_BANK_SIZE 0x00780000u

static ETH_HandleTypeDef heth;
static ETH_TxPacketConfigTypeDef tx_config;
static lan8742_Object_t phy;
static ETH_DMADescTypeDef rx_desc[ETH_RX_DESC_CNT] __attribute__((aligned(32)));
static ETH_DMADescTypeDef tx_desc[ETH_TX_DESC_CNT] __attribute__((aligned(32)));
static uint8_t rx_pool[NET_RX_POOL][NET_RX_BUF] __attribute__((aligned(32)));
static uint8_t rx_free[NET_RX_POOL]; /* 1: buffer free */
static uint8_t tx_pool[ETH_TX_DESC_CNT][NET_RX_BUF] __attribute__((aligned(32)));
static volatile uint8_t tx_busy[ETH_TX_DESC_CNT]; /* 1: handed to the DMA, until HAL_ETH_TxFreeCallback */
static udpip_t ip;
static uint8_t out[LINK_MAX_DATAGRAM];
static link_rx_t kit_rx;
static int kit_bank;               /* bank the next kit is written to */

static volatile int request = -1;  /* preset for the main loop to post (net_request), until it is posted */
static uint32_t last_link_check;
static net_status_t st;

/* ---------------- memory seen by the Ethernet DMA ---------------- */

/* the D2 SRAM at the address the DMA uses (0x10000000 -> 0x30000000) */
static void *dma_addr(void *p)
{
    const uintptr_t a = (uintptr_t)p;
    return (void *)(a >= 0x10000000u && a < 0x10048000u ? a + 0x20000000u : a);
}

/* ---------------- HAL glue ---------------- */

void HAL_ETH_MspInit(ETH_HandleTypeDef *h)
{
    (void)h;
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    /* MII pins of the STM32H745I-DISCO (its CubeMX board file) */
    static const struct {
        GPIO_TypeDef *port;
        uint32_t pins;
    } map[] = {
        {GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7},                           /* RX_CLK, MDIO, RX_DV */
        {GPIOB, GPIO_PIN_0 | GPIO_PIN_1},                                        /* RXD2, RXD3 */
        {GPIOC, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5}, /* MDC, TXD2, TX_CLK, RXD0, RXD1 */
        {GPIOE, GPIO_PIN_2},                                                     /* TXD3 */
        {GPIOG, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13},                        /* TX_EN, TXD1, TXD0 */
        {GPIOH, GPIO_PIN_2 | GPIO_PIN_3},                                        /* CRS, COL */
        {GPIOI, GPIO_PIN_10},                                                    /* RX_ER */
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        GPIO_InitTypeDef g = {0};
        g.Pin = map[i].pins;
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(map[i].port, &g);
    }
    HAL_NVIC_SetPriority(ETH_IRQn, NET_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

void ETH_IRQHandler(void) { HAL_ETH_IRQHandler(&heth); }

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    *buff = NULL;
    for (unsigned i = 0; i < NET_RX_POOL; i++)
        if (rx_free[i]) {
            rx_free[i] = 0;
            *buff = dma_addr(rx_pool[i]);
            return;
        }
}

static uint16_t rx_len;
static int rx_parts;

/* one frame = one buffer (NET_RX_BUF holds the largest); a frame in several parts is dropped */
static void rx_release(uint8_t *buff)
{
    for (unsigned i = 0; i < NET_RX_POOL; i++)
        if (dma_addr(rx_pool[i]) == buff)
            rx_free[i] = 1;
}

/* one frame = one buffer (NET_RX_BUF holds the largest); the parts after the first of a longer one go back
 * to the pool at once, and the frame is dropped */
void HAL_ETH_RxLinkCallback(void **start, void **end, uint8_t *buff, uint16_t len)
{
    if (*start == NULL) {
        *start = *end = buff;
        rx_len = len;
        rx_parts = 1;
    } else {
        rx_parts++;
        rx_release(buff);
    }
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *h)
{
    void *frame = NULL;
    while (HAL_ETH_ReadData(h, &frame) == HAL_OK && frame) {
        if (rx_parts == 1)
            udpip_input(&ip, frame, rx_len);
        else
            st.rx_bad++;
        rx_release(frame);
        frame = NULL;
    }
}

/* Transmission never waits (the Ethernet interrupt outranks the tick: a timeout could not run out): the frame
 * is copied into a free buffer of the pool and handed to the DMA; the buffer comes back when the DMA is done
 * (HAL_ETH_TxFreeCallback, from HAL_ETH_ReleaseTxPacket). No free buffer: the frame is dropped and counted. */
void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    for (unsigned i = 0; i < ETH_TX_DESC_CNT; i++)
        if ((uint32_t *)dma_addr(tx_pool[i]) == buff)
            tx_busy[i] = 0;
}

void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *h) { HAL_ETH_ReleaseTxPacket(h); }

static void frame_send(void *ctx, const uint8_t *frame, int len)
{
    (void)ctx;
    if (len > (int)NET_RX_BUF || !st.up)
        return;
    HAL_ETH_ReleaseTxPacket(&heth);
    unsigned i = 0;
    while (i < ETH_TX_DESC_CNT && tx_busy[i])
        i++;
    if (i == ETH_TX_DESC_CNT) {
        st.tx_dropped++;
        return;
    }
    uint8_t *b = dma_addr(tx_pool[i]);
    memcpy(b, frame, (size_t)len);
    static ETH_BufferTypeDef desc;
    desc.buffer = b;
    desc.len = (uint32_t)len;
    desc.next = NULL;
    tx_config.Length = (uint32_t)len;
    tx_config.TxBuffer = &desc;
    tx_config.pData = (void *)b;
    tx_busy[i] = 1;
    if (HAL_ETH_Transmit_IT(&heth, &tx_config) != HAL_OK) {
        tx_busy[i] = 0;
        st.tx_dropped++;
    }
}

/* ---------------- PHY ---------------- */

static int32_t phy_io_init(void) { return 0; }
static int32_t phy_io_deinit(void) { return 0; }
static int32_t phy_read(uint32_t addr, uint32_t reg, uint32_t *val)
{
    return HAL_ETH_ReadPHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}
static int32_t phy_write(uint32_t addr, uint32_t reg, uint32_t val)
{
    return HAL_ETH_WritePHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}
static int32_t phy_tick(void) { return (int32_t)HAL_GetTick(); }

/* ---------------- the board link ---------------- */

/* the MCU's unique id, given by the M7 (shared.h: the M4 faults reading it); 0 if the M7 never gives it */
static uint32_t uid_w[3];

static void uid_fetch(void)
{
    for (uint32_t t0 = HAL_GetTick(); HAL_GetTick() - t0 < 3000u;) {
        if (SHARED->uid_ok == SHARED_UID_OK(SHARED->uid[0])) {
            __DMB();
            for (int i = 0; i < 3; i++)
                uid_w[i] = SHARED->uid[i];
            return;
        }
        HAL_Delay(1);
    }
}

static uint32_t uid_bytes(uint8_t uid[12])
{
    const uint32_t *w = uid_w;
    for (int i = 0; i < 12; i++)
        uid[i] = (uint8_t)(w[i / 4] >> (8 * (i % 4)));
    return link_board_id(uid, 12);
}

static void reply(const udpip_peer_t *to, int n)
{
    if (n > 0)
        udpip_send(&ip, to, out, n);
}

static void hello(const udpip_peer_t *to, uint32_t seq)
{
    link_hello_t m;
    memset(&m, 0, sizeof m);
    m.id = uid_bytes(m.uid);
    m.ip = ip.ip;
    m.catalog = preset_catalog;
    const int32_t p = SHARED->m7_magic == SHARED_MAGIC_M7 ? SHARED->preset : -1;
    m.preset = (int16_t)(p >= 0 && p < preset_count ? p : p == SHARED_PLAYING_NET ? SHARED_PLAYING_NET : -1);
    m.preset_count = (uint16_t)preset_count;
    m.load_pct = (uint8_t)(SHARED->load_avg_pct + 0.5f);
    m.peak_pct = (uint8_t)(SHARED->load_peak_pct + 0.5f);
    m.overruns = SHARED->overruns;
    m.kit_size = SHARED->kit_size;
    strcpy(m.board, "h745");
    if (m.preset >= 0)
        strncpy(m.playing, presets[m.preset].name, LINK_NAME - 1);
    else if (m.preset == SHARED_PLAYING_NET)
        strncpy(m.playing, NET_PRESET->name, LINK_NAME - 1);
    const link_header_t h = {0, st.id, seq};
    reply(to, link_encode_hello(out, &h, &m));
}

/* a request is in flight until the M7 has acknowledged it */
/* a request is in flight until the M7 has applied it (done_seq: it reads NET_PRESET until then) */
static int busy(void) { return request >= 0 || SHARED->done_seq != SHARED->req_seq; }

__attribute__((unused)) static void push_midi(const uint8_t *p, int len)
{
    int pos = 0, n, start = 0;
    while ((n = link_midi_next(p, len, &pos)) > 0) {
        linkq_event_t e = {0};
        e.kind = LINKQ_MIDI;
        e.len = (uint8_t)n;
        for (int i = 0; i < n; i++)
            e.midi[i] = p[start + i];
        if (!linkq_push(LINKQ, &e))
            st.events_lost++;
        start = pos;
    }
    if (n < 0)
        st.rx_bad++;
}

static void push_params(const uint8_t *p, int len)
{
    static link_param_t prm[255];
    uint32_t catalog;
    int count;
    if (link_decode_param(p, len, &catalog, prm, 255, &count) < 0 || catalog != preset_catalog) {
        st.rx_bad++;
        return;
    }
    for (int i = 0; i < count; i++) {
        linkq_event_t e = {0};
        e.kind = LINKQ_PARAM;
        e.id = prm[i].id;
        e.value = prm[i].value < 0.0f ? 0.0f : prm[i].value > 1.0f ? 1.0f : prm[i].value;
        if (!linkq_push(LINKQ, &e))
            st.events_lost++;
    }
}

static uint8_t on_select(const uint8_t *p, int len)
{
    char name[LINK_NAME];
    if (link_decode_select(p, len, name) < 0)
        return LINK_BAD;
    for (int i = 0; i < preset_count; i++)
        if (!strcmp(presets[i].name, name)) {
            if (busy())
                return LINK_BUSY;
            request = i;
            return LINK_OK;
        }
    return LINK_UNKNOWN;
}

static uint8_t on_preset(const uint8_t *p, int len)
{
    static link_preset_t m;
    if (link_decode_preset(p, len, &m) < 0 || m.engine >= SYNTH_ENGINE_COUNT)
        return LINK_BAD;
    if (m.catalog != preset_catalog)
        return LINK_CATALOG;
    if (busy())
        return LINK_BUSY;
    net_preset_t *d = NET_PRESET;
    d->engine = m.engine;
    d->count = m.count;
    memcpy(d->name, m.name, LINK_NAME);
    for (int i = 0; i < m.count; i++) {
        d->params[i].id = m.params[i].id;
        d->params[i].value = m.params[i].value;
    }
    request = (int)SHARED_PRESET_NET;
    return LINK_OK;
}

static uint8_t on_xfer(uint8_t type, const uint8_t *p, int len, uint32_t *xfer)
{
    if (type == LINK_XFER_BEGIN) {
        link_xfer_t x;
        if (link_decode_xfer_begin(p, len, &x) < 0)
            return LINK_BAD;
        *xfer = x.xfer;
        if (x.kind != LINK_KIND_KIT || !kit_rx.capacity)
            return LINK_TOO_BIG; /* no SDRAM, or a kind this board does not keep */
        if (SHARED->m7_magic == SHARED_MAGIC_M7 && SHARED->kit_ack_seq != SHARED->kit_seq)
            return LINK_BUSY; /* the M7 has not let go of the bank this kit would overwrite yet */
        kit_rx.mem = (uint8_t *)(NET_KIT_BANK0 + (uint32_t)kit_bank * NET_KIT_BANK_SIZE);
        return link_rx_begin(&kit_rx, &x);
    }
    uint32_t off;
    const uint8_t *d;
    int n;
    if (link_decode_xfer_data(p, len, xfer, &off, &d, &n) < 0)
        return LINK_BAD;
    const uint32_t before = kit_rx.next;
    uint8_t s = link_rx_data(&kit_rx, *xfer, off, d, n);
    if (s == LINK_OK && before != kit_rx.next) { /* just completed: check the kit, then publish it */
        __DSB(); /* the SDRAM writes are done */
        const int count = kit_count(kit_rx.mem, kit_rx.info.size);
        if (count < 0) {
            kit_rx.status = LINK_BAD;
            return LINK_BAD;
        }
        SHARED->kit_addr = (uint32_t)(uintptr_t)kit_rx.mem;
        SHARED->kit_size = kit_rx.info.size;
        SHARED->kit_count = (uint32_t)count;
        __DMB(); /* the kit before the sequence number that publishes it */
        SHARED->kit_seq++;
        st.kit_size = kit_rx.info.size;
        st.kit_count = (uint32_t)count;
        kit_bank ^= 1; /* the next kit goes to the other bank: the M7 may be playing this one */
    }
    return s;
}

/* a datagram for our port (Ethernet interrupt) */
static void on_datagram(void *ctx, const udpip_peer_t *from, const uint8_t *d, int len)
{
    (void)ctx;
    link_header_t h;
    const int n = link_get_header(d, len, &h);
    if (n < 0 || !link_for_me(h.dest, st.id)) {
        if (n < 0)
            st.rx_bad++;
        return;
    }
    st.rx_datagrams++;
    SHARED->net_alive++; /* the M7 stops every note if this stops moving after MIDI (LINK_SILENCE_MS) */
    const uint8_t *p = d + LINK_HEADER;
    const link_header_t rh = {0, st.id, h.seq};
    switch (h.type) {
    case LINK_DISCOVER:
        hello(from, h.seq);
        break;
    case LINK_MIDI:
#if BOARD_MIDI_NET
        push_midi(p, n);
        SHARED->net_midi++;
#endif /* otherwise MIDI is taken from USB only (net.h) */
        break;
    case LINK_HEARTBEAT:
        break; /* counted in net_alive */
    case LINK_PARAM:
        push_params(p, n);
        break;
    case LINK_SELECT:
        reply(from, link_encode_ack(out, &rh, h.seq, on_select(p, n)));
        break;
    case LINK_PRESET:
        reply(from, link_encode_ack(out, &rh, h.seq, on_preset(p, n)));
        break;
    case LINK_XFER_BEGIN:
    case LINK_XFER_DATA: {
        uint32_t xfer = 0;
        const uint8_t s = on_xfer(h.type, p, n, &xfer);
        reply(from, link_encode_xfer_ack(out, &rh, xfer, kit_rx.next, s));
        break;
    }
    default:
        break;
    }
}

/* ---------------- link state ---------------- */

static void link_start(int32_t state)
{
    ETH_MACConfigTypeDef mac;
    HAL_ETH_GetMACConfig(&heth, &mac);
    mac.DuplexMode = state == LAN8742_STATUS_100MBITS_FULLDUPLEX || state == LAN8742_STATUS_10MBITS_FULLDUPLEX
                         ? ETH_FULLDUPLEX_MODE
                         : ETH_HALFDUPLEX_MODE;
    mac.Speed = state == LAN8742_STATUS_10MBITS_FULLDUPLEX || state == LAN8742_STATUS_10MBITS_HALFDUPLEX
                    ? ETH_SPEED_10M
                    : ETH_SPEED_100M;
    HAL_ETH_SetMACConfig(&heth, &mac);
    HAL_ETH_Start_IT(&heth);
    st.up = 1;
    SHARED->net_up = 1;
    /* tell the network we are here (a hub waiting for boards sees this one at once) */
    const udpip_peer_t all = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, UDPIP_BROADCAST, LINK_PORT};
    HAL_NVIC_DisableIRQ(ETH_IRQn);
    hello(&all, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

static void link_check(void)
{
    if (!st.phy_ok)
        return;
    const int32_t state = LAN8742_GetLinkState(&phy);
    const int up = state >= LAN8742_STATUS_100MBITS_FULLDUPLEX && state <= LAN8742_STATUS_10MBITS_HALFDUPLEX;
    if (up && !st.up) {
        link_start(state);
    } else if (!up && st.up) {
        HAL_NVIC_DisableIRQ(ETH_IRQn);
        HAL_ETH_Stop_IT(&heth);
        HAL_NVIC_EnableIRQ(ETH_IRQn);
        st.up = 0;
        SHARED->net_up = 0; /* the M7 stops the notes the network was holding */
    }
}

/* ---------------- interface ---------------- */

void net_init(int sdram_ok)
{
    uint8_t uid[12], mac[6];
    uid_fetch();
    st.id = uid_bytes(uid);
    st.ip = udpip_link_local(st.id);
    udpip_mac_from_id(st.id, mac);
    udpip_init(&ip, mac, st.ip, 0xFFFF0000u, LINK_PORT, LINK_GROUP, frame_send, on_datagram, NULL);
    link_rx_init(&kit_rx, (uint8_t *)NET_KIT_BANK0, sdram_ok ? NET_KIT_BANK_SIZE : 0u);
    LINKQ->lost = 0; /* the producer's counter (SRAM4 is not cleared at power-up) */
    SHARED->net_up = 0;
    for (unsigned i = 0; i < NET_RX_POOL; i++)
        rx_free[i] = 1;
    static uint8_t mac_addr[6];
    memcpy(mac_addr, mac, 6);

    heth.Instance = ETH;
    heth.Init.MACAddr = mac_addr;
    heth.Init.MediaInterface = HAL_ETH_MII_MODE;
    heth.Init.TxDesc = dma_addr(tx_desc);
    heth.Init.RxDesc = dma_addr(rx_desc);
    heth.Init.RxBuffLen = NET_RX_BUF;
    if (HAL_ETH_Init(&heth) != HAL_OK)
        return;
    memset(&tx_config, 0, sizeof tx_config);
    tx_config.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    tx_config.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    /* broadcast and every multicast group: udpip.c keeps what is ours */
    ETH_MACFilterConfigTypeDef f;
    HAL_ETH_GetMACFilterConfig(&heth, &f);
    f.PassAllMulticast = ENABLE;
    f.BroadcastFilter = DISABLE;
    HAL_ETH_SetMACFilterConfig(&heth, &f);

    lan8742_IOCtx_t io = {phy_io_init, phy_io_deinit, phy_write, phy_read, phy_tick};
    LAN8742_RegisterBusIO(&phy, &io);
    st.phy_ok = LAN8742_Init(&phy) == LAN8742_STATUS_OK;
    link_check();
    last_link_check = HAL_GetTick();
}

void net_poll(void)
{
    const uint32_t now = HAL_GetTick();
    if (now - last_link_check >= NET_LINK_MS) {
        last_link_check = now;
        link_check();
    }
}

int net_request(void) { return request; }

void net_request_posted(void) { request = -1; }

net_status_t net_status(void)
{
    net_status_t s = st;
    s.rx_bad += ip.rx_bad;
    s.events_lost += LINKQ->lost;
    return s;
}
