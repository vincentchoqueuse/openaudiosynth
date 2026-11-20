/* usbh_midi.c — USB host for one MIDI device on the M4 (usbh_midi.h), on the HAL's host controller driver: the
 * port and its timing (USB 2.0 spec. sec. 7.1.7.3 and 9.1: 100 ms after attach, reset, then address and
 * configuration), the control transfers of the enumeration, then the device's MIDI IN endpoints polled every
 * frame from the start-of-frame interrupt. The main loop only resets the port (the HAL waits 110 ms there);
 * everything else runs in the USB interrupt, at the Ethernet's priority: the two producers of the event queue
 * (shared.h, LINKQ) never preempt each other. */
#include "usbh_midi.h"

#include <string.h>

#include "main.h"
#include "midi.h"
#include "net.h"
#include "shared.h"
#include "stm32h745i_discovery_lcd.h"
#include "usbdesc.h"

#define CH_CTL_OUT 0u
#define CH_CTL_IN 1u
#define CH_MIDI 2u /* then one channel per MIDI IN endpoint */
#define DEV_ADDR 1u
#define IN_MAX 64u           /* full-speed packet */
#define CFG_MAX 512u         /* configuration descriptor kept (MIDI devices: a few hundred bytes) */
#define CTL_TIMEOUT_MS 500u  /* a control transfer that takes longer fails */
#define ENUM_WAIT_MS 100u    /* after the reset, before the first request (recovery time) */
#define RETRY_MS 3000u       /* a device that failed is set up again after this */
#define RETRIES 3

static HCD_HandleTypeDef hhcd;

/* port and enumeration states; the main loop moves EMPTY .. WAIT_ENABLED, the interrupt ENUM .. ERROR */
typedef enum { S_OFF, S_EMPTY, S_SETTLE, S_WAIT_ENABLED, S_ENUM, S_RUN, S_NOT_MIDI, S_ERROR } state_t;

static struct {
    volatile state_t s;
    volatile uint32_t plugged, unplugged, enabled; /* counters bumped by the HAL's callbacks */
    uint32_t plugged_seen, unplugged_seen, enabled_seen;
    uint32_t t0; /* main loop: HAL tick of the last state change */
    int retries;
    /* interrupt: the enumeration */
    int step;
    int ctl_active; /* a control transfer is in progress */
    uint32_t wait; /* frames to wait before the next step */
    uint8_t speed, mps0;
    usbdesc_device_t dev;
    usbdesc_midi_t md;
    /* the control transfer in progress */
    uint8_t setup[8];
    uint16_t len;
    int stage;
    uint32_t frames;
    uint32_t got; /* bytes received in the data stage */
    /* the MIDI IN channels */
    uint8_t busy[USBDESC_MAX_IN];
    uint32_t messages, lost;
    char name[24];
} u;

static uint8_t ctl_buf[CFG_MAX + 64u] __attribute__((aligned(4)));
static uint8_t in_buf[USBDESC_MAX_IN][IN_MAX] __attribute__((aligned(4)));

/* ---------------- clocks and pins ---------------- */

int usbh_midi_clock(void)
{
    /* HSE 25 MHz / 5 = 5 MHz, x 144 = 720 MHz; Q / 15 = 48 MHz (USB), R / 75 = 9.6 MHz (the panel's pixel clock,
     * 9 MHz typical) */
    RCC_PeriphCLKInitTypeDef c = {0};
    c.PeriphClockSelection = RCC_PERIPHCLK_LTDC | RCC_PERIPHCLK_USB;
    c.PLL3.PLL3M = 5;
    c.PLL3.PLL3N = 144;
    c.PLL3.PLL3P = 2;
    c.PLL3.PLL3Q = 15;
    c.PLL3.PLL3R = 75;
    c.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    c.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    c.PLL3.PLL3FRACN = 0;
    c.UsbClockSelection = RCC_USBCLKSOURCE_PLL3;
    return HAL_RCCEx_PeriphCLKConfig(&c) == HAL_OK;
}

/* replaces the board support package's (weak) clock set-up of the display: PLL3 serves USB too */
HAL_StatusTypeDef MX_LTDC_ClockConfig(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;
    return usbh_midi_clock() ? HAL_OK : HAL_ERROR;
}

