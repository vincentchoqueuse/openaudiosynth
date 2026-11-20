/* scope_view.c — oscilloscope and spectrum of the M7's output (see scope_view.h). The FFT is the project's
 * radix-2 transform (dsp/core/fft.c); the window and the column map are computed once at init. This is
 * display code on the M4, outside the audio path: libm is allowed here. */
#include "scope_view.h"

#include <math.h>

#include "core/fft.h"
#include "gfx.h"
#include "shared.h"

#define FFT_N 2048 /* 23 Hz per bin */
#define SR 48000.0f
#define F_LO 30.0f
#define F_HI 20000.0f
#define DB_FLOOR -90.0f
#define FALL_DB 2.5f   /* per frame: how fast the peaks fall back */
#define SCOPE_SPAN 960 /* samples shown by the oscilloscope: 20 ms */

#define C_GRID 0x1E262Fu
#define C_LABEL 0x4A545Eu
#define C_PANEL 0x0F1318u

static float window[FFT_N], re[FFT_N], im[FFT_N];
static float shown_db[GFX_W];
static float wave_gain = 4.0f; /* follows the level, so a quiet sound still fills the oscilloscope */
static int16_t snap[SCOPE_LEN];

void scope_view_init(void)
{
    for (int i = 0; i < FFT_N; i++)
        window[i] = 0.5f - 0.5f * cosf(6.28318531f * (float)i / (float)FFT_N);
    for (int i = 0; i < GFX_W; i++)
        shown_db[i] = DB_FLOOR;
}

/* a copy of the ring, oldest sample first (the M7 keeps writing while we draw) */
static void snapshot(void)
{
    const uint32_t end = SCOPE->pos;
    for (uint32_t k = 0; k < SCOPE_LEN; k++)
        snap[k] = SCOPE->buf[(end + k) & (SCOPE_LEN - 1u)];
}

static void vline(int x, int y0, int y1, uint32_t rgb)
{
    if (y0 > y1) {
        const int t = y0;
        y0 = y1;
        y1 = t;
    }
    gfx_fill(x, y0, 1, y1 - y0 + 1, rgb);
}

static void draw_wave(int x, int y, int w, int h, uint32_t rgb)
{
    gfx_round_rect(x, y, w, h, 8, C_PANEL);
    gfx_fill(x + 8, y + h / 2, w - 16, 1, C_GRID);
    /* trigger: the last rising zero crossing that still leaves SCOPE_SPAN samples after it */
    int start = SCOPE_LEN - SCOPE_SPAN - 1;
    for (int k = SCOPE_LEN - SCOPE_SPAN - 1; k > SCOPE_LEN / 2; k--)
        if (snap[k - 1] < 0 && snap[k] >= 0) {
            start = k;
            break;
        }
    int peak = 1;
    for (int k = start; k < start + SCOPE_SPAN; k++) {
        const int a = snap[k] < 0 ? -snap[k] : snap[k];
        peak = a > peak ? a : peak;
    }
    float target = 0.8f * 32768.0f / (float)peak;
    target = target < 1.0f ? 1.0f : target > 64.0f ? 64.0f : target;
    wave_gain += (target - wave_gain) * (target < wave_gain ? 0.5f : 0.1f); /* quick to shrink, slow to grow */
    const float scale = (float)(h / 2 - 6) / 32768.0f * wave_gain;
    int prev = 0;
    for (int c = 0; c < w - 16; c++) {
        const int a = start + c * SCOPE_SPAN / (w - 16), b = start + (c + 1) * SCOPE_SPAN / (w - 16);
        int lo = 32767, hi = -32768; /* min and max of the samples under the column: no aliasing */
        for (int k = a; k < b && k < (int)SCOPE_LEN; k++) {
            lo = snap[k] < lo ? snap[k] : lo;
            hi = snap[k] > hi ? snap[k] : hi;
        }
        int y0 = y + h / 2 - (int)((float)hi * scale), y1 = y + h / 2 - (int)((float)lo * scale);
        y0 = y0 < y + 2 ? y + 2 : y0 > y + h - 3 ? y + h - 3 : y0;
        y1 = y1 < y + 2 ? y + 2 : y1 > y + h - 3 ? y + h - 3 : y1;
        if (c > 0) { /* join with the previous column */
            y0 = prev < y0 ? prev : y0;
            y1 = prev > y1 ? prev : y1;
        }
        vline(x + 8 + c, y0, y1, rgb);
        prev = (y0 + y1) / 2;
    }
}

