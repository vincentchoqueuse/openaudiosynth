/* app.c — the M7 application: the synth engine (dsp/synth.h) rendered in the audio interrupt (audio.h),
 * presets (presets.h) chosen by the M4 through the mailbox (shared.h), and a small arpeggio played inside
 * the audio callback so every preset can be heard without MIDI: off at power-up, toggled by a tap on the
 * screen's title.
 * A preset change never runs in the interrupt: the interrupt fades out (5 ms) and outputs silence, the main
 * loop re-initializes the synth and applies the preset as the host does (synth_init, engine, values), then
 * the interrupt fades back in. No click, and nothing long inside the audio interrupt.
 * Notes, controllers and parameter values from the network (the M4's Ethernet, docs/link.md) arrive through
 * a lock-free queue (shared.h, LINKQ) read at the start of each audio buffer; the first note from the
 * network stops the demo arpeggio.
 * Sample kits (the sampler engine) arrive in the SDRAM through the M4 (shared.h, kit_seq): the M7 reads them
 * through its data cache (MPU region 2, write-through), invalidated for the kit before handing it to the synth. */
#include "app.h"

#include <string.h>

#include "audio.h"
#include "core/kit.h"
#include "link.h"
#include "engines/engine.h"
#include "knobs.h"
#include "main.h"
#include "presets.h"
#include "profile.h"
#include "shared.h"
#include "synth.h"

/* From the linker script (app/stm32h745_cm7_synth.ld): the AXI SRAM section, not zeroed by the startup
 * code, and the end of the static data, where the free stack area begins. */
extern uint8_t _sram_d1[], _eram_d1[];
extern uint32_t _end[];
#define APP_STACK_PAINT 0xA5A5A5A5u
#define APP_DEFAULT_PRESET "mono_d_lead"
#define APP_FADE (2 * AUDIO_FRAMES) /* 256 samples = 5.3 ms */

/* Health of the board, refreshed by app_loop. Volatile so a debugger can watch it (Live Expressions);
 * the same values go to the M4 through the mailbox. */
typedef struct {
    int audio_status;          /* 0 = playing, else the error of audio_start */
    float load_avg_pct;        /* DSP load, smoothed, in % of the time between two interrupts */
    float load_peak_pct;       /* worst interrupt since start */
    uint32_t overruns;         /* interrupts longer than the buffer (audible glitches): must stay 0 */
    uint32_t audio_errors;     /* DMA / SAI errors */
    uint32_t stack_free_bytes; /* stack never touched since start (painted at init) */
    int preset;                /* preset playing */
    int overload;              /* preset muted for overload, -1 none */
} app_health_t;
volatile app_health_t app_health;

/* Who owns the synth: the interrupt (PLAYING) or the main loop (SILENT). While PLAYING, the output gain
 * slides towards app_target (0 or 1) in APP_FADE samples; at 0 the interrupt hands the synth over. */
typedef enum { APP_PLAYING, APP_SILENT } app_state_t;
static volatile app_state_t app_state = APP_SILENT;
static volatile float app_target = 1.0f; /* written by the main loop */
static float app_gain;                   /* interrupt only */
static int app_pending = -1;             /* preset to apply once silent (main loop only) */
static volatile int app_overload;        /* set by the interrupt when it gave up a preset too heavy */
#define APP_OVERLOAD_STREAK 4            /* consecutive overruns (~10 ms of real overload) before muting */
static uint32_t app_req_seen;      /* last mailbox request taken (main loop only) */
static uint32_t app_kit_seen;      /* last kit taken from the M4 (main loop only) */
static const uint8_t *app_kit;     /* the kit the synth plays (synth_part_kit), NULL none */
static size_t app_kit_bytes;
static volatile int app_kit_new;   /* set by the main loop: the interrupt gives app_kit to the synth */
static uint32_t app_kit_new_seq;   /* its kit_seq, acknowledged (kit_ack_seq) once the synth plays it */

/* ---------------- demo sequencer (runs inside the audio callback) ---------------- */

