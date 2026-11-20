/* wav.c — WAV writer (format 3 = IEEE float), little-endian fields written byte by byte
 * so the file is correct whatever the host endianness. Reference: Microsoft RIFF/WAVE spec. */
#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

int wav_write_stereo_f32(const char *path, const float *L, const float *R, int frames, int sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    const uint32_t data_bytes = (uint32_t)frames * 2u * 4u;
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36u + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);                              /* fmt chunk size */
    put_u16(f, 3);                               /* IEEE float */
    put_u16(f, 2);                               /* channels */
    put_u32(f, (uint32_t)sample_rate);
    put_u32(f, (uint32_t)sample_rate * 8u);      /* byte rate */
    put_u16(f, 8);                               /* block align */
    put_u16(f, 32);                              /* bits per sample */
    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);
    for (int i = 0; i < frames; i++) {
        uint32_t u;
        memcpy(&u, &L[i], 4);
        put_u32(f, u);
        memcpy(&u, &R[i], 4);
        put_u32(f, u);
    }
    const int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}

int wav_write_mono_i16(const char *path, const int16_t *x, int frames, int sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    const uint32_t data_bytes = (uint32_t)frames * 2u;
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36u + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1); /* PCM */
    put_u16(f, 1); /* mono */
    put_u32(f, (uint32_t)sample_rate);
    put_u32(f, (uint32_t)sample_rate * 2u);
    put_u16(f, 2);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);
    for (int i = 0; i < frames; i++)
        put_u16(f, (uint16_t)x[i]);
    return fclose(f) == 0 ? 0 : -1;
}

static uint32_t le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

float *wav_read_mono(const char *path, int *frames, int *sample_rate)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = size > 0 ? malloc((size_t)size) : NULL;
    const int ok = d && fread(d, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok || size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
        free(d);
        return NULL;
    }
    int fmt = 0, ch = 0, bits = 0, rate = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    for (long pos = 12; pos + 8 <= size;) {
        const uint32_t n = le(d + pos + 4, 4);
        if (!memcmp(d + pos, "fmt ", 4) && n >= 16 && pos + 8 + 16 <= size) {
            fmt = (int)le(d + pos + 8, 2);
            ch = (int)le(d + pos + 10, 2);
            rate = (int)le(d + pos + 12, 4);
            bits = (int)le(d + pos + 22, 2);
            if (fmt == 0xFFFE && n >= 26 && pos + 8 + 26 <= size)
                fmt = (int)le(d + pos + 32, 2); /* WAVE_FORMAT_EXTENSIBLE: the sub-format */
        } else if (!memcmp(d + pos, "data", 4)) {
            data = d + pos + 8;
            data_len = n < (uint32_t)(size - pos - 8) ? n : (uint32_t)(size - pos - 8);
        }
        pos += 8 + (long)n + (long)(n & 1u);
    }
    const int bps = bits / 8;
    if (!data || ch < 1 || rate < 1000 || !((fmt == 1 && (bits == 16 || bits == 24)) || (fmt == 3 && bits == 32))) {
        free(d);
        return NULL;
    }
    const int n = (int)(data_len / (uint32_t)(bps * ch));
    float *x = malloc(sizeof(float) * (size_t)(n > 0 ? n : 1));
    for (int i = 0; x && i < n; i++) {
        float s = 0.0f;
        for (int c = 0; c < ch; c++) {
            const uint8_t *p = data + ((size_t)i * (size_t)ch + (size_t)c) * (size_t)bps;
            if (fmt == 3) {
                float v;
                const uint32_t u = le(p, 4);
                memcpy(&v, &u, 4);
                s += v;
            } else if (bits == 16) {
                s += (float)(int16_t)le(p, 2) / 32768.0f;
            } else {
                s += (float)((int32_t)(le(p, 3) << 8) >> 8) / 8388608.0f;
            }
        }
        x[i] = s / (float)ch;
    }
    free(d);
    *frames = n;
    *sample_rate = rate;
    return x;
}
