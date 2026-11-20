/* song.c — song rendering (see song.h). Each track: load patch, apply per-track overrides, build the
 * phrase (start offset as leading rests, repeats by concatenation), render offline, then mix.
 * Master: reverb return, fades, peak normalization to -1 dBFS (reported, never boosted above +12 dB). */
#include "song.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx/reverb.h"
#include "json.h"
#include "patch.h"
#include "engines/drums.h"

#define SONG_VERSION 1
#define SONG_MAX_SECONDS 3600 /* one hour: bounds the buffers */
#define SONG_MAX_REPEAT 4096

static double num(const json_t *obj, const char *key, double def)
{
    const json_t *v = json_get(obj, key);
    return v && v->type == JSON_NUM ? v->num : def;
}

/* Appends `count` copies of src after `rests` rest steps. */
static char *build_notes(const char *src, int rests, int count)
{
    const size_t len = strlen(src);
    char *s = malloc((size_t)rests * 2 + (len + 1) * (size_t)count + 1); /* rests >= 0, count >= 1: checked by the caller */
    if (!s)
        return NULL;
    char *q = s;
    for (int i = 0; i < rests; i++) {
        *q++ = '-';
        *q++ = ' ';
    }
    for (int i = 0; i < count; i++) {
        memcpy(q, src, len);
        q += len;
        *q++ = ' ';
    }
    *q = 0;
    return s;
}

/* Note for a pattern row name: a drum instrument ("kick", "chat"...) or a note name ("C2"). */
static int row_note(const char *name)
{
    for (int i = 0; i < DR_COUNT; i++)
        if (!strcmp(name, drums_instrument_names[i]))
            return drums_instrument_notes[i];
    return patch_parse_note(name, (int)strlen(name));
}

/* Step-sequencer grid -> phrase. `pat` is one object {row: "x..X..o.", ..., "repeat": n} or an array of
 * them (sections played in order). Returns a malloc'ed phrase, or NULL with a message in err. */
static char *pattern_to_notes(const json_t *pat, char *err, size_t errlen)
{
    size_t cap = 4096, len = 0;
    char *out = malloc(cap);
    out[0] = 0;
    const json_t *first = pat->type == JSON_ARR ? pat->child : pat;
    for (const json_t *sec = first; sec; sec = pat->type == JSON_ARR ? sec->next : NULL) {
        if (sec->type != JSON_OBJ) {
            snprintf(err, errlen, "pattern sections must be objects");
            free(out);
            return NULL;
        }
        const json_t *rep = json_get(sec, "repeat");
        const int repeat = rep && rep->type == JSON_NUM && rep->num >= 1 ? (int)rep->num : 1;
        /* rows: strip spaces and bar lines, check equal lengths */
        enum { MAXROWS = 24, MAXSTEPS = 512 };
        int notes[MAXROWS], nrows = 0, steps = -1;
        char cells[MAXROWS][MAXSTEPS];
        for (const json_t *r = sec->child; r; r = r->next) {
            if (!strcmp(r->key, "repeat"))
                continue;
            if (r->type != JSON_STR || nrows >= MAXROWS) {
                snprintf(err, errlen, "pattern row \"%s\" must be a string", r->key);
                free(out);
                return NULL;
            }
            const int note = row_note(r->key);
            if (note < 0) {
                snprintf(err, errlen, "pattern row \"%s\": not a drum name (kick snare clap rim chat ohat ltom "
                                      "mtom htom cymbal cowbell clave) nor a note", r->key);
                free(out);
                return NULL;
            }
            int k = 0;
            for (const char *c = r->str; *c && k < MAXSTEPS; c++)
                if (*c != ' ' && *c != '|')
                    cells[nrows][k++] = *c;
            if (steps >= 0 && k != steps) {
                snprintf(err, errlen, "pattern row \"%s\" has %d steps, previous rows have %d", r->key, k, steps);
                free(out);
                return NULL;
            }
            steps = k;
            notes[nrows++] = note;
        }
        for (int rp = 0; rp < repeat; rp++) {
            for (int st = 0; st < steps; st++) {
                char tok[256];
                size_t tl = 0;
                for (int r = 0; r < nrows; r++) {
                    const char c = cells[r][st];
                    if (c != 'x' && c != 'X' && c != 'o')
                        continue;
                    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                    tl += (size_t)snprintf(tok + tl, sizeof tok - tl, "%s%s%d%s", tl ? "+" : "", names[notes[r] % 12],
                                           notes[r] / 12 - 1, c == 'X' ? "!" : c == 'o' ? "?" : "");
                }
                if (!tl)
                    tok[tl++] = '-', tok[tl] = 0;
                if (len + tl + 2 >= cap)
                    out = realloc(out, cap *= 2);
                len += (size_t)snprintf(out + len, cap - len, "%s ", tok);
            }
        }
    }
    return out;
}

