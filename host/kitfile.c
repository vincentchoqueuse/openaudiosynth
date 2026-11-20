/* kitfile.c — sample kits from WAV files (kitfile.h). Resampling to 48 kHz goes through the sampler's player
 * (dsp/osc/splay.h): the same band-limited interpolation the board uses, so a 44.1 or 96 kHz sample neither
 * aliases nor dulls. */
#include "kitfile.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/kit.h"
#include "osc/splay.h"
#include "wav.h"

#define KITFILE_DIRS 8
#define KITFILE_CACHE 64 /* kits kept loaded (a plugin session browsing kits); past it, one is reported missing */

static int by_note(const void *a, const void *b)
{
    return ((const kitfile_sample_t *)a)->note - ((const kitfile_sample_t *)b)->note;
}

static int16_t to16(float v)
{
    const float c = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    return (int16_t)(c * 32767.0f + (c >= 0.0f ? 0.5f : -0.5f));
}

/* x (frames at rate) as 16-bit samples at KIT_RATE; *out_frames */
static int16_t *convert(const float *x, int frames, int rate, uint32_t *out_frames)
{
    int16_t *src = malloc(sizeof(int16_t) * (size_t)(frames > 0 ? frames : 1));
    if (!src)
        return NULL;
    for (int i = 0; i < frames; i++)
        src[i] = to16(x[i]);
    if (rate == KIT_RATE || frames < 2) {
        *out_frames = (uint32_t)frames;
        return src;
    }
    const float r = (float)rate / (float)KIT_RATE;
    const int n = (int)((float)frames / r);
    int16_t *dst = malloc(sizeof(int16_t) * (size_t)(n + 32));
    if (!dst) {
        free(src);
        return NULL;
    }
    float blk[32];
    splay_t p;
    splay_start(&p, src, frames, 0.0f, 1.0f, 0, 0);
    int k = 0;
    while (k < n) {
        splay_process(&p, blk, 32, r, r);
        for (int i = 0; i < 32 && k < n; i++)
            dst[k++] = to16(blk[i]);
    }
    free(src);
    *out_frames = (uint32_t)n;
    return dst;
}

uint8_t *kitfile_build(const kitfile_sample_t *in, int count, size_t *bytes)
{
    if (count < 1 || count > KIT_MAX)
        return NULL;
    kitfile_sample_t s[KIT_MAX];
    memcpy(s, in, sizeof(s[0]) * (size_t)count);
    qsort(s, (size_t)count, sizeof(s[0]), by_note);
    int16_t *pcm[KIT_MAX];
    kit_entry_t e[KIT_MAX];
    size_t off = KIT_HEADER + (size_t)count * KIT_ENTRY;
    for (int i = 0; i < count; i++) {
        memset(&e[i], 0, sizeof e[i]);
        snprintf(e[i].name, sizeof e[i].name, "%s", s[i].name);
        e[i].note = (uint8_t)(s[i].note & 127);
        pcm[i] = convert(s[i].x, s[i].frames, s[i].rate, &e[i].frames);
        if (!pcm[i]) { /* out of memory: no kit rather than a kit of silent samples */
            for (int k = 0; k < i; k++)
                free(pcm[k]);
            return NULL;
        }
        e[i].offset = (uint32_t)off;
        off += 2u * e[i].frames;
    }
    uint8_t *kit = malloc(off);
    if (kit) {
        kit_write_directory(kit, e, count);
        for (int i = 0; i < count; i++)
            for (uint32_t k = 0; pcm[i] && k < e[i].frames; k++) { /* little-endian, whatever the host */
                kit[e[i].offset + 2 * k] = (uint8_t)pcm[i][k];
                kit[e[i].offset + 2 * k + 1] = (uint8_t)((uint16_t)pcm[i][k] >> 8);
            }
    }
    for (int i = 0; i < count; i++)
        free(pcm[i]);
    *bytes = off;
    return kit;
}

