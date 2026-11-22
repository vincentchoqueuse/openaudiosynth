/* app_m4.c — the M4 application: a preset selector on the 4.3" touch screen (gfx.h: double-buffered RGB565
 * frames in the SDRAM, anti-aliased text). Two screens: a grid of instruments (one tile per engine, in its own
 * color, with its number of presets), then the presets of the chosen instrument, with their descriptions, in
 * a list that scrolls with the finger; the "<" of the top bar goes back. A tap on a preset asks the M7 for it
 * through the mailbox (shared.h). The top bar always shows the preset playing, the M7's DSP load as a meter,
 * its overruns and its liveness. Two views of the preset playing, as tabs in the top bar: a tap on its name (or
 * on the preset playing in the list) opens its controls (the eight knobs on the macros: the parameter each one
 * moves, its value, lit as it turns; the M7 fills them, knobs.h), a tap on the meter its signal (scope_view.h:
 * oscilloscope and spectrum); "<" goes back to the list.
 * The touch panel shares I2C4 with the audio codec: it is started only once the M7 has set the codec up and
 * released the bus (shared.h, i2c4_free). A frame is drawn only when something on it changed, to keep the
 * memory buses quiet for the audio.
 * The board is also on the network (net.h): presets asked for over Ethernet are posted here like taps, and
 * the top bar shows the cable's state and the sample kit received. */
#include "app_m4.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "main.h"
#include "net.h"
#include "usbh_midi.h"
#include "presets.h"
#include "scope_view.h"
#include "shared.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_ts.h"

#define BAR_H 48
#define BACK_W 170                 /* touch zone of the "<" in the top bar */
#define ROW_H 48
#define LIST_Y (BAR_H + 2)
#define LIST_H (GFX_H - LIST_Y)
#define GRID_COLS 3
#define GAP 8
#define MAX_INST 12
#define MAX_PRESETS 256
#define TAP_MOVE 10                /* pixels: a touch that moved less is a tap */
#define STATUS_MS 250              /* top bar refresh */
#define SCOPE_MS 33                /* oscilloscope and spectrum: 30 frames per second */
#define METER_X 340                /* the DSP meter, also the button of the audio view */
#define KNOB_LIT_MS 600            /* a knob just turned stays lit that long */
#define M7_TIMEOUT_MS 1000         /* no heartbeat for that long: the M7 is shown as not responding */
#define TS_FAIL_RESET 1            /* failed touch reads before the panel is started again (each costs the BSP's 1 s timeout) */

/* palette: cold, dark */
#define C_BG 0x0B0E12u
#define C_BAR 0x12171Du
#define C_TILE 0x161C23u
#define C_ROW_ON 0x18222Cu
#define C_LINE 0x1E262Fu
#define C_TEXT 0xE8EEF4u
#define C_DIM 0x7C8894u
#define C_FAINT 0x4A545Eu
#define C_OK 0x4FD18Bu
#define C_WARN 0xE5A33Du
#define C_ERR 0xE5534Bu

typedef enum { SCREEN_INSTRUMENTS, SCREEN_PRESETS, SCREEN_SCOPE, SCREEN_KNOBS } screen_t;

/* an instrument = an engine with its presets, in the order of presets[] (alphabetical) */
typedef struct {
    const char *name;
    int first, count; /* its slice of ui.order */
} inst_t;

typedef struct {
    int lcd_ok, ts_ok;
    screen_t screen;
    screen_t back_to;        /* where the audio and controls views return */
    int inst;                /* instrument shown on SCREEN_PRESETS */
    int scroll;              /* list scroll, pixels */
    int back_scroll;         /* scroll of the screen the audio view returns to */
    int pressed;             /* touch held */
    int touch_x0, touch_y0, touch_scroll0, touch_moved;
    uint32_t seq;            /* last request sent */
    int requested;           /* preset asked for, -1 none */
    inst_t insts[MAX_INST];
    int n_inst;
    int order[MAX_PRESETS];  /* preset indices grouped by instrument */
    /* what the last frame showed, to draw a new one only on change */
    int dirty;
    char shown[400];
    uint32_t last_status, last_heartbeat, heartbeat_tick;
    uint32_t demo_shown;     /* demo state on the last frame */
    uint32_t knob_moves[SHARED_KNOBS], knob_tick[SHARED_KNOBS]; /* last turn of each knob seen, and when */
    /* touch panel health (read with the debugger): failed reads, re-initializations, slowest read in ms */
    uint32_t ts_reads, ts_errors, ts_resets, ts_max_ms, ts_fail_streak;
} ui_t;

