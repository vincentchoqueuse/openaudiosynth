/* scope_view.h — the audio of the M7 on the M4's screen: an oscilloscope (triggered on a rising zero
 * crossing, so a steady note stands still) above a spectrum (2048-point FFT with a Hann window, logarithmic
 * frequency axis 30 Hz .. 20 kHz, -90 .. 0 dBFS, peaks falling slowly). Reads the ring the M7 fills
 * (shared.h, SCOPE). */
#ifndef SCOPE_VIEW_H
#define SCOPE_VIEW_H

#include <stdint.h>

void scope_view_init(void);
/* Draws into the frame being prepared (gfx.h), in the rectangle (x, y, w, h), in color rgb. */
void scope_view_draw(int x, int y, int w, int h, uint32_t rgb);

#endif /* SCOPE_VIEW_H */