void HAL_HCD_MspInit(HCD_HandleTypeDef *h)
{
    if (h->Instance != USB2_OTG_FS)
        return;
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12; /* D-, D+ */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF10_OTG2_FS;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; /* ID */
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_5; /* the power switch of the connector's VBUS, active high */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = 0;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOA, &g);
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, NET_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void OTG_FS_IRQHandler(void) { HAL_HCD_IRQHandler(&hhcd); }

/* ---------------- the HAL's callbacks (USB interrupt) ---------------- */

static void enum_frame(void);
static void midi_frame(void);

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *h)
{
    (void)h;
    u.plugged++;
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *h)
{
    (void)h;
    u.unplugged++;
    if (u.s != S_OFF)
        u.s = S_EMPTY;
    SHARED->usb_up = 0; /* the M7 stops the notes the device was holding */
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *h)
{
    (void)h;
    u.enabled++;
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *h) { (void)h; }

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *h)
{
    (void)h;
    if (u.s == S_ENUM)
        enum_frame();
    else if (u.s == S_RUN)
        midi_frame();
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *h, uint8_t ch, HCD_URBStateTypeDef urb)
{
    (void)h;
    (void)ch;
    (void)urb; /* the states are read at each frame */
}

/* ---------------- control transfers (interrupt, one stage per frame at most) ---------------- */

enum { C_SETUP, C_DATA, C_STATUS };

static void open_control(uint8_t addr, uint8_t mps)
{
    HAL_HCD_HC_Init(&hhcd, CH_CTL_OUT, 0x00, addr, u.speed, EP_TYPE_CTRL, mps);
    HAL_HCD_HC_Init(&hhcd, CH_CTL_IN, 0x80, addr, u.speed, EP_TYPE_CTRL, mps);
}

static void control(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
    const uint8_t s[8] = {type, req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index, (uint8_t)(index >> 8),
                          (uint8_t)len, (uint8_t)(len >> 8)};
    memcpy(u.setup, s, sizeof s);
    u.len = len;
    u.got = 0;
    u.frames = 0;
    u.stage = C_SETUP;
    u.ctl_active = 1;
    HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_OUT, 0, EP_TYPE_CTRL, 0, u.setup, 8, 0);
}

static void status_stage(void)
{
    u.stage = C_STATUS;
    if (u.len) /* data came in: the status goes out */
        HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_OUT, 0, EP_TYPE_CTRL, 1, ctl_buf, 0, 0);
    else
        HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_IN, 1, EP_TYPE_CTRL, 1, ctl_buf, 0, 0);
}

/* 1 done, 0 in progress, -1 failed */
static int control_run(void)
{
    if (++u.frames > CTL_TIMEOUT_MS)
        return -1;
    const int out = u.stage == C_SETUP || (u.stage == C_STATUS && u.len);
    const HCD_URBStateTypeDef urb = HAL_HCD_HC_GetURBState(&hhcd, out ? CH_CTL_OUT : CH_CTL_IN);
    if (urb == URB_ERROR || urb == URB_STALL)
        return -1;
    if (urb == URB_NOTREADY && out) { /* NAK on an OUT stage: send it again (the HAL retries an IN by itself) */
        if (u.stage == C_SETUP)
            HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_OUT, 0, EP_TYPE_CTRL, 0, u.setup, 8, 0);
        else
            HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_OUT, 0, EP_TYPE_CTRL, 1, ctl_buf, 0, 0);
        return 0;
    }
    if (urb != URB_DONE)
        return 0;
    switch (u.stage) {
    case C_SETUP:
        if (u.len) {
            u.stage = C_DATA;
            HAL_HCD_HC_SubmitRequest(&hhcd, CH_CTL_IN, 1, EP_TYPE_CTRL, 1, ctl_buf, u.len, 0);
        } else {
            status_stage();
        }
        return 0;
    case C_DATA:
        u.got = HAL_HCD_HC_GetXferCount(&hhcd, CH_CTL_IN);
        if (u.got > u.len)
            u.got = u.len;
        status_stage();
        return 0;
    default:
        return 1;
    }
}

/* ---------------- enumeration (interrupt, at each frame) ---------------- */

static void enum_fail(void) { u.s = S_ERROR; }

