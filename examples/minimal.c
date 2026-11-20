/* minimal.c — the smallest complete use of the public API: init, a C major chord sent as raw MIDI,
 * rendering in host-sized chunks, WAV output. Start here.
 *
 *   ./build-host/examples/example_minimal [out.wav]
 */
#include <stdio.h>
#include <stdlib.h>

#include "synth.h"
#include "wav.h"

#define SR 48000
#define SECONDS 4
#define CHUNK 256 /* typical audio callback size; the synth splits it into blocks of 32 */

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "minimal.wav";
    const int frames = SR * SECONDS;
    float *L = malloc(sizeof(float) * frames), *R = malloc(sizeof(float) * frames);

    synth_init(synth_default(), (float)SR);
    synth_set_engine(synth_default(), SYNTH_ENGINE_VA); /* default engine, shown for clarity */

    /* Note-on: status 0x90 (channel 1), note number, velocity 1..127 */
    const uint8_t chord[3] = {60, 64, 67};
    for (int i = 0; i < 3; i++) {
        const uint8_t on[3] = {0x90, chord[i], 100};
        synth_midi(synth_default(), on, 3, 0);
    }

    for (int pos = 0; pos < frames; pos += CHUNK) {
        if (pos == 2 * SR) { /* after 2 s: note-off (here as note-on with velocity 0) */
            for (int i = 0; i < 3; i++) {
                const uint8_t off[3] = {0x90, chord[i], 0};
                synth_midi(synth_default(), off, 3, 0);
            }
        }
        const int n = frames - pos < CHUNK ? frames - pos : CHUNK;
        synth_render(synth_default(), L + pos, R + pos, n);
    }

    if (wav_write_stereo_f32(path, L, R, frames, SR) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    printf("wrote %s\n", path);
    free(L);
    free(R);
    return 0;
}
