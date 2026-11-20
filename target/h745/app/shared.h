/* shared.h — the mailbox between the two cores of the STM32H745, in SRAM4 (D3 domain, 0x38000000), which
 * both cores see and nothing else uses. Each field has one writer: the M4 writes its requests, the M7 its
 * status. No lock: a request is the pair (req_preset, req_seq), written in that order with a barrier, and
 * the M7 acts once per new req_seq. The M7 maps this area non-cacheable with the MPU (app_init); the M4
 * has no data cache. Magic numbers tell a core that the other one has initialized its half.
 * Layout of SRAM4: mailbox 0x0000, scope 0x0400, network event queue 0x2800, network preset 0x3500. */
#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#include "link.h"
#include "linkq.h"

#define SHARED_MAGIC_M4 0x4D345359u /* "M4SY" */
#define SHARED_MAGIC_M7 0x4D375359u /* "M7SY" */
#define SHARED_ADDR 0x38000000u
#define SHARED_SIZE 1024u /* room of the mailbox; the scope follows it */
#define BENCH_FX_OFF 1u   /* bench_cmd options: chorus, delay and reverb off */
#define BENCH_OS_1X 2u    /* nonlinear filters without oversampling */
#define BENCH_DELAY_OFF 4u
#define BENCH_REVERB_OFF 8u
#define BENCH_PROFILE 16u /* sample the PC during the benchmark (profile.h) */

typedef struct {
    /* ---- written by the M4 ---- */
    volatile uint32_t m4_magic;
    volatile uint32_t req_seq;    /* incremented after req_preset is written */
    volatile uint32_t req_preset; /* index in presets[] */
    volatile uint32_t bench_cmd;  /* written by the debugger (tools/h745_bench.py): (sequence << 8) | options */
    volatile int32_t bench_preset; /* written by the debugger: the only preset to measure, -1 every preset */

    /* ---- written by the M7 ---- */
    volatile uint32_t m7_magic;
    volatile uint32_t i2c4_free;  /* 1 once the codec is set up: I2C4 belongs to the M4 (touch panel) */
    volatile uint32_t ack_seq;    /* last request applied */
    volatile int32_t preset;      /* preset playing, -1 none */
    volatile int32_t audio_status;
    volatile int32_t overload;    /* preset muted because the M7 could not keep up with it, -1 none */
    volatile float load_avg_pct, load_peak_pct;
    volatile uint32_t overruns, audio_errors, stack_free, heartbeat;
    volatile uint32_t bench_ack;  /* sequence of the last benchmark finished (results in app.c's bench_*) */

    /* ---- written by the M4: the network (m4/net.c), appended to keep the offsets above ---- */
    volatile uint32_t kit_seq;    /* incremented once a new sample kit is complete and checked */
    volatile uint32_t kit_addr;   /* in the SDRAM (format: dsp/core/kit.h), written before kit_seq */
    volatile uint32_t kit_size;
    volatile uint32_t kit_count;  /* samples */

    /* ---- written by the M7, appended ---- */
    volatile uint32_t done_seq;    /* last request applied (ack_seq: taken; the network preset is read until done) */
    volatile uint32_t kit_ack_seq; /* last kit the synth plays: the other bank is free for the next one */

    /* ---- written by the M4: the network's liveness, for the M7's fail-safe (app.c, net_watch) ---- */
    volatile uint32_t net_alive;   /* link datagrams received for this board */
    volatile uint32_t net_midi;    /* MIDI datagrams among them */
    volatile uint32_t net_up;      /* 1 while the cable is connected */

    /* ---- the demo arpeggio: off at power-up (a board that reboots on stage stays silent) ---- */
    volatile uint32_t demo_taps;    /* M4: incremented by each tap on the title; each change toggles the demo */
    volatile uint32_t demo_playing; /* M7: 1 while the demo plays */

    /* ---- written by the M4: the USB MIDI device (m4/usbh_midi.c), for the M7's fail-safe ---- */
    volatile uint32_t usb_up;   /* 1 while a MIDI device is plugged in and set up */
    volatile uint32_t usb_midi; /* MIDI messages received from it */

    /* ---- written by the M7 at start-up: the MCU's unique id, which the M4 cannot read (a bus fault: the system
     * memory at UID_BASE is not on its bus); the network derives the board's MAC address and id from it ---- */
    volatile uint32_t uid[3];
    volatile uint32_t uid_ok; /* SHARED_UID_OK(uid[0]) once uid is written */
} shared_t;

#define SHARED_UID_OK(w0) (0x55494400u ^ (w0))

/* req_preset value for the preset received over the network (NET_PRESET) instead of a built-in one; the M7
 * then reports SHARED_PLAYING_NET as the preset playing. */
#define SHARED_PRESET_NET 0x10000u
#define SHARED_PLAYING_NET (-2)

_Static_assert(sizeof(shared_t) <= SHARED_SIZE, "mailbox larger than its room");

#define SHARED ((shared_t *)SHARED_ADDR)

/* The last SCOPE_LEN samples of the output (mono, 16 bits), written by the M7's audio callback, read by the
 * M4 for the oscilloscope and the spectrum. pos counts every sample written (the ring index is pos modulo
 * SCOPE_LEN). Same MPU region as the mailbox: non-cacheable for the M7. */
#define SCOPE_ADDR (SHARED_ADDR + SHARED_SIZE)
#define SCOPE_LEN 4096u /* power of two: 85 ms at 48 kHz */
typedef struct {
    volatile uint32_t pos;
    volatile int16_t buf[SCOPE_LEN];
} scope_t;
#define SCOPE ((scope_t *)SCOPE_ADDR)

/* MIDI and parameter values from the network, pushed by the M4's Ethernet interrupt, popped by the M7 at the
 * start of each audio buffer (midi/linkq.h). */
#define LINKQ_ADDR (SHARED_ADDR + 0x2800u)
#define LINKQ ((linkq_t *)LINKQ_ADDR)

/* A preset received over the network, written by the M4 while no request is pending, read by the M7 when it
 * takes the request req_preset = SHARED_PRESET_NET. Same layout as presets.h's parameters. */
#define NET_PRESET_ADDR (SHARED_ADDR + 0x3500u)
typedef struct {
    uint32_t engine, count;
    char name[LINK_NAME];
    struct {
        uint16_t id;
        float value;
    } params[LINK_PARAMS_MAX];
} net_preset_t;
#define NET_PRESET ((net_preset_t *)NET_PRESET_ADDR)

#define SHARED_MPU_SIZE 16384u /* mailbox, scope, queue and network preset: the M7's non-cacheable region */
_Static_assert(SHARED_SIZE + sizeof(scope_t) <= LINKQ_ADDR - SHARED_ADDR, "scope over the queue");
_Static_assert(LINKQ_ADDR + sizeof(linkq_t) <= NET_PRESET_ADDR, "queue over the network preset");
_Static_assert(NET_PRESET_ADDR + sizeof(net_preset_t) <= SHARED_ADDR + SHARED_MPU_SIZE, "network preset outside the MPU region");

#endif /* SHARED_H */
