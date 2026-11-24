/* kitfile.h — sample kits from WAV files (host side): a directory of NN_name.wav files (NN: the MIDI note)
 * becomes a kit in memory (dsp/core/kit.h), the samples mixed to mono, converted to 16 bits and resampled to
 * 48 kHz with the sampler's own band-limited player. Named kits ("kit": "synth_analog" in a patch) are looked
 * up in search directories and loaded once. */
#ifndef KITFILE_H
#define KITFILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int note;         /* 0..127 */
    const char *name;
    const float *x;   /* mono, full scale 1.0 */
    int frames, rate;
} kitfile_sample_t;

/* A kit from samples (sorted by note: pad 1 is the lowest). Returns it malloc'ed, or NULL. */
uint8_t *kitfile_build(const kitfile_sample_t *s, int count, size_t *bytes);
/* A kit from a directory of NN_name.wav files. NULL with a message in err on failure. */
uint8_t *kitfile_load_dir(const char *dir, size_t *bytes, char *err, size_t errlen);
/* A kit from "NOTE:FILE.wav" arguments. */
uint8_t *kitfile_load_args(char **args, int count, size_t *bytes, char *err, size_t errlen);

/* A kit file (NAME.oidk, the format of dsp/core/kit.h, checked). NULL with a message in err on failure. */
uint8_t *kitfile_load_oidk(const char *path, size_t *bytes, char *err, size_t errlen);

#define KITFILE_NAME 64

/* Where named kits are looked for: a directory holding one directory of NN_name.wav files or one NAME.oidk file
 * per kit. The last directory added wins over the earlier ones. */
void kitfile_add_search_dir(const char *dir);
/* The kit of this name, loaded on first use and kept until the program ends; NULL if none (patch_kit_resolver). */
const uint8_t *kitfile_find(const char *name, size_t *bytes);
/* The names of the kits in the search directories (sorted, each once; names of a-z, 0-9 and _ only, as in a
 * patch): returns how many (at most max). */
int kitfile_list(char (*names)[KITFILE_NAME], int max);

#endif /* KITFILE_H */
