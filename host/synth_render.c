/* synth_render.c — offline renderer: plays a scripted demo through the public API and writes a
 * 32-bit float stereo WAV. Also reports peak / RMS levels so loudness regressions are visible.
 *
 *   synth_render --engine va --demo out.wav      first built-in demo of the engine (va and others have some)
 *   synth_render --name va_bass --demo out.wav   a given demo
 *   synth_render --all build-host/renders        every demo, one WAV each
 *   synth_render --list
 *   synth_render --patch patches/acid_bass.json --out x.wav [--notes "C2 C2 C3 -"] [--tail 1.5]
 *   synth_render --song songs/drift.json --out x.wav
 * A patch that names a sample kit ("kit": "synth_drums") finds it in kits/NAME (the current directory), then in
 * the kits built next to this program (build-host/kits/NAME, tools/kit_demo).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demos.h"
#include "engines/engine.h"
#include "kitfile.h"
#include "patch.h"
#include "song.h"
#include "wav.h"


/* Renders a sequence, writes the WAV and prints its levels. Takes ownership of s. */
static int render_seq(seq_t *s, float seconds, const char *label, const char *path, float sr)
{
    float *L, *R;
    const int frames = seq_render(s, sr, seconds, &L, &R);
    seq_free(s);
    if (frames <= 0) {
        fprintf(stderr, "%s: nothing to render (%.1f s) or out of memory\n", label, (double)seconds);
        free(L);
        free(R);
        return 1;
    }
    float peak = 0.0f;
    double acc = 0.0;
    for (int i = 0; i < frames; i++) {
        peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
        acc += 0.5 * ((double)L[i] * (double)L[i] + (double)R[i] * (double)R[i]);
    }
    const int err = wav_write_stereo_f32(path, L, R, frames, (int)sr);
    free(L);
    free(R);
    if (err) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    printf("%-11s -> %s  (%.1f s, peak %.1f dBFS, rms %.1f dBFS)\n", label, path, (double)seconds,
           20.0 * log10((double)peak + 1e-12), 10.0 * log10(acc / frames + 1e-24));
    return 0;
}

static int render_one(const demo_t *d, const char *path, float sr)
{
    seq_t s;
    seq_init(&s);
    d->build(&s);
    return render_seq(&s, d->seconds, d->name, path, sr);
}

static int render_patch(const char *patch_path, const char *notes, float tail, const char *out, float sr)
{
    patch_t p;
    char err[2048], warn[1024];
    if (patch_load(patch_path, &p, err, sizeof err, warn, sizeof warn) != 0) {
        fprintf(stderr, "%s: %s\n", patch_path, err);
        return 1;
    }
    if (warn[0])
        fprintf(stderr, "%s: warning:\n%s", patch_path, warn);
    seq_t s;
    seq_init(&s);
    patch_apply(&p, &s);
    const float len = patch_audition(&p, notes, &s, err, sizeof err);
    if (len < 0.0f) {
        fprintf(stderr, "%s: %s\n", patch_path, err);
        seq_free(&s);
        return 1;
    }
    return render_seq(&s, len + tail, p.name, out, sr);
}

static int write_and_report(const char *label, const char *out, float *L, float *R, int frames, float sr)
{
    float peak = 0.0f;
    double acc = 0.0;
    for (int i = 0; i < frames; i++) {
        peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
        acc += 0.5 * ((double)L[i] * (double)L[i] + (double)R[i] * (double)R[i]);
    }
    const int err = wav_write_stereo_f32(out, L, R, frames, (int)sr);
    free(L);
    free(R);
    if (err) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("%-11s -> %s  (%.1f s, peak %.1f dBFS, rms %.1f dBFS)\n", label, out, (double)frames / (double)sr,
           20.0 * log10((double)peak + 1e-12), 10.0 * log10(acc / frames + 1e-24));
    return 0;
}

static int render_song(const char *path, const char *out, float sr)
{
    float *L, *R;
    char err[2048] = "", warn[2048] = "";
    const int frames = song_render(path, sr, &L, &R, err, sizeof err, warn, sizeof warn);
    if (frames < 0) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }
    if (warn[0])
        fprintf(stderr, "%s: %s", path, warn);
    return write_and_report(path, out, L, R, frames, sr);
}