static ui_t ui;

/* ---------------- instruments ---------------- */

static uint32_t engine_color(const char *e)
{
    static const struct {
        const char *name;
        uint32_t rgb;
    } map[] = {{"va", 0x5AB4E0u},      {"mono", 0xE5A33Du},   {"fm6", 0x9B8CFFu},
               {"modal", 0x4FD1C5u},   {"poly", 0xF2777Au},   {"acid", 0xB8E05Au},
               {"drums", 0xE5534Bu},   {"coupled", 0x7FA7FFu}, {"selfosc", 0xD98CFFu},
               {"sampler", 0xF08FB8u}, {"drone", 0x8FC9E8u},
               {"micro", 0xE8E2D0u}};
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(map[i].name, e))
            return map[i].rgb;
    return 0x9AA5B1u;
}

static void insts_build(void)
{
    /* the usual families first (subtractive, then FM, physical, drums, the experimental ones), then any other
     * engine in the order of its first preset */
    static const char *const family[] = {"va", "mono", "poly", "acid", "fm6", "modal", "drums", "sampler", "coupled", "selfosc", "drone", "micro"};
    int n = 0, k = 0;
    for (unsigned f = 0; f < sizeof family / sizeof family[0] && n < MAX_INST; f++)
        for (int i = 0; i < preset_count; i++)
            if (!strcmp(presets[i].engine, family[f])) {
                ui.insts[n++].name = family[f];
                break;
            }
    for (int i = 0; i < preset_count && n < MAX_INST; i++) {
        int known = 0;
        for (int j = 0; j < n; j++)
            known |= !strcmp(ui.insts[j].name, presets[i].engine);
        if (!known)
            ui.insts[n++].name = presets[i].engine;
    }
    for (int j = 0; j < n; j++) {
        ui.insts[j].first = k;
        for (int i = 0; i < preset_count && k < MAX_PRESETS; i++)
            if (!strcmp(presets[i].engine, ui.insts[j].name))
                ui.order[k++] = i;
        ui.insts[j].count = k - ui.insts[j].first;
    }
    ui.n_inst = n;
}

static int inst_of(int preset)
{
    for (int j = 0; preset >= 0 && preset < preset_count && j < ui.n_inst; j++)
        if (!strcmp(presets[preset].engine, ui.insts[j].name))
            return j;
    return -1;
}

/* preset index of a row of the current instrument, or -1 */
static int preset_at(int row)
{
    const inst_t *in = &ui.insts[ui.inst];
    return row >= 0 && row < in->count ? ui.order[in->first + row] : -1;
}

static int max_scroll(void)
{
    const int h = ui.insts[ui.inst].count * ROW_H - LIST_H;
    return h > 0 ? h : 0;
}

/* ---------------- mailbox ---------------- */

static void mailbox_init(void)
{
    /* SRAM4 keeps its content across a reset: continue the sequence of the previous run, so the M7 does not
     * take a stale request for a new one */
    if (SHARED->m4_magic != SHARED_MAGIC_M4) {
        SHARED->req_preset = 0;
        SHARED->req_seq = 0;
        __DMB();
        SHARED->m4_magic = SHARED_MAGIC_M4;
    }
    ui.seq = SHARED->req_seq;
}

static int m7_ready(void) { return SHARED->m7_magic == SHARED_MAGIC_M7; }