static void enum_frame(void)
{
    if (u.wait) {
        u.wait--;
        return;
    }
    if (u.ctl_active) {
        const int r = control_run();
        if (r == 0)
            return;
        u.ctl_active = 0;
        if (r < 0) {
            if (u.step != 7) { /* only the product name (read before step 7) may fail */
                enum_fail();
                return;
            }
            u.got = 0;
        }
    }
    switch (u.step) {
    case 0: /* the device's first 8 bytes: its control packet size */
        u.speed = (uint8_t)HAL_HCD_GetCurrentSpeed(&hhcd);
        open_control(0, 8);
        control(0x80, 6, 0x0100, 0, 8);
        break;
    case 1:
        if (!usbdesc_device(ctl_buf, (int)u.got, &u.dev) || u.dev.mps0 < 8 || u.dev.mps0 > 64) {
            enum_fail();
            return;
        }
        u.mps0 = u.dev.mps0;
        open_control(0, u.mps0);
        control(0x00, 5, DEV_ADDR, 0, 0); /* SET_ADDRESS */
        break;
    case 2:
        u.wait = 2; /* SET_ADDRESS recovery: 2 ms */
        break;
    case 3:
        open_control(DEV_ADDR, u.mps0);
        control(0x80, 6, 0x0100, 0, 18); /* the whole device descriptor */
        break;
    case 4:
        if (!usbdesc_device(ctl_buf, (int)u.got, &u.dev)) {
            enum_fail();
            return;
        }
        control(0x80, 6, 0x0200, 0, 9); /* the configuration's first 9 bytes: its total length */
        break;
    case 5: {
        const int total = usbdesc_config_total(ctl_buf, (int)u.got);
        if (!total) {
            enum_fail();
            return;
        }
        control(0x80, 6, 0x0200, 0, (uint16_t)(total < (int)CFG_MAX ? total : (int)CFG_MAX));
        break;
    }
    case 6:
        usbdesc_find_midi(ctl_buf, (int)u.got, &u.md);
        if (u.dev.i_product)
            control(0x80, 6, (uint16_t)(0x0300u | u.dev.i_product), 0x0409, 64); /* product name, US English */
        else
            u.got = 0;
        break;
    case 7:
        usbdesc_string(ctl_buf, (int)u.got, u.name, sizeof u.name);
        if (!u.md.n_in) {
            u.s = S_NOT_MIDI;
            return;
        }
        control(0x00, 9, u.md.config, 0, 0); /* SET_CONFIGURATION */
        break;
    case 8:
        for (int k = 0; k < u.md.n_in; k++) {
            const uint16_t mps = u.md.mps_in[k] < IN_MAX ? u.md.mps_in[k] : IN_MAX;
            /* polled as an interrupt endpoint: one IN token per frame, a NAK waits for the next frame (a bulk
             * endpoint answers the same transaction) */
            HAL_HCD_HC_Init(&hhcd, (uint8_t)(CH_MIDI + k), u.md.ep_in[k], DEV_ADDR, u.speed, EP_TYPE_INTR, mps);
            hhcd.hc[CH_MIDI + k].toggle_in = 0;
            u.md.mps_in[k] = mps;
            u.busy[k] = 0;
        }
        u.retries = 0;
        u.s = S_RUN;
        SHARED->usb_up = 1;
        return;
    default:
        enum_fail();
        return;
    }
    u.step++;
}

/* ---------------- MIDI in (interrupt, at each frame) ---------------- */

static void midi_frame(void)
{
    for (int k = 0; k < u.md.n_in; k++) {
        const uint8_t ch = (uint8_t)(CH_MIDI + k);
        if (u.busy[k]) {
            const HCD_URBStateTypeDef urb = HAL_HCD_HC_GetURBState(&hhcd, ch);
            if (urb == URB_IDLE)
                continue; /* the transaction is under way */
            if (urb == URB_DONE) {
                const uint32_t n = HAL_HCD_HC_GetXferCount(&hhcd, ch);
                for (uint32_t i = 0; i + 4 <= n && i + 4 <= IN_MAX; i += 4) {
                    midi_msg_t m;
                    if (!midi_usb_decode(in_buf[k] + i, &m))
                        continue;
                    linkq_event_t e = {0};
                    e.kind = LINKQ_MIDI;
                    e.len = m.len;
                    memcpy(e.midi, m.data, m.len);
                    if (linkq_push(LINKQ, &e)) {
                        u.messages++;
                        SHARED->usb_midi++;
                    } else {
                        u.lost++;
                    }
                }
            }
            u.busy[k] = 0; /* done, NAK (nothing to say), or an error: ask again */
        }
        HAL_HCD_HC_SubmitRequest(&hhcd, ch, 1, EP_TYPE_INTR, 1, in_buf[k], u.md.mps_in[k], 0);
        u.busy[k] = 1;
    }
}