#define SEQ_STEP (AUDIO_SAMPLE_RATE / 4) /* 4 notes per second */
static const uint8_t seq_notes[] = {45, 52, 57, 60, 64, 60, 57, 52}; /* A2 E3 A3 C4 E4 C4 A3 E3 */
static uint32_t seq_pos;   /* samples since start */
static int seq_index = -1; /* note sounding, -1 none */
static int seq_on;         /* off at power-up; toggled from the screen (demo_taps), stopped by a network note */
static uint32_t seq_taps;  /* demo_taps last seen */
static volatile int app_silence; /* set by the main loop (net_watch): the interrupt stops every note */

static void seq_note(uint8_t status, uint8_t note, uint8_t vel, int offset)
{
    const uint8_t msg[3] = {status, note, vel};
    synth_midi(synth_default(), msg, 3, offset);
}

static void seq_stop(void)
{
    if (seq_index >= 0)
        seq_note(0x80, seq_notes[seq_index], 0, 0);
    seq_index = -1;
    seq_on = 0;
}

/* a tap on the screen's title toggles the demo (audio interrupt) */
static void seq_follow_taps(void)
{
    const uint32_t taps = SHARED->demo_taps;
    if (taps == seq_taps)
        return;
    seq_taps = taps;
    if (seq_on)
        seq_stop();
    else
        seq_on = 1;
}

static void seq_run(int n)
{
    /* events falling inside this buffer, with their offset in samples */
    uint32_t next = (seq_pos + SEQ_STEP - 1) / SEQ_STEP * SEQ_STEP;
    for (; next < seq_pos + (uint32_t)n; next += SEQ_STEP) {
        const int offset = (int)(next - seq_pos);
        if (seq_index >= 0)
            seq_note(0x80, seq_notes[seq_index], 0, offset);
        seq_index = (seq_index + 1) % (int)sizeof(seq_notes);
        seq_note(0x90, seq_notes[seq_index], (seq_index % 4) ? 90 : 120, offset);
    }
    seq_pos += (uint32_t)n;
}

/* ---------------- audio callback ---------------- */

/* the output for the M4's oscilloscope and spectrum (shared.h): mono, 16 bits */
static void scope_write(const float *L, const float *R, int n)
{
    uint32_t pos = SCOPE->pos;
    for (int i = 0; i < n; i++, pos++) {
        float m = 0.5f * (L[i] + R[i]) * 32767.0f;
        m = m > 32767.0f ? 32767.0f : m < -32767.0f ? -32767.0f : m;
        SCOPE->buf[pos & (SCOPE_LEN - 1u)] = (int16_t)m;
    }
    __DMB(); /* the samples before the position that publishes them */
    SCOPE->pos = pos;
}

static void app_render_synth(float *L, float *R, int n);
static void wdg_kick(void);

/* Every note off on every channel, sustain pedal up: what the network was holding stops (release tails play). */
static void silence_all(void)
{
    for (uint8_t ch = 0; ch < 16; ch++) {
        const uint8_t pedal[3] = {(uint8_t)(0xB0u | ch), 64, 0}, off[3] = {(uint8_t)(0xB0u | ch), 123, 0};
        synth_midi(synth_default(), pedal, 3, 0);
        synth_midi(synth_default(), off, 3, 0);
    }
}

/* The events the network queued since the last buffer, applied at its start (a 128-frame buffer: 2.7 ms of
 * jitter at most). Bounded: at most the queue's length per buffer. */
static void net_events(int apply)
{
    linkq_event_t e;
    for (uint32_t k = 0; k < LINKQ_LEN && linkq_pop(LINKQ, &e); k++) {
        if (!apply)
            continue; /* the main loop owns the synth (preset change): dropped */
        if (e.kind == LINKQ_MIDI && e.len >= 1 && e.len <= 3) {
            if ((e.midi[0] & 0xF0u) == 0x90u && seq_on) /* someone plays: the demo stops */
                seq_stop();
            synth_midi(synth_default(), e.midi, e.len, 0);
            knobs_midi(e.midi, e.len);
        } else if (e.kind == LINKQ_PARAM) {
            synth_set_param(synth_default(), e.id, e.value);
        }
    }
}

static void app_render(float *L, float *R, int n)
{
    app_render_synth(L, R, n);
    scope_write(L, R, n);
}

