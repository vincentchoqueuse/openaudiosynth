/* demos.c — demo sequences and an offline sequencer: events are sorted by time and fed to the
 * synth with their sample offset inside each SYNTH_BLOCK chunk. */
#include "demos.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engines/va.h"
#include "mod/params.h"

/* ---------------- sequencer ---------------- */

void seq_init(seq_t *s)
{
    s->n = 0;
    s->cap = 256;
    s->ev = malloc(sizeof(seq_event_t) * (size_t)s->cap);
}

void seq_free(seq_t *s)
{
    free(s->ev);
    s->ev = NULL;
    s->n = s->cap = 0;
}

static seq_event_t *seq_add(seq_t *s, float t, int kind)
{
    if (s->n == s->cap) {
        s->cap *= 2;
        s->ev = realloc(s->ev, sizeof(seq_event_t) * (size_t)s->cap);
    }
    seq_event_t *e = &s->ev[s->n];
    memset(e, 0, sizeof *e);
    e->t = t;
    e->order = s->n++;
    e->kind = (uint8_t)kind;
    return e;
}

void seq_midi(seq_t *s, float t, uint8_t a, uint8_t b, uint8_t c)
{
    seq_event_t *e = seq_add(s, t, SEQ_MIDI);
    e->len = ((a & 0xF0) == 0xC0 || (a & 0xF0) == 0xD0) ? 2 : 3;
    e->msg[0] = a;
    e->msg[1] = b;
    e->msg[2] = c;
}

void seq_note(seq_t *s, float t, int note, int vel, float dur)
{
    seq_midi(s, t, 0x90, (uint8_t)note, (uint8_t)vel);
    seq_midi(s, t + dur, 0x80, (uint8_t)note, 0);
}

void seq_param_norm(seq_t *s, float t, int id, float norm)
{
    seq_event_t *e = seq_add(s, t, SEQ_PARAM);
    e->id = id;
    e->value = norm;
}

void seq_va(seq_t *s, float t, int id, float physical)
{
    seq_param_norm(s, t, id, param_to_norm(&va_params[id], physical));
}

void seq_engine(seq_t *s, float t, synth_engine_t eng)
{
    seq_event_t *e = seq_add(s, t, SEQ_ENGINE);
    e->id = (int)eng;
}

void seq_kit(seq_t *s, float t, const void *kit, size_t bytes)
{
    seq_event_t *e = seq_add(s, t, SEQ_KIT);
    e->kit = kit;
    e->kit_bytes = bytes;
}

static int cmp_ev(const void *a, const void *b)
{
    const seq_event_t *x = a, *y = b;
    if (x->t != y->t)
        return x->t < y->t ? -1 : 1;
    return x->order - y->order;
}

int seq_render(seq_t *s, float sr, float seconds, float **Lp, float **Rp)
{
    const int frames = seconds > 0.0f ? (int)(seconds * sr) : 0;
    float *L = calloc((size_t)frames + 1, sizeof(float)), *R = calloc((size_t)frames + 1, sizeof(float));
    if (!L || !R) {
        free(L);
        free(R);
        *Lp = *Rp = NULL;
        return -1;
    }
    qsort(s->ev, (size_t)s->n, sizeof(seq_event_t), cmp_ev);
    synth_init(synth_default(), sr);
    int next = 0;
    for (int pos = 0; pos < frames; pos += SYNTH_BLOCK) {
        const int n = frames - pos < SYNTH_BLOCK ? frames - pos : SYNTH_BLOCK;
        while (next < s->n && (int)(s->ev[next].t * sr) < pos + n) {
            const seq_event_t *e = &s->ev[next++];
            int off = (int)(e->t * sr) - pos;
            if (off < 0)
                off = 0;
            switch (e->kind) {
            case SEQ_MIDI: synth_midi(synth_default(), e->msg, e->len, off); break;
            case SEQ_PARAM: synth_set_param(synth_default(), e->id, e->value); break;
            case SEQ_KIT: synth_part_kit(synth_default(), 0, e->kit, e->kit_bytes); break;
            default: synth_set_engine(synth_default(), (synth_engine_t)e->id); break;
            }
        }
        synth_render(synth_default(), L + pos, R + pos, n);
    }
    *Lp = L;
    *Rp = R;
    return frames;
}

