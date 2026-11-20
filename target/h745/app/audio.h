/* audio.h — audio output of the STM32H745I-DISCO: 48 kHz, 32-bit stereo to the headphone jack through the
 * WM8994 codec (SAI2 + DMA in circular double buffer, ST's board support package). The render callback runs
 * in the DMA interrupt, once per half buffer. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_FRAMES 128 /* frames rendered per interrupt (2.7 ms); the latency is two of them */

/* Fills L and R with n samples in -1..1 (n = AUDIO_FRAMES). Called from the DMA interrupt. */
typedef void (*audio_render_fn)(float *L, float *R, int n);

#define AUDIO_ERROR_CLOCK (-100) /* the SAI kernel clock is not the audio PLL (wrong sample rate) */

/* Starts the codec and the DMA. volume 0..100. Returns 0, a negative BSP error code or AUDIO_ERROR_CLOCK. */
int audio_start(audio_render_fn render, int volume);

/* DSP load measured with the cycle counter around the render callback, 0..1 of the time available:
 * smoothed average and peak since the last call of audio_load_peak_reset. */
float audio_load_avg(void);
float audio_load_peak(void);
void audio_load_peak_reset(void);
/* Interrupts that took longer than the buffer (audible glitch), and DMA or SAI errors reported by the BSP. */
uint32_t audio_overruns(void);
/* Consecutive overruns up to the last interrupt (0 after any interrupt that kept up): sustained overload. */
uint32_t audio_overrun_streak(void);
uint32_t audio_errors(void);
/* Buffers rendered since the start: moves while the audio interrupt runs. */
uint32_t audio_buffers(void);
/* Zeros the whole DMA buffer: the circular DMA keeps playing silence without the CPU (a fault handler's last
 * act, instead of the last 2.7 ms repeated as a loud buzz). */
void audio_silence(void);

#endif /* AUDIO_H */
