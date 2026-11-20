/* wav.h — minimal WAV files: writer (RIFF, 32-bit IEEE float stereo, or 16-bit mono) and reader (PCM 16/24
 * bits or float 32, any channel count, mixed to mono). Host only. */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>

/* Writes frames stereo samples. Returns 0 on success, -1 on I/O error. */
int wav_write_stereo_f32(const char *path, const float *L, const float *R, int frames, int sample_rate);

/* Writes frames mono 16-bit samples. Returns 0 on success, -1 on I/O error. */
int wav_write_mono_i16(const char *path, const int16_t *x, int frames, int sample_rate);

/* Reads a WAV file mixed to mono (full scale 1.0): returns malloc'ed samples (caller frees) and their count
 * and rate, or NULL if the file is missing or in another format. */
float *wav_read_mono(const char *path, int *frames, int *sample_rate);

#endif /* WAV_H */