/* ---------------- demos ---------------- */

/* Warm pad: slow attack, slowly opening filter, wide detune and drift, chords with sustain pedal. */
static void demo_pad(seq_t *s)
{
    seq_engine(s, 0.0f, SYNTH_ENGINE_VA);
    seq_va(s, 0.0f, VA_P_AMP_A, 0.6f);
    seq_va(s, 0.0f, VA_P_AMP_R, 1.8f);
    seq_va(s, 0.0f, VA_P_CUTOFF, 500.0f);
    seq_va(s, 0.0f, VA_P_RESO, 1.5f);
    seq_va(s, 0.0f, VA_P_FENV_A, 1.2f);
    seq_va(s, 0.0f, VA_P_FENV_D, 2.5f);
    seq_va(s, 0.0f, VA_P_FENV_S, 0.4f);
    seq_va(s, 0.0f, VA_P_FENV_AMT, 2.0f);
    seq_va(s, 0.0f, VA_P_OSC2_DETUNE, 11.0f);
    seq_va(s, 0.0f, VA_P_OSC2_LEVEL, 0.9f);
    seq_va(s, 0.0f, VA_P_DRIFT, 6.0f);
    seq_va(s, 0.0f, VA_P_SPREAD, 0.9f);
    seq_va(s, 0.0f, VA_P_LFO_RATE, 0.25f);
    seq_va(s, 0.0f, VA_P_LFO_CUTOFF, 0.5f);
    static const int chords[4][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}, {48, 55, 60, 64}, {55, 59, 62, 67}};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            seq_note(s, 2.0f * (float)c, chords[c][k], 70 + 8 * k, 1.9f);
}

/* Acid-ish bass: saw + sub pulse, resonant low-pass with a short filter envelope, accents. */
static void demo_bass(seq_t *s)
{
    seq_engine(s, 0.0f, SYNTH_ENGINE_VA);
    seq_va(s, 0.0f, VA_P_OSC2_WAVE, (float)OSC_PULSE);
    seq_va(s, 0.0f, VA_P_OSC2_OCT, -1.0f);
    seq_va(s, 0.0f, VA_P_OSC2_PW, 0.3f);
    seq_va(s, 0.0f, VA_P_OSC2_DETUNE, 0.0f);
    seq_va(s, 0.0f, VA_P_OSC2_LEVEL, 0.5f);
    seq_va(s, 0.0f, VA_P_CUTOFF, 180.0f);
    seq_va(s, 0.0f, VA_P_RESO, 7.0f);
    seq_va(s, 0.0f, VA_P_FENV_AMT, 4.5f);
    seq_va(s, 0.0f, VA_P_FENV_D, 0.18f);
    seq_va(s, 0.0f, VA_P_FENV_S, 0.0f);
    seq_va(s, 0.0f, VA_P_VEL_CUTOFF, 2.0f);
    seq_va(s, 0.0f, VA_P_AMP_D, 0.25f);
    seq_va(s, 0.0f, VA_P_AMP_S, 0.6f);
    seq_va(s, 0.0f, VA_P_AMP_R, 0.04f);
    seq_va(s, 0.0f, VA_P_SPREAD, 0.0f);
    static const int line[16] = {36, 36, 48, 36, 39, 36, 46, 36, 36, 43, 36, 41, 36, 48, 39, 34};
    const float step = 60.0f / 124.0f / 4.0f;
    for (int bar = 0; bar < 4; bar++)
        for (int i = 0; i < 16; i++) {
            const float t = step * (float)(16 * bar + i);
            const int accent = (i % 4 == 0) || i == 14;
            seq_note(s, t, line[i] + (bar == 2 ? 5 : 0), accent ? 120 : 70, step * 0.6f);
        }
    /* the cutoff slowly opens over the 4 bars */
    for (int k = 0; k <= 200; k++)
        seq_va(s, 7.75f * (float)k / 200.0f, VA_P_CUTOFF, 180.0f * powf(6.0f, (float)k / 200.0f));
}