static void app_render_synth(float *L, float *R, int n)
{
    const app_state_t st = app_state;
    /* overload guard: a preset the M7 cannot render in time would take every cycle and starve the main loop
     * (no more preset changes, no status). After a few consecutive overruns the interrupt gives the synth
     * back and stays silent until the next preset is chosen. */
    if (st == APP_PLAYING && audio_overrun_streak() >= APP_OVERLOAD_STREAK) {
        app_overload = 1;
        app_gain = 0.0f;
        app_state = APP_SILENT;
    }
    if (app_state == APP_SILENT) { /* the main loop owns the synth */
        net_events(0);
        for (int i = 0; i < n; i++)
            L[i] = R[i] = 0.0f;
        return;
    }
    if (app_kit_new) { /* a new kit: given to the synth here, where the synth is owned */
        synth_part_kit(synth_default(), 0, app_kit, app_kit_bytes);
        SHARED->kit_ack_seq = app_kit_new_seq; /* the previous bank is not read any more */
        app_kit_new = 0;
    }
    if (app_silence) {
        silence_all();
        app_silence = 0;
    }
    net_events(1);
    seq_follow_taps();
    if (seq_on)
        seq_run(n);
    synth_render(synth_default(), L, R, n);
    const float target = app_target;
    if (app_gain == target)
        return;
    const float step = 1.0f / (float)APP_FADE;
    for (int i = 0; i < n; i++) {
        app_gain = target > app_gain ? (app_gain + step < target ? app_gain + step : target)
                                     : (app_gain - step > target ? app_gain - step : target);
        L[i] *= app_gain;
        R[i] *= app_gain;
    }
    if (app_gain == 0.0f && target == 0.0f)
        app_state = APP_SILENT; /* the next callbacks output silence: the main loop owns the synth */
}

/* ---------------- presets (main loop, while the interrupt outputs silence) ---------------- */

static void preset_apply(int index)
{
    synth_t *s = synth_default();
    synth_init(s, (float)AUDIO_SAMPLE_RATE);
    if (index >= 0 && index < preset_count) {
        const preset_t *p = &presets[index];
        synth_set_engine(s, (synth_engine_t)p->engine_id);
        for (int k = 0; k < p->count; k++)
            synth_set_param(s, preset_params[p->first + k].id, preset_params[p->first + k].value);
    }
    if (index >= 0 && index < preset_count)
        knobs_apply(s, presets[index].engine_id, &preset_params[presets[index].first], presets[index].count);
    synth_part_kit(s, 0, app_kit, app_kit_bytes); /* synth_init removed it */
    seq_index = -1; /* synth_init silenced every note */
    app_health.preset = index;
}

/* The engine and the parameters a preset queued (synth.c applies them at the next render: the engine's init and
 * some 170 parameters) are applied here, in the main loop, by rendering one silent block, instead of in the
 * first audio interrupt after the fade-in, which could overrun on a heavy preset. */
static void engine_warm(void)
{
    static float wl[SYNTH_BLOCK], wr[SYNTH_BLOCK];
    synth_render(synth_default(), wl, wr, SYNTH_BLOCK);
}

_Static_assert(sizeof(preset_param_t) == sizeof(NET_PRESET->params[0]), "same layout as preset_param_t");

/* the preset the M4 received over the network (shared.h, NET_PRESET), applied like a built-in one */
static void preset_apply_net(void)
{
    synth_t *s = synth_default();
    synth_init(s, (float)AUDIO_SAMPLE_RATE);
    const net_preset_t *p = NET_PRESET;
    const uint32_t e = p->engine, count = p->count <= LINK_PARAMS_MAX ? p->count : 0u;
    if (e < SYNTH_ENGINE_COUNT && engine_registry[e]->shared_floats == 0) { /* not the wavetables: no memory */
        synth_set_engine(s, (synth_engine_t)e);
        for (uint32_t k = 0; k < count; k++)
            synth_set_param(s, p->params[k].id, p->params[k].value);
    }
    if (e < SYNTH_ENGINE_COUNT && engine_registry[e]->shared_floats == 0)
        knobs_apply(s, (int)e, (const preset_param_t *)(const void *)p->params, (int)count);
    synth_part_kit(s, 0, app_kit, app_kit_bytes);
    seq_index = -1;
    app_health.preset = SHARED_PLAYING_NET;
}