/* a built-in preset, or SHARED_PRESET_NET for the one received over the network */
static void request_preset(int index)
{
    const int net = index == (int)SHARED_PRESET_NET;
    if (!net && (index < 0 || index >= preset_count))
        return;
    SHARED->req_preset = (uint32_t)index;
    __DMB(); /* the index before the sequence number */
    SHARED->req_seq = ++ui.seq;
    ui.requested = net ? SHARED_PLAYING_NET : index;
    ui.dirty = 1;
}

/* ---------------- drawing ---------------- */

typedef struct {
    int alive, playing, overload;
    float load;
    uint32_t overruns;
    int32_t audio_status;
} status_t;

static status_t status_read(void)
{
    const uint32_t now = HAL_GetTick(), hb = SHARED->heartbeat;
    if (hb != ui.last_heartbeat) {
        ui.last_heartbeat = hb;
        ui.heartbeat_tick = now;
    }
    status_t st = {0};
    st.alive = m7_ready() && now - ui.heartbeat_tick < M7_TIMEOUT_MS;
    st.playing = st.alive ? SHARED->preset : -1;
    st.overload = st.alive ? SHARED->overload : -1;
    st.load = SHARED->load_avg_pct;
    st.overruns = SHARED->overruns;
    st.audio_status = SHARED->audio_status;
    return st;
}