/* loads the WAV files of s[0..count) (path in name), then builds; the names become the base names */
static uint8_t *load(kitfile_sample_t *s, char **paths, int count, size_t *bytes, char *err, size_t errlen)
{
    uint8_t *kit = NULL;
    char names[KIT_MAX][KIT_NAME];
    int loaded = 0;
    for (; loaded < count; loaded++) {
        float *x = wav_read_mono(paths[loaded], &s[loaded].frames, &s[loaded].rate);
        if (!x) {
            snprintf(err, errlen, "%s: not a readable WAV file (PCM 16/24 bits or float 32)", paths[loaded]);
            goto out;
        }
        s[loaded].x = x;
        const char *base = strrchr(paths[loaded], '/') ? strrchr(paths[loaded], '/') + 1 : paths[loaded];
        while (*base >= '0' && *base <= '9')
            base++;
        while (*base == '_' || *base == '-' || *base == ' ')
            base++;
        snprintf(names[loaded], KIT_NAME, "%.*s", (int)strcspn(base, "."), base);
        s[loaded].name = names[loaded];
    }
    if (!(kit = kitfile_build(s, count, bytes)))
        snprintf(err, errlen, "out of memory");
out:
    for (int i = 0; i < loaded; i++)
        free((void *)s[i].x);
    return kit;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

uint8_t *kitfile_load_dir(const char *dir, size_t *bytes, char *err, size_t errlen)
{
    DIR *d = opendir(dir);
    if (!d) {
        snprintf(err, errlen, "%s: no such directory", dir);
        return NULL;
    }
    char *paths[KIT_MAX];
    kitfile_sample_t s[KIT_MAX];
    int n = 0;
    for (struct dirent *de; (de = readdir(d)) && n < KIT_MAX;) {
        const size_t len = strlen(de->d_name);
        if (len < 5 || strcmp(de->d_name + len - 4, ".wav") || de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        const int note = atoi(de->d_name);
        if (note > 127)
            continue;
        const size_t size = strlen(dir) + len + 2;
        if (!(paths[n] = malloc(size)))
            break;
        snprintf(paths[n], size, "%s/%s", dir, de->d_name);
        n++;
    }
    closedir(d);
    qsort(paths, (size_t)n, sizeof(paths[0]), cmp_str);
    uint8_t *kit = NULL;
    if (!n) {
        snprintf(err, errlen, "%s: no NN_name.wav file (NN: the MIDI note)", dir);
    } else {
        for (int i = 0; i < n; i++) {
            memset(&s[i], 0, sizeof s[i]);
            const char *base = strrchr(paths[i], '/') + 1;
            s[i].note = atoi(base);
        }
        kit = load(s, paths, n, bytes, err, errlen);
    }
    for (int i = 0; i < n; i++)
        free(paths[i]);
    return kit;
}

uint8_t *kitfile_load_args(char **args, int count, size_t *bytes, char *err, size_t errlen)
{
    if (count < 1 || count > KIT_MAX) {
        snprintf(err, errlen, "1 to %d samples, each NOTE:FILE.wav", KIT_MAX);
        return NULL;
    }
    kitfile_sample_t s[KIT_MAX];
    char *paths[KIT_MAX];
    for (int i = 0; i < count; i++) {
        const char *colon = strchr(args[i], ':');
        memset(&s[i], 0, sizeof s[i]);
        s[i].note = colon ? atoi(args[i]) : -1;
        if (!colon || s[i].note < 0 || s[i].note > 127) {
            snprintf(err, errlen, "%s: each sample is NOTE:FILE.wav (NOTE 0..127)", args[i]);
            return NULL;
        }
        paths[i] = (char *)colon + 1;
    }
    return load(s, paths, count, bytes, err, errlen);
}

uint8_t *kitfile_load_oidk(const char *path, size_t *bytes, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "%s: cannot open", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *kit = size > 0 ? malloc((size_t)size) : NULL;
    const int ok = kit && fread(kit, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok || kit_count(kit, (size_t)size) < 0) {
        snprintf(err, errlen, "%s: not a kit file", path);
        free(kit);
        return NULL;
    }
    *bytes = (size_t)size;
    return kit;
}

/* ---------------- named kits ---------------- */

static char *dirs[KITFILE_DIRS];
static int ndirs;
static struct {
    char *name;
    uint8_t *kit;
    size_t bytes;
} cache[KITFILE_CACHE];
static int ncache;

void kitfile_add_search_dir(const char *dir)
{
    if (ndirs < KITFILE_DIRS)
        dirs[ndirs++] = strdup(dir);
}

const uint8_t *kitfile_find(const char *name, size_t *bytes)
{
    for (int i = 0; i < ncache; i++)
        if (!strcmp(cache[i].name, name)) {
            *bytes = cache[i].bytes;
            return cache[i].kit;
        }
    if (ncache >= KITFILE_CACHE) {
        fprintf(stderr, "kitfile: more than %d kits loaded, %s not loaded\n", KITFILE_CACHE, name);
        return NULL;
    }
    for (int i = ndirs - 1; i >= 0; i--) {
        char path[2048], err[512];
        snprintf(path, sizeof path, "%s/%s.oidk", dirs[i], name);
        size_t n;
        uint8_t *kit = kitfile_load_oidk(path, &n, err, sizeof err);
        if (!kit) {
            snprintf(path, sizeof path, "%s/%s", dirs[i], name);
            kit = kitfile_load_dir(path, &n, err, sizeof err);
        }
        if (kit) {
            cache[ncache].name = strdup(name);
            cache[ncache].kit = kit;
            cache[ncache].bytes = n;
            ncache++;
            *bytes = n;
            return kit;
        }
    }
    return NULL;
}

static int valid_name(const char *s, size_t len)
{
    if (!len || len >= KITFILE_NAME)
        return 0;
    for (size_t i = 0; i < len; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return 0; /* the names a patch can hold ("kit": "my_kit") */
    return 1;
}

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int kitfile_list(char (*names)[KITFILE_NAME], int max)
{
    int n = 0;
    for (int i = 0; i < ndirs; i++) {
        DIR *d = opendir(dirs[i]);
        for (struct dirent *de; d && (de = readdir(d)) && n < max;) {
            const char *e = de->d_name;
            size_t len = strlen(e);
            if (len > 5 && !strcmp(e + len - 5, ".oidk"))
                len -= 5;
            else if (e[0] == '.' || strchr(e, '.'))
                continue; /* a directory of samples has no dot in its name */
            if (!valid_name(e, len))
                continue;
            int dup = 0;
            for (int k = 0; k < n; k++)
                dup |= strlen(names[k]) == len && !strncmp(names[k], e, len);
            if (!dup)
                snprintf(names[n++], KITFILE_NAME, "%.*s", (int)len, e);
        }
        if (d)
            closedir(d);
    }
    qsort(names, (size_t)n, KITFILE_NAME, cmp_name);
    return n;
}
