/* automation.c — parameters and modulation through the public API: an arpeggio whose filter is
 * automated with synth_set_param (physical units converted by the parameter descriptors),
 * a sample-accurate MIDI schedule (sample_offset), the mod wheel (CC 1) and pitch bend.
 *
 *   ./build-host/examples/example_automation [out.wav]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "engines/va.h"
#include "mod/params.h"
#include "synth.h"
#include "wav.h"

#define SR 48000
#define CHUNK 128

/* Physical value -> normalized value expected by synth_set_param. */
static void set(int id, float physical) { synth_set_param(synth_default(), id, param_to_norm(&va_params[id], physical)); }

static void midi(uint8_t a, uint8_t b, uint8_t c, int offset)
{
    const uint8_t m[3] = {a, b, c};
    synth_midi(synth_default(), m, 3, offset);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "automation.wav";
    const float bpm = 132.0f, step_s = 60.0f / bpm / 4.0f; /* sixteenth notes */
    const int steps = 64, step_frames = (int)(step_s * SR);
    const int frames = steps * step_frames + 2 * SR;
    float *L = malloc(sizeof(float) * frames), *R = malloc(sizeof(float) * frames);

    synth_init(synth_default(), (float)SR);
    set(VA_P_OSC1_WAVE, (float)OSC_SAW);
    set(VA_P_OSC2_WAVE, (float)OSC_PULSE);
    set(VA_P_OSC2_SEMI, 7.0f); /* a fifth above */
    set(VA_P_OSC2_LEVEL, 0.4f);
    set(VA_P_RESO, 4.0f);
    set(VA_P_FENV_AMT, 3.0f);
    set(VA_P_FENV_D, 0.15f);
    set(VA_P_FENV_S, 0.0f);
    set(VA_P_AMP_D, 0.2f);
    set(VA_P_AMP_S, 0.3f);
    set(VA_P_AMP_R, 0.25f);
    set(VA_P_WHEEL_VIB, 0.5f);
    set(VA_P_LFO_RATE, 6.0f);

    static const int arp[8] = {57, 60, 64, 69, 72, 69, 64, 60};
    int prev_note = -1;
    for (int pos = 0; pos < frames; pos += CHUNK) {
        const int n = frames - pos < CHUNK ? frames - pos : CHUNK;
        const float t = (float)pos / SR;

        /* every chunk: cutoff follows a slow sine between 300 Hz and 4 kHz */
        set(VA_P_CUTOFF, 300.0f * powf(4000.0f / 300.0f, 0.5f - 0.5f * cosf(6.2831853f * t / 6.0f)));

        /* notes that start inside this chunk get their exact sample offset */
        for (int s = 0; s < steps; s++) {
            const int at = s * step_frames;
            if (at >= pos && at < pos + n) {
                if (prev_note >= 0)
                    midi(0x80, (uint8_t)prev_note, 0, at - pos);
                prev_note = arp[s % 8] + (s >= 32 ? 5 : 0);
                midi(0x90, (uint8_t)prev_note, (uint8_t)(s % 4 == 0 ? 120 : 75), at - pos);
            }
        }
        if (pos <= steps * step_frames && pos + n > steps * step_frames && prev_note >= 0)
            midi(0x80, (uint8_t)prev_note, 0, steps * step_frames - pos);

        /* second half: mod wheel brings in vibrato; last bar: bend up a whole tone */
        if (t > 4.0f)
            midi(0xB0, 1, (uint8_t)fminf(127.0f, (t - 4.0f) * 64.0f), 0);
        if (t > 6.5f) {
            const int bend = 8192 + (int)fminf(8191.0f, (t - 6.5f) * 8191.0f);
            midi(0xE0, (uint8_t)(bend & 0x7F), (uint8_t)(bend >> 7), 0);
        }

        synth_render(synth_default(), L + pos, R + pos, n);
    }

    if (wav_write_stereo_f32(path, L, R, frames, SR) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    printf("wrote %s (%.1f s)\n", path, (double)frames / SR);
    free(L);
    free(R);
    return 0;
}