static void draw_bar(const status_t *st)
{
    gfx_fill(0, 0, GFX_W, BAR_H, C_BAR);
    gfx_fill(0, BAR_H, GFX_W, 1, C_LINE);
    if (ui.screen != SCREEN_INSTRUMENTS) {
        const char *title = ui.screen == SCREEN_SCOPE   ? "signal"
                            : ui.screen == SCREEN_KNOBS ? "controls"
                                                        : ui.insts[ui.inst].name;
        const uint32_t c = ui.screen == SCREEN_PRESETS ? engine_color(ui.insts[ui.inst].name) : C_TEXT;
        int x = gfx_text(&font_big, 12, 12, "<", c, 0);
        gfx_text(&font_title, x + 8, 14, title, C_TEXT, BACK_W - x - 16);
    } else {
        const int tx = gfx_text(&font_big, 14, 6, "OASynth", C_TEXT, 0); /* x after the title */
        const int demo = SHARED->demo_playing != 0; /* a tap on the title toggles it */
        gfx_text(&font_small, 15, 31, demo ? "demo on" : "demo off", demo ? C_OK : C_FAINT, 0);
        /* the network: cable state, then the sample kit received */
        const net_status_t ns = net_status();
        const int nx = tx + 10;
        char line[40];
        if (ns.kit_count)
            snprintf(line, sizeof(line), "%s  kit %lu", ns.up ? "LAN" : "no LAN", (unsigned long)ns.kit_count);
        else
            snprintf(line, sizeof(line), "%s", ns.up ? "LAN" : "no LAN");
        gfx_round_rect(nx, 10, 6, 6, 3, ns.up ? C_OK : C_FAINT);
        gfx_text(&font_small, nx + 10, 7, line, ns.up ? C_DIM : C_FAINT, BACK_W - nx - 14);
        /* the USB MIDI device: its name once it plays */
        const usbh_midi_status_t us = usbh_midi_status();
        if (us.state != USBH_MIDI_OFF) {
            const int on = us.state == USBH_MIDI_RUNNING;
            const char *what = us.state == USBH_MIDI_EMPTY      ? "no USB"
                               : us.state == USBH_MIDI_ENUM     ? "USB..."
                               : us.state == USBH_MIDI_NOT_MIDI ? "USB: not MIDI"
                               : us.state == USBH_MIDI_ERROR    ? "USB: error"
                               : us.name[0]                     ? us.name
                                                                : "USB MIDI";
            char msg[48];
            const uint8_t st0 = us.last[0], kind = st0 & 0xF0u;
            if (on && kind == 0xB0u)
                snprintf(msg, sizeof(msg), "%s  CC%u=%u ch%u", what, us.last[1], us.last[2], (st0 & 15u) + 1u);
            else if (on && (kind == 0x90u || kind == 0x80u))
                snprintf(msg, sizeof(msg), "%s  note %u ch%u", what, us.last[1], (st0 & 15u) + 1u);
            else
                snprintf(msg, sizeof(msg), "%s", what);
            gfx_round_rect(nx, 29, 6, 6, 3, on ? C_OK : us.state == USBH_MIDI_ERROR ? C_ERR : C_FAINT);
            gfx_text(&font_small, nx + 10, 26, msg, on ? C_DIM : C_FAINT, BACK_W - nx - 14);
        }
    }

    /* the preset playing */
    const int px = BACK_W + 4;
    gfx_text(&font_small, px, 7, "playing", C_FAINT, 0);
    if (st->playing >= 0 && st->playing < preset_count) {
        const preset_t *p = &presets[st->playing];
        gfx_round_rect(px, 26, 6, 14, 3, engine_color(p->engine));
        gfx_text(&font_body, px + 12, 24, p->name, C_TEXT, 330 - px);
    } else if (st->playing == SHARED_PLAYING_NET) { /* a preset sent over the network: not in presets[] */
        gfx_round_rect(px, 26, 6, 14, 3, C_TEXT);
        gfx_text(&font_body, px + 12, 24, NET_PRESET->name, C_TEXT, 330 - px);
    } else {
        gfx_text(&font_body, px, 24, "-", C_DIM, 0);
    }

    /* the two views of the preset playing, as tabs: its name opens the controls, the meter the signal */
    if (ui.screen == SCREEN_KNOBS || ui.screen == SCREEN_SCOPE) {
        const uint32_t tc = st->playing >= 0 && st->playing < preset_count ? engine_color(presets[st->playing].engine)
                                                                             : C_TEXT;
        if (ui.screen == SCREEN_KNOBS)
            gfx_fill(BACK_W, BAR_H - 3, METER_X - BACK_W, 3, tc);
        else
            gfx_fill(METER_X, BAR_H - 3, GFX_W - METER_X, 3, tc);
    }

    /* DSP load meter, or what is wrong */
    const int mx = METER_X + 12, mw = 116;
    char label[32];
    uint32_t col;
    if (!st->alive) {
        snprintf(label, sizeof(label), "M7 not responding");
        col = C_ERR;
    } else if (st->audio_status != 0) {
        snprintf(label, sizeof(label), "audio error %ld", (long)st->audio_status);
        col = C_ERR;
    } else {
        snprintf(label, sizeof(label), "DSP %d%%", (int)(st->load + 0.5f));
        col = st->load >= 85.0f ? C_ERR : st->load >= 60.0f ? C_WARN : C_OK;
    }
    gfx_text(&font_small, mx, 7, label, st->alive && !st->audio_status ? C_DIM : C_ERR, mw);
    if (st->overruns) {
        snprintf(label, sizeof(label), "x%lu", (unsigned long)st->overruns);
        gfx_text(&font_small, mx + mw - gfx_text_width(&font_small, label), 7, label, C_ERR, 0);
    }
    gfx_round_rect(mx, 28, mw, 8, 4, C_LINE);
    int fill = (int)((float)mw * (st->alive ? st->load : 0.0f) / 100.0f);
    fill = fill > mw ? mw : fill;
    if (fill >= 8)
        gfx_round_rect(mx, 28, fill, 8, 4, col);
}