static void usage(void)
{
    printf("usage: synth_render [--engine NAME] [--name DEMO] [--sr HZ] --demo OUT.wav\n"
           "       synth_render --all DIR\n"
           "       synth_render --list\n"
           "       synth_render --song FILE.json --out OUT.wav\n"
           "       synth_render --patch FILE.json --out OUT.wav [--notes \"C3 E3 G3+B3 - _\"] [--tail SEC]\n");
}

int main(int argc, char **argv)
{
    const char *out = NULL, *all_dir = NULL, *name = NULL, *patch = NULL, *notes = NULL, *song = NULL;
    float tail = 1.5f;
    synth_engine_t engine = SYNTH_ENGINE_VA;
    float sr = 48000.0f;

    /* sample kits: next to the program (build-host/host/../kits), then ./kits, which wins */
    char kits[2048];
    const char *slash = strrchr(argv[0], '/');
    snprintf(kits, sizeof kits, "%.*s/../kits", slash ? (int)(slash - argv[0]) : 1, slash ? argv[0] : ".");
    kitfile_add_search_dir(kits);
    kitfile_add_search_dir("kits");
    patch_kit_resolver = kitfile_find;

    for (int i = 1; i < argc; i++) {
        const int has_val = i + 1 < argc;
        if (!strcmp(argv[i], "--engine") && has_val) {
            const char *e = argv[++i];
            const engine_ops_t *ops = engine_find(e);
            if (!ops) {
                fprintf(stderr, "unknown engine: %s\n", e);
                return 2;
            }
            engine = (synth_engine_t)engine_id(ops);
        } else if (!strcmp(argv[i], "--song") && has_val) {
            song = argv[++i];
        } else if (!strcmp(argv[i], "--patch") && has_val) {
            patch = argv[++i];
        } else if (!strcmp(argv[i], "--notes") && has_val) {
            notes = argv[++i];
        } else if (!strcmp(argv[i], "--tail") && has_val) {
            tail = (float)atof(argv[++i]);
            if (!(tail >= 0.0f && tail <= 600.0f)) {
                fprintf(stderr, "--tail: 0..600 seconds\n");
                return 2;
            }
        } else if ((!strcmp(argv[i], "--demo") || !strcmp(argv[i], "--out")) && has_val) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--name") && has_val) {
            name = argv[++i];
        } else if (!strcmp(argv[i], "--sr") && has_val) {
            sr = (float)atof(argv[++i]);
            if (!(sr >= 8000.0f && sr <= 192000.0f)) {
                fprintf(stderr, "--sr: 8000..192000 Hz\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--all") && has_val) {
            all_dir = argv[++i];
        } else if (!strcmp(argv[i], "--list")) {
            for (int k = 0; k < demo_count; k++)
                printf("%-11s [%s] %s\n", demos[k].name, engine_registry[demos[k].engine]->name, demos[k].description);
            return 0;
        } else {
            usage();
            return 2;
        }
    }
    if (sr < 8000.0f || sr > 192000.0f) {
        fprintf(stderr, "invalid sample rate\n");
        return 2;
    }

    if (all_dir) {
        int err = 0;
        char path[1024];
        for (int k = 0; k < demo_count; k++) {
            snprintf(path, sizeof path, "%s/%s.wav", all_dir, demos[k].name);
            err |= render_one(&demos[k], path, sr);
        }
        return err;
    }
    if (!out) {
        usage();
        return 2;
    }
    if (song)
        return render_song(song, out, sr);
    if (patch)
        return render_patch(patch, notes, tail, out, sr);
    const demo_t *d = name ? demo_find(name) : demo_for_engine(engine);
    if (!d) {
        if (name)
            fprintf(stderr, "no demo called %s (see --list)\n", name);
        else
            fprintf(stderr, "no built-in demo for engine %s (see --list); render one of its patches with --patch\n",
                    engine_registry[engine]->name);
        return 2;
    }
    return render_one(d, out, sr);
}