/* Resonant filter sweep on a held fifth: shows the SVF behaviour under fast modulation. */
static void demo_sweep(seq_t *s)
{
    seq_engine(s, 0.0f, SYNTH_ENGINE_VA);
    seq_va(s, 0.0f, VA_P_RESO, 12.0f);
    seq_va(s, 0.0f, VA_P_FENV_AMT, 0.0f);
    seq_va(s, 0.0f, VA_P_KEYTRACK, 0.0f);
    seq_va(s, 0.0f, VA_P_AMP_S, 1.0f);
    seq_va(s, 0.0f, VA_P_AMP_R, 0.5f);
    seq_va(s, 0.0f, VA_P_CUTOFF, 40.0f);
    seq_note(s, 0.0f, 36, 110, 7.5f);
    seq_note(s, 0.0f, 43, 100, 7.5f);
    for (int k = 0; k <= 800; k++) { /* 40 Hz -> 12 kHz -> 40 Hz, one point every 10 ms */
        const float t = 8.0f * (float)k / 800.0f;
        const float u = t < 4.0f ? t / 4.0f : (8.0f - t) / 4.0f;
        seq_va(s, t, VA_P_CUTOFF, 40.0f * powf(300.0f, u));
    }
}

/* PWM strings-like lead with slow LFO on pulse width, mod-wheel vibrato and pitch bend. */
static void demo_pwm(seq_t *s)
{
    seq_engine(s, 0.0f, SYNTH_ENGINE_VA);
    seq_va(s, 0.0f, VA_P_OSC1_WAVE, (float)OSC_PULSE);
    seq_va(s, 0.0f, VA_P_OSC2_WAVE, (float)OSC_PULSE);
    seq_va(s, 0.0f, VA_P_OSC2_DETUNE, 6.0f);
    seq_va(s, 0.0f, VA_P_LFO_RATE, 0.7f);
    seq_va(s, 0.0f, VA_P_LFO_PWM, 0.35f);
    seq_va(s, 0.0f, VA_P_CUTOFF, 1800.0f);
    seq_va(s, 0.0f, VA_P_FENV_AMT, 1.0f);
    seq_va(s, 0.0f, VA_P_AMP_A, 0.04f);
    seq_va(s, 0.0f, VA_P_AMP_R, 0.6f);
    seq_va(s, 0.0f, VA_P_WHEEL_VIB, 0.4f);
    static const int mel[8] = {69, 72, 76, 74, 72, 71, 72, 69};
    static const float dur[8] = {0.75f, 0.75f, 1.5f, 0.5f, 0.5f, 0.5f, 1.0f, 2.0f};
    float t = 0.0f;
    for (int i = 0; i < 8; i++) {
        seq_note(s, t, mel[i], 100, dur[i] * 0.95f);
        seq_note(s, t, mel[i] - 12, 80, dur[i] * 0.95f);
        t += dur[i];
    }
    for (int k = 0; k <= 100; k++) /* mod wheel ramps up during the last note */
        seq_midi(s, 5.5f + 2.0f * (float)k / 100.0f, 0xB0, 1, (uint8_t)(127 * k / 100));
    for (int k = 0; k <= 50; k++) /* small bend down at the end */
        seq_midi(s, 7.0f + 0.5f * (float)k / 50.0f, 0xE0, 0, (uint8_t)(64 - 16 * k / 50));
}

const demo_t demos[] = {
    {"va_pad", SYNTH_ENGINE_VA, "warm detuned pad, slowly opening filter", 9.5f, demo_pad},
    {"va_bass", SYNTH_ENGINE_VA, "resonant bass line with filter envelope and accents", 8.5f, demo_bass},
    {"va_sweep", SYNTH_ENGINE_VA, "resonant low-pass sweep 40 Hz -> 12 kHz -> 40 Hz", 8.5f, demo_sweep},
    {"va_pwm", SYNTH_ENGINE_VA, "PWM lead, mod-wheel vibrato, pitch bend", 9.0f, demo_pwm},
};
const int demo_count = (int)(sizeof demos / sizeof demos[0]);

const demo_t *demo_find(const char *name)
{
    for (int i = 0; i < demo_count; i++)
        if (strcmp(demos[i].name, name) == 0)
            return &demos[i];
    return NULL;
}

const demo_t *demo_for_engine(synth_engine_t e)
{
    for (int i = 0; i < demo_count; i++)
        if (demos[i].engine == e)
            return &demos[i];
    return NULL;
}