static void draw_grid(const status_t *st)
{
    const int rows = (ui.n_inst + GRID_COLS - 1) / GRID_COLS;
    const int tw = (GFX_W - GAP * (GRID_COLS + 1)) / GRID_COLS;
    const int th = (GFX_H - BAR_H - GAP * (rows + 1)) / (rows ? rows : 1);
    const int playing_inst = inst_of(st->playing);
    for (int j = 0; j < ui.n_inst; j++) {
        const int x = GAP + (j % GRID_COLS) * (tw + GAP), y = BAR_H + GAP + (j / GRID_COLS) * (th + GAP);
        const uint32_t c = engine_color(ui.insts[j].name);
        const int on = j == playing_inst;
        gfx_round_rect(x, y, tw, th, 10, on ? gfx_mix(C_TILE, c, 40) : C_TILE);
        gfx_round_rect(x + 12, y + 14, 4, th - 28, 2, c);
        gfx_text(&font_title, x + 26, y + th / 2 - 20, ui.insts[j].name, on ? c : C_TEXT, tw - 36);
        char count[24];
        snprintf(count, sizeof(count), "%d preset%s", ui.insts[j].count, ui.insts[j].count > 1 ? "s" : "");
        if (on)
            gfx_text(&font_small, x + 26, y + th / 2 + 4, presets[st->playing].name, C_TEXT, tw - 36);
        else
            gfx_text(&font_small, x + 26, y + th / 2 + 4, count, C_DIM, tw - 36);
    }
}

static void draw_list(const status_t *st)
{
    const uint32_t c = engine_color(ui.insts[ui.inst].name);
    gfx_clip(0, LIST_Y, GFX_W, LIST_H);
    const int first = ui.scroll / ROW_H;
    for (int r = first; r < ui.insts[ui.inst].count && r * ROW_H - ui.scroll < LIST_H; r++) {
        const int y = LIST_Y + r * ROW_H - ui.scroll, index = preset_at(r);
        const int playing = index == st->playing, over = index == st->overload, asked = index == ui.requested;
        if (playing || asked)
            gfx_round_rect(6, y + 3, GFX_W - 18, ROW_H - 6, 8, playing ? gfx_mix(C_BG, c, 36) : C_ROW_ON);
        if (playing)
            gfx_round_rect(12, y + 12, 4, ROW_H - 24, 2, over ? C_ERR : c);
        const preset_t *p = &presets[index];
        gfx_text(&font_body, 26, y + 7, p->name, over ? C_ERR : playing ? c : C_TEXT, 300);
        if (over)
            gfx_text(&font_small, 336, y + 9, "too heavy", C_ERR, 0);
        gfx_text(&font_small, 26, y + 28, p->description, C_DIM, GFX_W - 60);
        if (!playing && !asked)
            gfx_fill(26, y + ROW_H - 1, GFX_W - 52, 1, C_LINE);
    }
    gfx_noclip();
    const int total = ui.insts[ui.inst].count * ROW_H;
    if (total > LIST_H) { /* scroll indicator */
        const int len = LIST_H * LIST_H / total, pos = (LIST_H - len) * ui.scroll / max_scroll();
        gfx_round_rect(GFX_W - 6, LIST_Y + pos, 3, len, 1, C_FAINT);
    }
}

static uint32_t playing_color(const status_t *st)
{
    return st->playing >= 0 && st->playing < preset_count ? engine_color(presets[st->playing].engine) : C_DIM;
}

/* a knob is lit for a moment after it turned */
static int knob_lit(int k, uint32_t now)
{
    const uint32_t m = SHARED->knob_moves[k];
    if (m != ui.knob_moves[k]) {
        ui.knob_moves[k] = m;
        if (m)
            ui.knob_tick[k] = now;
        else
            ui.knob_tick[k] = now - KNOB_LIT_MS; /* a new preset */
    }
    return m && now - ui.knob_tick[k] < KNOB_LIT_MS;
}

