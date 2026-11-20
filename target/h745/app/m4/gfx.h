/* gfx.h — a small renderer for the 480 x 272 screen of the STM32H745I-DISCO: two RGB565 frames in the
 * SDRAM (one shown, one drawn, swapped during the vertical blanking: no flicker, no tearing), rectangle
 * fills by the DMA2D, rounded rectangles and anti-aliased text (fonts_gen.c, tools/gen_font.py) blended by
 * the CPU, all clipped to a rectangle. Colors are 0xRRGGBB. */
#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

#include <stdint.h>

#define GFX_W 480
#define GFX_H 272

typedef struct {
    uint32_t offset;     /* in the font's bits: rows of 4-bit coverage, two pixels per byte */
    uint8_t w, h;        /* ink box */
    int8_t x, y;         /* ink box position from the pen (x) and from the top of the line (y) */
    uint8_t advance;
} glyph_t;

typedef struct {
    const uint8_t *bits;
    const glyph_t *glyphs;
    uint8_t height, ascent, first, count;
} font_t;

extern const font_t font_small, font_body, font_title, font_big;

/* After BSP_LCD_InitEx (RGB565, layer 0 at LCD_LAYER_0_ADDRESS): the second frame follows the first. */
void gfx_init(void);
/* Shows the frame just drawn at the next vertical blanking, then draws into the other one. */
void gfx_present(void);

void gfx_clip(int x, int y, int w, int h); /* drawing outside is skipped */
void gfx_noclip(void);
void gfx_fill(int x, int y, int w, int h, uint32_t rgb);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb);
/* Text with its top at y; cut with "..." to max_w pixels (0 = no limit). Returns the x after the text. */
int gfx_text(const font_t *f, int x, int y, const char *s, uint32_t rgb, int max_w);
int gfx_text_width(const font_t *f, const char *s);
uint32_t gfx_mix(uint32_t a, uint32_t b, int t256); /* a + (b - a) * t / 256, per channel */

#endif /* GFX_H_INCLUDED */
