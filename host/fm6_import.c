/* fm6_import.c — converts the user's 6-operator FM sysex files (32-voice banks or single voices) into
 * open-format patches for the fm6 engine, one JSON file per voice.
 *
 *   fm6_import bank.syx patches/fm/            -> patches/fm/<voice_name>.json ...
 *   fm6_import --list bank.syx                 -> prints the voice names
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fm6_sysex.h"
#include "patch.h"

static void slug(const char *name, int index, char *out, size_t len)
{
    size_t k = 0;
    for (const char *c = name; *c && k + 1 < len; c++) {
        if (isalnum((unsigned char)*c))
            out[k++] = (char)tolower((unsigned char)*c);
        else if (k && out[k - 1] != '_')
            out[k++] = '_';
    }
    while (k && out[k - 1] == '_')
        k--;
    if (!k)
        k = (size_t)snprintf(out, len, "voice");
    snprintf(out + k, len - k, "_%02d", index + 1);
}

static void put_file(void *f, const char *text) { fputs(text, f); }

static int write_patch(const char *dir, const char *src, int index, const fm6_sysex_voice_t *v)
{
    char file[256], path[1024], name[16];
    slug(v->name, index, file, sizeof file);
    snprintf(path, sizeof path, "%s/%s.json", dir, file);
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    snprintf(name, sizeof name, "%s", v->name);
    for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; i--)
        name[i] = 0;
    for (char *c = name; *c; c++) /* the name goes into a JSON string */
        if (*c == '"' || *c == '\\')
            *c = '\'';
    fprintf(f, "{\n  \"version\": %d,\n  \"engine\": \"fm6\",\n  \"name\": \"%s\",\n", PATCH_VERSION, name);
    char from[256]; /* the source path goes into a JSON string too */
    snprintf(from, sizeof from, "%s", src);
    for (char *c = from; *c; c++)
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 32)
            *c = '/';
    fprintf(f, "  \"description\": \"imported from %s, voice %d\",\n", from, index + 1);
    patch_setting_t set[FM6_P_COUNT];
    int n = 0;
    for (int i = 0; i < FM6_P_COUNT; i++) {
        if (i == FM6_P_VOLUME || i == FM6_P_BEND_RANGE || i == FM6_P_SPREAD)
            continue;
        set[n].role = fm6_params[i].role;
        snprintf(set[n++].value, sizeof set[0].value, "%g", (double)v->p[i]);
    }
    patch_write_settings(put_file, f, set, n, 2);
    fprintf(f, ",\n  \"audition\": {\"notes\": \"C3+E3+G3 _ _ _ C4 _ E4 _ G4 _ C5 _ _ _ - -\", \"bpm\": 100, \"step\": 0.5}\n}\n");
    fclose(f);
    printf("%-10s -> %s\n", name, path);
    return 0;
}

int main(int argc, char **argv)
{
    const int list = argc >= 3 && !strcmp(argv[1], "--list");
    if (argc != 3) {
        fprintf(stderr, "usage: fm6_import FILE.syx OUTDIR | fm6_import --list FILE.syx\n");
        return 2;
    }
    const char *in = list ? argv[2] : argv[1];
    FILE *f = fopen(in, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", in);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = size > 0 ? malloc((size_t)size) : NULL;
    if (!buf) {
        fclose(f);
        fprintf(stderr, "cannot read %s (empty, not a regular file, or out of memory)\n", in);
        return 1;
    }
    const size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    static fm6_sysex_voice_t voices[512];
    char err[256], warn[1024];
    const int n = fm6_sysex_parse(buf, got, voices, 512, err, sizeof err, warn, sizeof warn);
    free(buf);
    if (n < 0) {
        fprintf(stderr, "%s: %s\n", in, err);
        return 1;
    }
    if (warn[0])
        fprintf(stderr, "%s: %s", in, warn);
    int rc = 0;
    for (int i = 0; i < n; i++) {
        if (list)
            printf("%2d  %s\n", i + 1, voices[i].name);
        else
            rc |= write_patch(argv[2], in, i, &voices[i]);
    }
    return rc ? 1 : 0;
}
