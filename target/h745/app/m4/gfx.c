/* gfx.c — the screen renderer of the M4 (see gfx.h). Fills go through the DMA2D in register-to-memory mode;
 * glyph and edge pixels are blended by the CPU (read, mix, write). The SDRAM is device memory for the M4 (no
 * cache): a barrier before each DMA2D transfer and before a frame is shown keeps the writes in order. */
#include "gfx.h"

#include <math.h>
#include <string.h>

#include "main.h"
#include "stm32h745i_discovery_lcd.h"

#define FRAME_BYTES (GFX_W * GFX_H * 2)

static uint16_t *frames[2];
static int back;                    /* frame being drawn */
static int cx0, cy0, cx1, cy1;      /* clip rectangle, x1/y1 exclusive */

static inline uint16_t rgb565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* blends color c into the pixel with coverage a (0..15) */
static inline void blend(uint16_t *p, uint32_t c, unsigned a)
{
    if (a == 0)
        return;
    const uint16_t fg = rgb565(c);
    if (a >= 15) {
        *p = fg;
        return;
    }
    const unsigned bg = *p;
    const unsigned r = ((bg >> 11) * (15 - a) + (fg >> 11) * a) / 15;
    const unsigned g = (((bg >> 5) & 63) * (15 - a) + ((fg >> 5) & 63) * a) / 15;
    const unsigned b = ((bg & 31) * (15 - a) + (fg & 31) * a) / 15;
    *p = (uint16_t)(r << 11 | g << 5 | b);
}

uint32_t gfx_mix(uint32_t a, uint32_t b, int t)
{
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        const int ca = (int)((a >> s) & 255), cb = (int)((b >> s) & 255);
        out |= (uint32_t)(ca + (cb - ca) * t / 256) << s;
    }
    return out;
}

void gfx_clip(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x;
    cy0 = y < 0 ? 0 : y;
    cx1 = x + w > GFX_W ? GFX_W : x + w;
    cy1 = y + h > GFX_H ? GFX_H : y + h;
}

void gfx_noclip(void) { gfx_clip(0, 0, GFX_W, GFX_H); }

void gfx_init(void)
{
    frames[0] = (uint16_t *)LCD_LAYER_0_ADDRESS; /* shown by the BSP's layer 0 */
    frames[1] = (uint16_t *)(LCD_LAYER_0_ADDRESS + FRAME_BYTES);
    back = 1;
    gfx_noclip();
}

void gfx_fill(int x, int y, int w, int h, uint32_t rgb)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;
    __DSB(); /* the CPU's pixels are written before the DMA2D runs */
    DMA2D->CR = DMA2D_R2M;
    DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
    DMA2D->OCOLR = rgb565(rgb);
    DMA2D->OMAR = (uint32_t)(frames[back] + y0 * GFX_W + x0);
    DMA2D->OOR = (uint32_t)(GFX_W - (x1 - x0));
    DMA2D->NLR = (uint32_t)((x1 - x0) << DMA2D_NLR_PL_Pos | (y1 - y0));
    DMA2D->CR |= DMA2D_CR_START;
    for (uint32_t guard = 0; (DMA2D->CR & DMA2D_CR_START) && guard < 1000000u; guard++) {
    }
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb)
{
    if (r * 2 > h)
        r = h / 2;
    if (r * 2 > w)
        r = w / 2;
    gfx_fill(x, y + r, w, h - 2 * r, rgb);
    for (int k = 0; k < r; k++) { /* the r rows at the top and at the bottom, edge pixels anti-aliased */
        const float dy = (float)r - (float)k - 0.5f;
        const float inset = (float)r - sqrtf((float)(r * r) - dy * dy);
        const int i = (int)inset;
        const unsigned cover = (unsigned)((1.0f - (inset - (float)i)) * 15.0f + 0.5f);
        const int rows[2] = {y + k, y + h - 1 - k};
        for (int t = 0; t < 2; t++) {
            gfx_fill(x + i + 1, rows[t], w - 2 * (i + 1), 1, rgb);
            if (rows[t] < cy0 || rows[t] >= cy1)
                continue;
            uint16_t *line = frames[back] + rows[t] * GFX_W;
            if (x + i >= cx0 && x + i < cx1)
                blend(line + x + i, rgb, cover);
            if (x + w - 1 - i >= cx0 && x + w - 1 - i < cx1)
                blend(line + x + w - 1 - i, rgb, cover);
        }
    }
}

static const glyph_t *glyph(const font_t *f, char c)
{
    const unsigned k = (unsigned char)c - f->first;
    return k < f->count ? &f->glyphs[k] : &f->glyphs[0];
}

int gfx_text_width(const font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++)
        w += glyph(f, *s)->advance;
    return w;
}

static int draw_glyph(const font_t *f, int x, int y, char c, uint32_t rgb)
{
    const glyph_t *g = glyph(f, c);
    const uint8_t *src = f->bits + g->offset;
    const int row_bytes = (g->w + 1) / 2;
    for (int j = 0; j < g->h; j++) {
        const int py = y + g->y + j;
        if (py < cy0 || py >= cy1)
            continue;
        uint16_t *line = frames[back] + py * GFX_W;
        for (int i = 0; i < g->w; i++) {
            const int px = x + g->x + i;
            if (px < cx0 || px >= cx1)
                continue;
            const uint8_t b = src[j * row_bytes + i / 2];
            blend(line + px, rgb, (i & 1) ? (b & 15u) : (b >> 4));
        }
    }
    return x + g->advance;
}

int gfx_text(const font_t *f, int x, int y, const char *s, uint32_t rgb, int max_w)
{
    if (max_w > 0 && gfx_text_width(f, s) > max_w) { /* cut, and end with "..." */
        const int dots = 3 * glyph(f, '.')->advance;
        int w = 0;
        for (; *s && w + glyph(f, *s)->advance <= max_w - dots; s++)
            x = draw_glyph(f, x, y, *s, rgb), w += glyph(f, *s)->advance;
        for (int k = 0; k < 3; k++)
            x = draw_glyph(f, x, y, '.', rgb);
        return x;
    }
    for (; *s; s++)
        x = draw_glyph(f, x, y, *s, rgb);
    return x;
}

void gfx_present(void)
{
    __DSB(); /* the whole frame is in the SDRAM before the display reads it */
    for (uint32_t guard = 0; (DMA2D->CR & DMA2D_CR_START) && guard < 1000000u; guard++) {
    }
    LTDC_Layer1->CFBAR = (uint32_t)frames[back];
    LTDC->SRCR = LTDC_SRCR_VBR; /* take the new address at the next vertical blanking */
    for (uint32_t t0 = HAL_GetTick(); (LTDC->SRCR & LTDC_SRCR_VBR) && HAL_GetTick() - t0 < 50;) {
    }
    back ^= 1;
}