/* ---------------- main loop ---------------- */

static int move(state_t from, state_t to)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    const int ok = u.s == from;
    if (ok)
        u.s = to;
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    if (ok)
        u.t0 = HAL_GetTick();
    return ok;
}

void usbh_midi_init(void)
{
    memset(&u, 0, sizeof u);
    SHARED->usb_up = 0;
#if BOARD_MIDI_USB
    if ((__HAL_RCC_GET_USB_SOURCE() != RCC_USBCLKSOURCE_PLL3 || !__HAL_RCC_GET_FLAG(RCC_FLAG_PLL3RDY)) &&
        !usbh_midi_clock())
        return; /* no 48 MHz: stays off */
    HAL_PWREx_EnableUSBVoltageDetector();
    hhcd.Instance = USB2_OTG_FS;
    hhcd.Init.Host_channels = 16;
    hhcd.Init.speed = HCD_SPEED_FULL;
    hhcd.Init.dma_enable = 0;
    hhcd.Init.phy_itface = HCD_PHY_EMBEDDED;
    hhcd.Init.Sof_enable = 0;
    hhcd.Init.low_power_enable = 0;
    hhcd.Init.vbus_sensing_enable = 0;
    hhcd.Init.lpm_enable = 0;
    if (HAL_HCD_Init(&hhcd) != HAL_OK)
        return;
    u.s = S_EMPTY;
    u.t0 = HAL_GetTick();
    HAL_HCD_Start(&hhcd);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET); /* power the connector */
#endif
}

void usbh_midi_poll(void)
{
    if (u.s == S_OFF)
        return;
    const uint32_t now = HAL_GetTick();
    if (u.unplugged != u.unplugged_seen) { /* start again from an empty port, as the device left */
        u.unplugged_seen = u.unplugged;
        u.plugged_seen = u.plugged;
        HAL_HCD_Stop(&hhcd);
        HAL_HCD_Start(&hhcd);
        u.s = S_EMPTY;
        u.t0 = now;
        u.retries = 0;
        u.name[0] = 0;
        u.dev.vid = u.dev.pid = 0;
        return;
    }
    switch (u.s) {
    case S_EMPTY:
        if (u.plugged != u.plugged_seen) {
            u.plugged_seen = u.plugged;
            move(S_EMPTY, S_SETTLE);
        }
        break;
    case S_SETTLE: /* 100 ms for the device's power to settle (200 ms here: margin), then the port reset */
        if (now - u.t0 >= 200u && move(S_SETTLE, S_WAIT_ENABLED)) {
            u.enabled_seen = u.enabled;
            HAL_HCD_ResetPort(&hhcd); /* 110 ms */
            u.t0 = HAL_GetTick();
        }
        break;
    case S_WAIT_ENABLED:
        if (u.enabled != u.enabled_seen) {
            u.step = 0;
            u.ctl_active = 0;
            u.wait = ENUM_WAIT_MS;
            u.name[0] = 0;
            move(S_WAIT_ENABLED, S_ENUM);
        } else if (now - u.t0 > 1000u) {
            move(S_WAIT_ENABLED, S_ERROR);
        }
        break;
    case S_ERROR: /* a few more tries, a while apart */
        if (u.retries < RETRIES && now - u.t0 >= RETRY_MS) {
            u.retries++;
            move(S_ERROR, S_SETTLE);
        }
        break;
    default:
        break;
    }
}

usbh_midi_status_t usbh_midi_status(void)
{
    static const usbh_midi_state_t map[] = {
        [S_OFF] = USBH_MIDI_OFF,         [S_EMPTY] = USBH_MIDI_EMPTY,  [S_SETTLE] = USBH_MIDI_ENUM,
        [S_WAIT_ENABLED] = USBH_MIDI_ENUM, [S_ENUM] = USBH_MIDI_ENUM,  [S_RUN] = USBH_MIDI_RUNNING,
        [S_NOT_MIDI] = USBH_MIDI_NOT_MIDI, [S_ERROR] = USBH_MIDI_ERROR,
    };
    usbh_midi_status_t st;
    st.state = map[u.s];
    st.vid = u.dev.vid;
    st.pid = u.dev.pid;
    memcpy(st.name, u.name, sizeof st.name);
    st.name[sizeof st.name - 1] = 0;
    st.messages = u.messages;
    st.lost = u.lost;
    return st;
}