static int preset_find(const char *name)
{
    for (int i = 0; i < preset_count; i++)
        if (!strcmp(presets[i].name, name))
            return i;
    return preset_count > 0 ? 0 : -1;
}

/* ---------------- benchmark (started by the debugger, tools/h745_bench.py) ---------------- */

/* Every preset plays the demo arpeggio for BENCH_SAMPLES samples, rendered in the main loop with the audio
 * interrupt stopped (the DMA plays silence), and its DSP load is recorded: average and worst buffer, in %
 * of the time a buffer lasts. The same measure for every optimization, on the real board. */
#define BENCH_SAMPLES (AUDIO_SAMPLE_RATE * 3 / 2)
#define BENCH_MAX 128
volatile float bench_avg[BENCH_MAX], bench_peak[BENCH_MAX];
volatile int bench_count;
static uint32_t bench_seen;

static void bench_options(uint32_t options)
{
    synth_t *s = synth_default();
    if (options & BENCH_FX_OFF) {
        synth_set_param(s, SYNTH_PARAM_CHORUS_MODE, 0.0f);
        synth_set_param(s, SYNTH_PARAM_DELAY_MIX, 0.0f);
        synth_set_param(s, SYNTH_PARAM_REVERB_MIX, 0.0f);
    }
    if (options & BENCH_DELAY_OFF)
        synth_set_param(s, SYNTH_PARAM_DELAY_MIX, 0.0f);
    if (options & BENCH_REVERB_OFF)
        synth_set_param(s, SYNTH_PARAM_REVERB_MIX, 0.0f);
    if ((options & BENCH_OS_1X) && app_health.preset >= 0) {
        const engine_ops_t *e = engine_registry[presets[app_health.preset].engine_id];
        for (int i = 0; i < e->param_count; i++)
            if (!strcmp(e->params[i].name, "oversample"))
                synth_set_param(s, i, 0.0f);
    }
}

static void bench_run(uint32_t options)
{
    static float bl[AUDIO_FRAMES], br[AUDIO_FRAMES];
    const int current = app_health.preset;
    app_target = 0.0f; /* fade out, then the interrupt hands the synth over */
    while (app_state != APP_SILENT)
        __WFI();
    HAL_Delay(10); /* both halves of the DMA buffer now hold silence */
    NVIC_DisableIRQ(DMA2_Stream1_IRQn);

    const float avail = (float)(SystemCoreClock / AUDIO_SAMPLE_RATE * AUDIO_FRAMES);
    const int n = preset_count < BENCH_MAX ? preset_count : BENCH_MAX;
    const int only = SHARED->bench_preset;
    for (int i = 0; i < n; i++) {
        bench_avg[i] = bench_peak[i] = 0.0f;
        if (only >= 0 && i != only)
            continue;
        preset_apply(i);
        bench_options(options);
        seq_pos = 0;
        uint64_t total = 0;
        uint32_t worst = 0;
        if (options & BENCH_PROFILE)
            profile_start(10000);
        for (int k = 0; k < BENCH_SAMPLES / AUDIO_FRAMES; k++) {
            const uint32_t t0 = DWT->CYCCNT;
            seq_run(AUDIO_FRAMES);
            synth_render(synth_default(), bl, br, AUDIO_FRAMES);
            wdg_kick(); /* the audio interrupt is stopped during the benchmark: the main loop is alive */
            const uint32_t dt = DWT->CYCCNT - t0;
            total += dt;
            worst = dt > worst ? dt : worst;
        }
        if (options & BENCH_PROFILE)
            profile_stop();
        bench_avg[i] = 100.0f * (float)total / (avail * (float)(BENCH_SAMPLES / AUDIO_FRAMES));
        bench_peak[i] = 100.0f * (float)worst / avail;
    }
    bench_count = n;

    preset_apply(current); /* back to the preset that was playing */
    NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    app_target = 1.0f;
    __DMB();
    app_state = APP_PLAYING;
}