static void draw_spectrum(int x, int y, int w, int h, uint32_t rgb)
{
    gfx_round_rect(x, y, w, h, 8, C_PANEL);
    for (int i = 0; i < FFT_N; i++) {
        re[i] = (float)snap[SCOPE_LEN - FFT_N + i] * (1.0f / 32768.0f) * window[i];
        im[i] = 0.0f;
    }
    fft_complex(re, im, FFT_N, 0);

    const int pw = w - 16, ph = h - 20, px = x + 8, py = y + 6;
    /* grid: 100 Hz, 1 kHz, 10 kHz, and -30 / -60 dB */
    static const float marks[] = {100.0f, 1000.0f, 10000.0f};
    static const char *const names[] = {"100", "1k", "10k"};
    const float lspan = logf(F_HI / F_LO);
    for (int m = 0; m < 3; m++) {
        const int gx = px + (int)((float)pw * logf(marks[m] / F_LO) / lspan);
        gfx_fill(gx, py, 1, ph, C_GRID);
        gfx_text(&font_small, gx + 3, py + ph + 1, names[m], C_LABEL, 0);
    }
    for (int d = 1; d < 3; d++)
        gfx_fill(px, py + ph * d / 3, pw, 1, C_GRID);

    const uint32_t fill = gfx_mix(C_PANEL, rgb, 110);
    for (int c = 0; c < pw; c++) {
        /* the bins under the column (at least one), on a logarithmic axis */
        const float f0 = F_LO * expf(lspan * (float)c / (float)pw), f1 = F_LO * expf(lspan * (float)(c + 1) / (float)pw);
        const int b0 = (int)(f0 * FFT_N / SR), b1 = (int)(f1 * FFT_N / SR);
        float mag = 0.0f;
        if (b1 > b0 + 1) { /* several bins under the column: the strongest */
            for (int b = b0; b < b1 && b < FFT_N / 2; b++) {
                const float m2 = re[b] * re[b] + im[b] * im[b];
                mag = m2 > mag ? m2 : mag;
            }
        } else { /* the column is narrower than a bin (low frequencies): interpolate between the two bins */
            const float fb = 0.5f * (f0 + f1) * FFT_N / SR;
            const int k = (int)fb;
            const float t = fb - (float)k;
            const float m0 = re[k] * re[k] + im[k] * im[k], m1 = re[k + 1] * re[k + 1] + im[k + 1] * im[k + 1];
            mag = m0 + (m1 - m0) * t;
        }
        /* a full-scale sine reads 0 dBFS: |X| = N/4 with the Hann window */
        float db = 10.0f * log10f(mag * 16.0f / ((float)FFT_N * FFT_N) + 1e-12f);
        db = db < DB_FLOOR ? DB_FLOOR : db > 0.0f ? 0.0f : db;
        shown_db[c] = db > shown_db[c] - FALL_DB ? db : shown_db[c] - FALL_DB;
        const int bar = (int)((float)ph * (shown_db[c] - DB_FLOOR) / -DB_FLOOR);
        if (bar > 0) {
            gfx_fill(px + c, py + ph - bar, 1, bar, fill);
            gfx_fill(px + c, py + ph - bar, 1, 2, rgb); /* bright edge */
        }
    }
}

void scope_view_draw(int x, int y, int w, int h, uint32_t rgb)
{
    snapshot();
    const int hw = (h - 8) * 2 / 5;
    draw_wave(x, y, w, hw, rgb);
    draw_spectrum(x, y + hw + 8, w, h - hw - 8, rgb);
}