/* the eight knobs: 4 x 2 tiles, each with its number and CC, the parameter it moves, its value and a bar */
static void draw_knobs(const status_t *st)
{
    const uint32_t c = playing_color(st), now = HAL_GetTick();
    const int cols = 4, tw = (GFX_W - GAP * (cols + 1)) / cols, th = (GFX_H - BAR_H - GAP * 3) / 2;
    for (int k = 0; k < SHARED_KNOBS; k++) {
        const int x = GAP + (k % cols) * (tw + GAP), y = BAR_H + GAP + (k / cols) * (th + GAP);
        const int lit = knob_lit(k, now), named = SHARED->knob_name[k][0] != 0;
        gfx_round_rect(x, y, tw, th, 10, lit ? gfx_mix(C_TILE, c, 70) : C_TILE);
        char line[24];
        if (SHARED->knob_cc[k])
            snprintf(line, sizeof(line), "%d   CC %u", k + 1, SHARED->knob_cc[k]);
        else
            snprintf(line, sizeof(line), "%d", k + 1);
        gfx_text(&font_small, x + 10, y + 7, line, C_FAINT, tw - 20);
        char name[sizeof SHARED->knob_name[k]], text[sizeof SHARED->knob_text[k]];
        for (size_t i = 0; i < sizeof name; i++)
            name[i] = SHARED->knob_name[k][i];
        for (size_t i = 0; i < sizeof text; i++)
            text[i] = SHARED->knob_text[k][i];
        name[sizeof name - 1] = text[sizeof text - 1] = 0;
        gfx_text(&font_body, x + 10, y + 25, named ? name : "-", named ? C_TEXT : C_FAINT, tw - 20);
        if (!named)
            continue;
        gfx_text(&font_title, x + 10, y + 49, text, lit ? C_TEXT : c, tw - 20);
        float v = SHARED->knob_value[k];
        v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
        const int bw = tw - 20, fill = (int)((float)bw * v + 0.5f);
        gfx_round_rect(x + 10, y + th - 16, bw, 6, 3, C_LINE);
        if (fill >= 6)
            gfx_round_rect(x + 10, y + th - 16, fill, 6, 3, c);
        /* soft takeover: until the knob reaches the value, where it is, and a hint */
        const float phys = SHARED->knob_phys[k];
        if (!SHARED->knob_caught[k] && phys >= 0.0f && phys <= 1.0f) {
            gfx_fill(x + 10 + (int)((float)(bw - 3) * phys), y + th - 21, 3, 16, C_TEXT);
            gfx_text(&font_small, x + tw - 10 - gfx_text_width(&font_small, phys < v ? "turn up" : "turn down"), y + 7,
                     phys < v ? "turn up" : "turn down", C_WARN, 0);
        }
    }
}

static void draw_frame(const status_t *st)
{
    gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
    draw_bar(st);
    if (ui.screen == SCREEN_INSTRUMENTS)
        draw_grid(st);
    else if (ui.screen == SCREEN_PRESETS)
        draw_list(st);
    else if (ui.screen == SCREEN_KNOBS)
        draw_knobs(st);
    else
        scope_view_draw(8, BAR_H + 8, GFX_W - 16, GFX_H - BAR_H - 12, playing_color(st));
    gfx_present();
}