/* ---------------- mailbox with the M4 ---------------- */

/* SRAM4 is cacheable for the M7 by default: one MPU region makes the mailbox non-cacheable and shareable,
 * so both cores always see the same bytes. Region 0 is CubeMX's (MPU_Config); this one is region 1. */
static void shared_mpu(void)
{
    MPU_Region_InitTypeDef r = {0};
    HAL_MPU_Disable();
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER1;
    r.BaseAddress = SHARED_ADDR;
    r.Size = MPU_REGION_SIZE_16KB; /* SHARED_MPU_SIZE: mailbox and scope */
    r.SubRegionDisable = 0x00;
    r.TypeExtField = MPU_TEX_LEVEL1;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* The SDRAM (FMC bank, 0xD0000000, 16 MB), where the M4 writes the sample kits: device memory by default for
 * the M7 (every read a bus access, and the sampler reads ~14 samples per output sample). Normal memory,
 * write-through, not shareable (the M7 caches it); the M7 never writes it, and invalidates a kit's lines when
 * the M4 publishes one (kit_take). */
static void sdram_mpu(void)
{
    MPU_Region_InitTypeDef r = {0};
    HAL_MPU_Disable();
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER2;
    r.BaseAddress = 0xD0000000u;
    r.Size = MPU_REGION_SIZE_16MB;
    r.SubRegionDisable = 0x00;
    r.TypeExtField = MPU_TEX_LEVEL0;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* A kit the M4 published: its cache lines dropped (the M4 wrote the SDRAM behind the cache), checked, then
 * handed to the interrupt. Main loop. */
static void kit_take(void)
{
    if (app_kit_new && app_state == APP_SILENT) { /* the main loop owns the synth (fade, overload): hand it here */
        synth_part_kit(synth_default(), 0, app_kit, app_kit_bytes);
        SHARED->kit_ack_seq = app_kit_new_seq;
        app_kit_new = 0;
    }
    const uint32_t seq = SHARED->kit_seq;
    if (seq == app_kit_seen || SHARED->m4_magic != SHARED_MAGIC_M4 || app_kit_new)
        return; /* nothing new, or the previous handover not taken yet (it is, at the next audio buffer) */
    __DMB(); /* the kit's fields were written before its sequence number */
    app_kit_seen = seq;
    const uint32_t addr = SHARED->kit_addr, size = SHARED->kit_size;
    if (addr < 0xD0000000u || addr >= 0xD1000000u || size > 0xD1000000u - addr) {
        SHARED->kit_ack_seq = seq; /* refused: the M4 may write again */
        return;
    }
    const uint32_t start = addr & ~31u;
    SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)((addr + size + 31u - start) & ~31u));
    if (kit_count((const uint8_t *)addr, size) < 0) {
        SHARED->kit_ack_seq = seq;
        return;
    }
    app_kit = (const uint8_t *)addr;
    app_kit_bytes = size;
    app_kit_new_seq = seq;
    __DMB();
    app_kit_new = 1;
}

/* A new request from the M4, or -1. */
static int shared_request(void)
{
    if (SHARED->m4_magic != SHARED_MAGIC_M4)
        return -1;
    const uint32_t seq = SHARED->req_seq;
    if (seq == app_req_seen)
        return -1;
    __DMB(); /* the preset index was written before the sequence number */
    const uint32_t index = SHARED->req_preset;
    app_req_seen = seq;
    return index < (uint32_t)preset_count || index == SHARED_PRESET_NET ? (int)index : -1;
}

static void shared_status(void)
{
    SHARED->preset = app_health.preset;
    SHARED->audio_status = app_health.audio_status;
    SHARED->overload = app_health.overload;
    SHARED->load_avg_pct = app_health.load_avg_pct;
    SHARED->load_peak_pct = app_health.load_peak_pct;
    SHARED->overruns = app_health.overruns;
    SHARED->audio_errors = app_health.audio_errors;
    SHARED->stack_free = app_health.stack_free_bytes;
    SHARED->ack_seq = app_req_seen;
    SHARED->demo_playing = (uint32_t)seq_on;
    if (app_pending < 0) /* nothing taken and still to apply: every request up to app_req_seen is done */
        SHARED->done_seq = app_req_seen;
    SHARED->heartbeat++;
}