int song_render(const char *path, float sr, float **Lp, float **Rp, char *err, size_t errlen, char *warn,
                size_t warnlen)
{
    json_t *root = json_parse_file(path, err, errlen);
    if (!root)
        return -1;
    int frames = -1;
    float *L = NULL, *R = NULL, *send = NULL;
    if (warn && warnlen)
        warn[0] = 0;

    if ((int)num(root, "version", 0) != SONG_VERSION) {
        snprintf(err, errlen, "\"version\": %d is required", SONG_VERSION);
        goto out;
    }
    const double bpm = num(root, "bpm", 90.0);
    const double beats = num(root, "length_beats", 64.0), tail = num(root, "tail_s", 6.0);
    if (!(bpm >= 10.0 && bpm <= 400.0) || !(beats > 0.0) || !(tail >= 0.0)) {
        snprintf(err, errlen, "\"bpm\" must be 10..400, \"length_beats\" > 0 and \"tail_s\" >= 0");
        goto out;
    }
    const double length_s = beats * 60.0 / bpm + tail;
    if (length_s > SONG_MAX_SECONDS) {
        snprintf(err, errlen, "song too long: %.0f s (%d s at most)", length_s, SONG_MAX_SECONDS);
        goto out;
    }
    frames = (int)(length_s * (double)sr);
    L = calloc((size_t)frames, sizeof(float));
    R = calloc((size_t)frames, sizeof(float));
    send = calloc((size_t)frames, sizeof(float));
    if (!L || !R || !send) {
        snprintf(err, errlen, "out of memory for %d frames", frames);
        frames = -1;
        goto out;
    }

    const json_t *tracks = json_get(root, "tracks");
    if (!tracks || tracks->type != JSON_ARR || !tracks->child) {
        snprintf(err, errlen, "\"tracks\" must be a non-empty array");
        frames = -1;
        goto out;
    }
    int ti = 0;
    for (const json_t *t = tracks->child; t; t = t->next, ti++) {
        const json_t *pp = json_get(t, "patch"), *notes = json_get(t, "notes"), *pat = json_get(t, "pattern");
        if (!pp || (pp->type != JSON_STR && pp->type != JSON_OBJ) || ((!notes || notes->type != JSON_STR) && !pat)) {
            snprintf(err, errlen, "track %d: \"patch\" (a file or an inline patch) and either \"notes\" or \"pattern\" are required", ti);
            frames = -1;
            goto out;
        }
        char *grid = NULL;
        if (pat && !(grid = pattern_to_notes(pat, err, errlen))) {
            frames = -1;
            goto out;
        }
        patch_t p;
        char perr[1024], pwarn[512];
        const int loaded = pp->type == JSON_OBJ ? patch_from_object(pp, &p, perr, sizeof perr, pwarn, sizeof pwarn)
                                                : patch_load(pp->str, &p, perr, sizeof perr, pwarn, sizeof pwarn);
        if (loaded != 0 || patch_set_params(&p, json_get(t, "params"), perr, sizeof perr, pwarn, sizeof pwarn) != 0) {
            snprintf(err, errlen, "track %d (%s): %s", ti, pp->type == JSON_STR ? pp->str : "inline patch", perr);
            free(grid);
            frames = -1;
            goto out;
        }
        if (pwarn[0] && warn) {
            const size_t u = strlen(warn);
            snprintf(warn + u, warnlen - u, "track %d: %s", ti, pwarn);
        }
        p.bpm = (float)bpm;
        /* same bounds as a patch's audition (patch.c); out-of-range values are clamped */
        p.step_beats = dsp_clampf((float)num(t, "step", grid ? 0.25 : 1.0), 1.0f / 64.0f, 64.0f); /* grids: sixteenths */
        p.gate = dsp_clampf((float)num(t, "gate", 1.0), 0.02f, 1.0f);
        p.velocity = dsp_clampi((int)num(t, "velocity", 100), 1, 127);
        p.humanize_vel = (float)num(t, "humanize_velocity", 0.0);
        p.humanize_ms = (float)num(t, "humanize_ms", 0.0);
        p.swing = (float)dsp_clampf((float)num(t, "swing", 0.0), 0.0f, 0.5f);
        const int start = dsp_clampi((int)num(t, "start_step", 0), 0, 1 << 20);
        const int repeat = dsp_clampi((int)num(t, "repeat", 1), 1, SONG_MAX_REPEAT);
        char *phrase = build_notes(grid ? grid : notes->str, start, repeat);
        free(grid);
        if (!phrase) {
            snprintf(err, errlen, "track %d: out of memory", ti);
            frames = -1;
            goto out;
        }

        seq_t s;
        seq_init(&s);
        patch_apply(&p, &s);
        const float plen = patch_audition(&p, phrase, &s, perr, sizeof perr);
        free(phrase);
        if (plen < 0.0f) {
            snprintf(err, errlen, "track %d: %s", ti, perr);
            seq_free(&s);
            frames = -1;
            goto out;
        }
        float *tl, *tr;
        const int tf = seq_render(&s, sr, (float)length_s, &tl, &tr);
        seq_free(&s);
        if (tf < 0) {
            snprintf(err, errlen, "track %d: out of memory", ti);
            frames = -1;
            goto out;
        }

        /* sidechain-style pumping locked to the beat: gain dips at every beat and recovers */
        const json_t *pump = json_get(t, "pump");
        if (pump) {
            const float depth = 1.0f - powf(10.0f, -(float)num(pump, "depth_db", 8.0) / 20.0f);
            const float tau = dsp_maxf((float)num(pump, "release_s", 0.18), 0.001f) / 3.0f;
            const float every = (float)(60.0 / bpm * (double)dsp_maxf((float)num(pump, "every_beats", 1.0), 1.0f / 64.0f));
            for (int i = 0; i < tf; i++) {
                const float tb = fmodf((float)i / sr, every);
                const float gp = 1.0f - depth * expf(-tb / tau);
                tl[i] *= gp;
                tr[i] *= gp;
            }
        }
        const float g = powf(10.0f, (float)num(t, "gain_db", 0.0) / 20.0f);
        const float pan = (float)num(t, "pan", 0.0); /* -1..1, balance */
        const float gl = g * (pan > 0.0f ? 1.0f - pan : 1.0f), gr = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const float sendg = (float)num(t, "reverb_send", 0.3);
        for (int i = 0; i < tf && i < frames; i++) {
            L[i] += gl * tl[i];
            R[i] += gr * tr[i];
            send[i] += sendg * 0.5f * (gl * tl[i] + gr * tr[i]);
        }
        free(tl);
        free(tr);
    }

    /* shared reverb */
    {
        const json_t *rv = json_get(root, "reverb");
        const float size = (float)num(rv, "size", 1.0);
        reverb_t r;
        float *mem = malloc(sizeof(float) * reverb_mem_floats(sr, size));
        reverb_init(&r, sr, size, mem);
        reverb_set(&r, (float)num(rv, "decay", 0.7), (float)num(rv, "damping_hz", 5000.0),
                   (float)num(rv, "predelay_s", 0.03));
        const float ret = powf(10.0f, (float)num(rv, "return_db", 0.0) / 20.0f);
        float wl[256], wr[256];
        for (int i = 0; i < frames; i += 256) {
            const int n = frames - i < 256 ? frames - i : 256;
            reverb_process(&r, send + i, wl, wr, n);
            for (int k = 0; k < n; k++) {
                L[i + k] += ret * wl[k];
                R[i + k] += ret * wr[k];
            }
        }
        free(mem);
    }

    /* master: fades, then normalize to -1 dBFS */
    {
        const json_t *ms = json_get(root, "master");
        const int fin = (int)(num(ms, "fade_in_s", 0.0) * (double)sr), fout = (int)(num(ms, "fade_out_s", 3.0) * (double)sr);
        for (int i = 0; i < frames; i++) {
            float g = 1.0f;
            if (i < fin)
                g *= (float)i / (float)fin;
            if (i > frames - fout)
                g *= (float)(frames - i) / (float)fout;
            L[i] *= g;
            R[i] *= g;
        }
        float peak = 1e-9f;
        for (int i = 0; i < frames; i++)
            peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
        float norm = 0.891f / peak; /* -1 dBFS */
        if (norm > 3.98f)
            norm = 3.98f; /* +12 dB at most */
        for (int i = 0; i < frames; i++) {
            L[i] *= norm;
            R[i] *= norm;
        }
        if (warn) {
            const size_t u = strlen(warn);
            snprintf(warn + u, warnlen - u, "normalization: %+.1f dB\n", 20.0 * log10((double)norm));
        }
    }

out:
    free(send);
    json_free(root);
    if (frames < 0) {
        free(L);
        free(R);
        return -1;
    }
    *Lp = L;
    *Rp = R;
    return frames;
}