/* draws a frame if anything it shows changed (the load is refreshed at most every STATUS_MS) */
static void refresh(void)
{
    const uint32_t now = HAL_GetTick();
    const status_t st = status_read();
    if (ui.screen == SCREEN_SCOPE) { /* moving pictures: a frame at a steady rate */
        if (!ui.dirty && now - ui.last_status < SCOPE_MS)
            return;
        ui.last_status = now;
        ui.dirty = 0;
        draw_frame(&st);
        return;
    }
    if (!ui.dirty && now - ui.last_status < (ui.screen == SCREEN_KNOBS ? SCOPE_MS : STATUS_MS))
        return;
    ui.last_status = now;
    char key[400];
    const net_status_t ns = net_status();
    const usbh_midi_status_t us = usbh_midi_status();
    snprintf(key, sizeof(key), "%d|%d|%d|%d|%d|%d|%d|%d|%lu|%ld|%d|%d|%lu|%lu|%d|%s", ui.screen, ui.inst, ui.scroll,
             st.alive, st.playing, st.overload, ui.requested, (int)(st.load + 0.5f), (unsigned long)st.overruns,
             (long)st.audio_status, ui.ts_ok, ns.up, (unsigned long)ns.kit_count, (unsigned long)SHARED->kit_seq,
             (int)us.state, us.name);
    if (us.last[0]) { /* the last message: redrawn as a knob turns */
        const size_t l = strlen(key);
        snprintf(key + l, sizeof(key) - l, "|%u.%u.%u", us.last[0], us.last[1], us.last[2]);
    }
    if (ui.screen == SCREEN_KNOBS) { /* the knobs: redrawn as they turn, and as a lit one goes out */
        size_t l = strlen(key);
        l += (size_t)snprintf(key + l, sizeof(key) - l, "|k%lu", (unsigned long)SHARED->knob_seq);
        for (int k = 0; k < SHARED_KNOBS && l < sizeof(key); k++)
            l += (size_t)snprintf(key + l, sizeof(key) - l, "|%d.%d.%.11s.%d.%d", (int)(SHARED->knob_value[k] * 1000.0f),
                                  knob_lit(k, now), (const char *)SHARED->knob_text[k],
                                  (int)(SHARED->knob_phys[k] * 127.0f), SHARED->knob_caught[k]);
    }
    if (SHARED->demo_playing != ui.demo_shown) {
        ui.demo_shown = SHARED->demo_playing;
        ui.dirty = 1;
    }
    if (!ui.dirty && !strcmp(key, ui.shown))
        return;
    snprintf(ui.shown, sizeof(ui.shown), "%s", key);
    ui.dirty = 0;
    draw_frame(&st);
}

static void show(screen_t screen, int inst)
{
    ui.screen = screen;
    ui.inst = inst;
    ui.scroll = 0;
    if (screen == SCREEN_PRESETS) { /* open on the preset playing when it belongs to this instrument */
        const int playing = SHARED->preset;
        for (int r = 0; r < ui.insts[inst].count; r++)
            if (preset_at(r) == playing) {
                const int s = r * ROW_H - LIST_H / 2 + ROW_H / 2, m = max_scroll();
                ui.scroll = s < 0 ? 0 : s > m ? m : s;
            }
    }
    ui.dirty = 1;
}

/* ---------------- touch ---------------- */

/* the controls or the signal of the preset playing; "<" goes back to the screen they were opened from */
static void open_view(screen_t view)
{
    if (ui.screen != SCREEN_SCOPE && ui.screen != SCREEN_KNOBS) {
        ui.back_to = ui.screen;
        ui.back_scroll = ui.scroll;
    }
    show(view, ui.inst);
}

static void on_tap(int x, int y)
{
    if (y < BAR_H) {
        const int view = ui.screen == SCREEN_SCOPE || ui.screen == SCREEN_KNOBS;
        if (view && x < BACK_W) { /* "<": back to the list or the grid */
            show(ui.back_to, ui.inst);
            ui.scroll = ui.back_scroll;
        } else if (x >= METER_X) { /* the meter opens the signal (oscilloscope and spectrum) */
            open_view(SCREEN_SCOPE);
        } else if (x >= BACK_W) { /* the preset playing opens its controls */
            if (SHARED->preset != -1)
                open_view(SCREEN_KNOBS);
        } else if (ui.screen == SCREEN_PRESETS && x < BACK_W) {
            show(SCREEN_INSTRUMENTS, 0);
        } else if (ui.screen == SCREEN_INSTRUMENTS && x < 110) { /* the title: demo arpeggio on / off */
            SHARED->demo_taps++;
            ui.dirty = 1;
        }
        return;
    }
    if (ui.screen == SCREEN_INSTRUMENTS) {
        const int rows = (ui.n_inst + GRID_COLS - 1) / GRID_COLS;
        const int th = (GFX_H - BAR_H - GAP * (rows + 1)) / (rows ? rows : 1);
        const int col = x * GRID_COLS / GFX_W, row = (y - BAR_H - GAP / 2) / (th + GAP);
        const int j = row * GRID_COLS + col;
        if (row >= 0 && row < rows && j < ui.n_inst)
            show(SCREEN_PRESETS, j);
    } else if (ui.screen == SCREEN_PRESETS && y >= LIST_Y) {
        const int index = preset_at((y - LIST_Y + ui.scroll) / ROW_H);
        if (index >= 0 && index == SHARED->preset) /* the preset playing, tapped again: its controls */
            open_view(SCREEN_KNOBS);
        else
            request_preset(index);
    }
}