/* ---------------- network fail-safe ---------------- */

/* Notes played over the network end with a note-off that may never come: cable pulled, hub crashed, a datagram
 * lost, the event queue full. Once MIDI has come, if nothing comes for LINK_SILENCE_MS (a host sends a heartbeat
 * every LINK_HEARTBEAT_MS), the cable goes down or events were lost, every note stops. Main loop. */
static uint32_t nw_alive, nw_alive_tick, nw_midi_done, nw_lost;

static void net_watch(void)
{
    const uint32_t now = HAL_GetTick(), alive = SHARED->net_alive, midi = SHARED->net_midi, lost = LINKQ->lost;
    if (alive != nw_alive) {
        nw_alive = alive;
        nw_alive_tick = now;
    }
    const int playing = midi != nw_midi_done; /* MIDI came since the last silence */
    if (playing && (now - nw_alive_tick > LINK_SILENCE_MS || !SHARED->net_up || lost != nw_lost)) {
        nw_midi_done = midi;
        if (app_state == APP_SILENT)
            silence_all(); /* the main loop owns the synth */
        else
            app_silence = 1;
    }
    nw_lost = lost;
}

/* The same for a USB MIDI device: unplugged while it held notes, every note stops. */
static uint32_t usb_midi_done;

static void usb_watch(void)
{
    const uint32_t midi = SHARED->usb_midi;
    if (midi != usb_midi_done && !SHARED->usb_up) {
        usb_midi_done = midi;
        if (app_state == APP_SILENT)
            silence_all();
        else
            app_silence = 1;
    }
}

static void net_watch_init(void)
{
    usb_midi_done = SHARED->usb_midi;
    nw_alive = SHARED->net_alive;
    nw_midi_done = SHARED->net_midi;
    nw_lost = LINKQ->lost;
    nw_alive_tick = HAL_GetTick();
}

/* ---------------- watchdog ---------------- */

/* IWDG1 resets the board if the main loop stops, or if the audio interrupt stops while the audio runs: a hung
 * core never leaves the circular DMA repeating its last buffer. Refreshed by the main loop only when the audio
 * interrupt has rendered since (or when the audio never started: no reset loop on a missing codec). Frozen
 * while a debugger halts the core. After a reset the board starts silent (the demo is off). */
#define APP_WDG_MS 250
static uint32_t wdg_seen;

static void wdg_start(void)
{
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;
    IWDG1->KR = 0xCCCCu; /* start (the LSI starts with it) */
    IWDG1->KR = 0x5555u; /* unlock PR and RLR */
    IWDG1->PR = 3u;      /* LSI 32 kHz / 32: one count per ms */
    IWDG1->RLR = APP_WDG_MS;
    while (IWDG1->SR != 0u) {
    }
    IWDG1->KR = 0xAAAAu;
}

static void wdg_kick(void) { IWDG1->KR = 0xAAAAu; }

static void wdg_poll(void)
{
    const uint32_t n = audio_buffers();
    if (n != wdg_seen || app_health.audio_status != 0) {
        wdg_seen = n;
        wdg_kick();
    }
}

void app_fault(void)
{
    __disable_irq();
    audio_silence();
    for (;;) {
    }
}

/* ---------------- stack watermark ---------------- */

static uint32_t *stack_bottom; /* lowest address of the free area painted at init */

/* Paints the area between the static data (+ the small heap) and the current stack pointer. */
static void stack_paint(void)
{
    uint32_t *sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    stack_bottom = _end + 0x200 / sizeof(uint32_t); /* _Min_Heap_Size of the linker script */
    for (uint32_t *p = stack_bottom; p < sp - 16; p++)
        *p = APP_STACK_PAINT;
}

static uint32_t stack_free(void)
{
    const uint32_t *p = stack_bottom;
    while (*p == APP_STACK_PAINT)
        p++;
    return (uint32_t)((uintptr_t)p - (uintptr_t)stack_bottom);
}

/* ---------------- application ---------------- */