static void touch_poll(void)
{
    if (!ui.ts_ok) {
        if (m7_ready() && SHARED->i2c4_free) { /* the codec is set up: I2C4 is free */
            TS_Init_t ts = {.Width = GFX_W, .Height = GFX_H, .Orientation = TS_SWAP_NONE, .Accuracy = 5};
            ui.ts_ok = BSP_TS_Init(0, &ts) == BSP_ERROR_NONE;
        }
        return;
    }
    TS_State_t st;
    const uint32_t t0 = HAL_GetTick();
    const int32_t ret = BSP_TS_GetState(0, &st);
    const uint32_t dt = HAL_GetTick() - t0;
    ui.ts_reads++;
    if (dt > ui.ts_max_ms)
        ui.ts_max_ms = dt;
    if (ret != BSP_ERROR_NONE) {
        /* the touch controller stopped answering: start it again (BSP_TS_Init probes both controllers and
         * both addresses of the GT911 again) */
        ui.ts_errors++;
        if (++ui.ts_fail_streak >= TS_FAIL_RESET) {
            ui.ts_fail_streak = 0;
            ui.ts_resets++;
            BSP_TS_DeInit(0);
            ui.ts_ok = 0; /* touch_poll starts it again on the next call */
        }
        ui.pressed = 0;
        return;
    }
    ui.ts_fail_streak = 0;
    const int x = (int)st.TouchX, y = (int)st.TouchY;
    if (st.TouchDetected) {
        if (!ui.pressed) {
            ui.pressed = 1;
            ui.touch_x0 = x;
            ui.touch_y0 = y;
            ui.touch_scroll0 = ui.scroll;
            ui.touch_moved = 0;
        }
        const int dy = ui.touch_y0 - y;
        if (dy > TAP_MOVE || dy < -TAP_MOVE)
            ui.touch_moved = 1;
        if (ui.touch_moved && ui.screen == SCREEN_PRESETS) {
            const int s = ui.touch_scroll0 + dy, m = max_scroll();
            const int clamped = s < 0 ? 0 : s > m ? m : s;
            if (clamped != ui.scroll) {
                ui.scroll = clamped;
                ui.dirty = 1;
            }
        }
    } else if (ui.pressed) {
        ui.pressed = 0;
        if (!ui.touch_moved)
            on_tap(ui.touch_x0, ui.touch_y0);
    }
}

/* ---------------- application ---------------- */

void app_m4_init(void)
{
    memset(&ui, 0, sizeof(ui));
    ui.requested = -1;
    insts_build();
    mailbox_init();
    ui.lcd_ok = BSP_LCD_InitEx(0, LCD_ORIENTATION_LANDSCAPE, LCD_PIXEL_FORMAT_RGB565, GFX_W, GFX_H) == BSP_ERROR_NONE;
    if (!ui.lcd_ok) {
        net_init(0); /* no SDRAM: the network without sample kits */
        usbh_midi_init();
        return;
    }
    gfx_init();
    net_init(1); /* the SDRAM is up (the display's frames live there): kits can be received */
    usbh_midi_init();
    scope_view_init();
    show(SCREEN_INSTRUMENTS, 0);
    refresh();
}

void app_m4_loop(void)
{
    touch_poll();
    net_poll();
    usbh_midi_poll();
    const int req = net_request();
    if (req >= 0) {
        request_preset(req);
        net_request_posted();
    }
    if (!ui.lcd_ok) {
        HAL_Delay(20);
        return;
    }
    if (ui.requested >= 0 && SHARED->preset == ui.requested) {
        ui.requested = -1; /* done */
        ui.dirty = 1;
    }
    refresh();
    HAL_Delay(15); /* ~50 Hz touch polling with the drawing */
}