void app_boot(void)
{
    /* the caches and the MPU are not set up yet: these writes go straight to SRAM4 */
    SHARED->m7_magic = 0;
    SHARED->i2c4_free = 0;
}

void app_init(void)
{
    stack_paint();
    shared_mpu();
    sdram_mpu();
    SCB_EnableICache();
    SCB_EnableDCache();
    /* flush-to-zero in interrupts too: FPSCR is reloaded from FPDSCR on exception entry (dsp/core/denormal.h) */
    FPU->FPDSCR |= FPU_FPDSCR_FZ_Msk;
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEPD1; /* keep the debugger connected while the core sleeps in __WFI */
    /* the unique id, for the M4's network (it cannot read it itself) */
    SHARED->uid[0] = HAL_GetUIDw0();
    SHARED->uid[1] = HAL_GetUIDw1();
    SHARED->uid[2] = HAL_GetUIDw2();
    __DMB();
    SHARED->uid_ok = SHARED_UID_OK(SHARED->uid[0]);
    memset(_sram_d1, 0, (size_t)(_eram_d1 - _sram_d1));

    /* the M7's half of the mailbox (the M4 may already have written its own: its sequence is the baseline) */
    SHARED->i2c4_free = 0;
    SHARED->preset = -1;
    SHARED->overload = -1;
    SHARED->heartbeat = 0;
    app_health.overload = -1;
    app_req_seen = SHARED->m4_magic == SHARED_MAGIC_M4 ? SHARED->req_seq : 0;
    SHARED->ack_seq = app_req_seen;
    bench_seen = SHARED->bench_cmd >> 8; /* a command of a previous run is not a new one */
    SHARED->bench_preset = -1;
    SHARED->bench_ack = bench_seen;
    LINKQ->tail = LINKQ->head; /* events queued before this start are stale */
    net_watch_init();
    seq_taps = SHARED->demo_taps; /* taps of a previous run do not start the demo */
    SHARED->demo_playing = 0;
    app_kit_seen = SHARED->kit_seq; /* a kit of a previous run is not in the SDRAM any more */
    SHARED->kit_ack_seq = app_kit_seen;
    SHARED->done_seq = app_req_seen;
    __DMB();
    SHARED->m7_magic = SHARED_MAGIC_M7;

    preset_apply(preset_find(APP_DEFAULT_PRESET));
    engine_warm();
    app_gain = 0.0f;
    app_target = 1.0f;
    app_state = APP_PLAYING;
    app_health.audio_status = audio_start(app_render, 70);
    wdg_seen = audio_buffers();
    wdg_start();
    __DMB();
    SHARED->i2c4_free = 1; /* the codec is set up: the M7 does not touch I2C4 any more */
}

void app_loop(void)
{
    const uint32_t bench = SHARED->bench_cmd;
    if ((bench >> 8) != bench_seen) {
        bench_seen = bench >> 8;
        bench_run(bench & 0xFFu);
        SHARED->bench_ack = bench_seen;
    }
    wdg_poll();
    kit_take();
    net_watch();
    usb_watch();
    knobs_poll();
    const int req = shared_request();
    if (req >= 0) {
        app_pending = req;
        app_target = 0.0f; /* fade out from wherever the gain is */
    }
    if (app_overload) { /* the interrupt muted the current preset: report it, wait for another one */
        app_overload = 0;
        app_health.overload = app_health.preset;
    }
    if (app_state == APP_SILENT && app_pending >= 0) {
        if (app_pending == (int)SHARED_PRESET_NET)
            preset_apply_net();
        else
            preset_apply(app_pending);
        engine_warm();
        app_pending = -1;
        app_health.overload = -1;
        app_target = 1.0f;
        __DMB(); /* the synth is ready before the interrupt takes it back */
        app_state = APP_PLAYING;
    }

    app_health.load_avg_pct = 100.0f * audio_load_avg();
    app_health.load_peak_pct = 100.0f * audio_load_peak();
    app_health.overruns = audio_overruns();
    app_health.audio_errors = audio_errors();
    app_health.stack_free_bytes = stack_free();
    shared_status();
    __WFI(); /* sleep until the next interrupt (the audio DMA wakes the core every 2.7 ms) */
}
