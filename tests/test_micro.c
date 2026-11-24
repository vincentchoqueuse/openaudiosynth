/* test_micro.c — precision sine engine: a note starts on the sample asked for, envelope lengths exact to the sample
 * (and a 20 s segment that does not drift), curves on the right side of linear, fixed and keyed pitch, a pitch
 * envelope that lands on the note, tone bursts of an exact number of cycles, exact beating in Hz split left/right,
 * ratchets on their interval and dying away, a gate pattern on the beat, the noise layer (inverse gate, a tick per
 * step, the periodic and stepped kinds), the low layer (on its sample, exact length, two hits identical, its
 * pitch fixed or from the note, hits on its own gate), a glide that arrives, and every
 * parameter at its extremes bounded and deterministic. Engine-level renders (no master stage) unless noted. */
#include <math.h>
#include <string.h>

#include "engines/micro.h"
#include "synth.h"
#include "test.h"

#define SR 48000.0f
#define BL SYNTH_BLOCK
#define N (4 * 48000)

static micro_t eng;
static float L[N], R[N], L2[N];

static void setp(int id, float v) { micro_engine.set_param(&eng, id, param_to_norm(&micro_params[id], v)); }
static void fresh(void)
{
    micro_engine.init(&eng, SR);
    setp(MICRO_P_VELOCITY, 0.0f);
}
static void note_at(int note, int onset)
{
    eng.poly.onset = onset;
    vpoly_note_on(&eng.poly, note, 1.0f);
    eng.poly.onset = 0;
}
static void off_at(int note, int onset)
{
    eng.poly.onset = onset;
    vpoly_note_off(&eng.poly, note);
    eng.poly.onset = 0;
}
/* n samples (a multiple of the block) into l, r from position 0, with a tempo of bpm from beat 0 */
static float beat_pos;
static void run(float *l, float *r, int n, float bpm)
{
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i += BL) {
        beat_pos += bpm * (float)BL / (60.0f * SR);
        micro_engine.tempo(&eng, bpm, beat_pos);
        micro_engine.render(&eng, l + i, r + i);
    }
}
static int first_nonzero(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (x[i] != 0.0f)
            return i;
    return -1;
}
static int last_nonzero(const float *x, int n)
{
    for (int i = n - 1; i >= 0; i--)
        if (x[i] != 0.0f)
            return i;
    return -1;
}

void suite_micro(void)
{
    /* onset: a note asked at sample 37 of the stream starts on sample 37 through the synth (block 1, offset 5) */
    {
        synth_t *s = synth_default();
        synth_init(s, SR);
        synth_set_engine(s, SYNTH_ENGINE_MICRO);
        synth_set_param(s, MICRO_P_PHASE, param_to_norm(&micro_params[MICRO_P_PHASE], 90.0f));
        synth_set_param(s, MICRO_P_ATTACK, 0.0f);
        float a[256], b[256];
        synth_render(s, a, b, 32); /* the engine switch */
        const uint8_t on[3] = {0x90, 69, 100};
        synth_midi(s, on, 3, 37);
        synth_render(s, a, b, 256);
        const int at = first_nonzero(a, 256);
        CHECK(at == 37, "micro onset: first sample %d, expected 37", at);
    }
    /* the same at the engine: offsets 0 and 19 of a block */
    for (int k = 0; k < 2; k++) {
        const int want = k ? 19 : 0;
        fresh();
        setp(MICRO_P_PHASE, 90.0f);
        setp(MICRO_P_ATTACK, 0.00002f);
        note_at(69, want);
        run(L, R, 256, 120.0f);
        const int at = first_nonzero(L, 256);
        CHECK(at == want, "micro engine onset: first sample %d, expected %d", at, want);
    }

    /* exact length: attack 1 sample, no hold, linear decay of 0.2 s to 0: the last sound on sample 9600 */
    fresh();
    setp(MICRO_P_PHASE, 90.0f);
    setp(MICRO_P_ATTACK, 0.00002f);
    setp(MICRO_P_DECAY, 0.2f);
    setp(MICRO_P_DECAY_CURVE, 0.0f);
    setp(MICRO_P_FREQ, 1000.0f);
    setp(MICRO_P_PITCH_MODE, 1.0f);
    note_at(60, 0);
    run(L, R, 12800, 120.0f);
    {
        const int last = last_nonzero(L, 12800);
        CHECK(last >= 9598 && last <= 9600, "micro decay 0.2 s: last sound at sample %d, expected 9599 +/- 1", last);
    }
    /* hold of 10 ms then decay: full level right to the end of the hold */
    fresh();
    setp(MICRO_P_ATTACK, 0.00002f);
    setp(MICRO_P_HOLD, 0.01f);
    note_at(60, 0);
    run(L, R, 512, 120.0f); /* 1 + 480 samples of attack and hold, then 31 of a 0.3 s decay */
    CHECK(eng.v[0].stage == MICRO_ENV_DECAY && eng.v[0].amp.y > 0.95f && eng.v[0].amp.y < 1.0f,
          "micro hold: stage %d, level %g after attack + hold", eng.v[0].stage, (double)eng.v[0].amp.y);

    /* attack curves, halfway through a 20 ms attack: linear 0.5, fast start above, slow start below */
    {
        float mid[3];
        const float c[3] = {0.0f, 0.8f, -0.8f};
        for (int k = 0; k < 3; k++) {
            fresh();
            setp(MICRO_P_ATTACK, 0.02f);
            setp(MICRO_P_ATTACK_CURVE, c[k]);
            note_at(60, 0);
            run(L, R, 480, 120.0f);
            mid[k] = eng.v[0].amp.y;
        }
        CHECK(fabsf(mid[0] - 0.5f) < 2e-3f && mid[1] > 0.7f && mid[2] < 0.3f,
              "micro attack curves at half time: linear %.4f, fast %.3f, slow %.3f", (double)mid[0], (double)mid[1],
              (double)mid[2]);
    }
    /* a linear 20 s decay is at 0.5 after 10 s (no drift over 480000 steps) */
    fresh();
    setp(MICRO_P_ATTACK, 0.00002f);
    setp(MICRO_P_DECAY, 20.0f);
    setp(MICRO_P_DECAY_CURVE, 0.0f);
    note_at(60, 0);
    for (int b = 0; b < 15000; b++) { /* 10 s = 15000 blocks */
        float l[BL] = {0}, r[BL] = {0};
        micro_engine.render(&eng, l, r);
    }
    CHECK(fabsf(eng.v[0].amp.y - 0.5f) < 1e-4f, "micro 20 s decay: %.6f after 10 s, expected 0.5",
          (double)eng.v[0].amp.y);

    /* pitch: keyed A4, and the fixed mode ignoring the key */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    note_at(69, 0);
    run(L, R, N, 120.0f);
    {
        const float f = estimate_freq(L + 4800, 48000, SR);
        CHECK(fabsf(f - 440.0f) < 0.2f, "micro keyed A4: %.3f Hz", (double)f);
    }
    for (int k = 0; k < 2; k++) {
        fresh();
        setp(MICRO_P_SUSTAIN, 1.0f);
        setp(MICRO_P_PITCH_MODE, 1.0f);
        setp(MICRO_P_FREQ, 1234.0f);
        note_at(k ? 30 : 100, 0);
        run(L, R, N, 120.0f);
        const float f = estimate_freq(L + 4800, 48000, SR);
        CHECK(fabsf(f - 1234.0f) < 0.5f, "micro fixed 1234 Hz (key %d): %.3f Hz", k ? 30 : 100, (double)f);
    }

    /* pitch envelope +36 st over 40 ms: two octaves up near the start, on the note after 60 ms */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_PITCH_MODE, 1.0f);
    setp(MICRO_P_FREQ, 50.0f);
    setp(MICRO_P_PITCH_AMOUNT, 36.0f);
    setp(MICRO_P_PITCH_TIME, 0.04f);
    note_at(60, 0);
    run(L, R, N, 120.0f);
    {
        int zc = 0; /* zero crossings in the first 2 ms: the pitch starts near 400 Hz */
        for (int i = 1; i < 96; i++)
            zc += (L[i - 1] < 0.0f) != (L[i] < 0.0f);
        const float f = estimate_freq(L + 4800, 48000, SR);
        CHECK(zc >= 1 && fabsf(f - 50.0f) < 0.2f, "micro pitch envelope: %d crossings in 2 ms, then %.3f Hz", zc,
              (double)f);
    }

    /* a burst of 4 cycles at 1 kHz: exactly 4 ms of sound (192 samples) */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_ATTACK, 0.00002f);
    setp(MICRO_P_PITCH_MODE, 1.0f);
    setp(MICRO_P_FREQ, 1000.0f);
    setp(MICRO_P_BURST, 4.0f);
    note_at(60, 3);
    run(L, R, 1024, 120.0f);
    {
        const int a = first_nonzero(L, 1024), b = last_nonzero(L, 1024);
        CHECK(a == 4 && b >= 3 + 190 && b <= 3 + 193, "micro burst of 4 cycles: sound on %d .. %d, expected 4 .. 194",
              a, b);
    }

    /* exact beating: 1000 Hz on the left, 1001.5 Hz on the right */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_PITCH_MODE, 1.0f);
    setp(MICRO_P_FREQ, 1000.0f);
    setp(MICRO_P_OSC2_LEVEL, 0.0f);
    setp(MICRO_P_OSC2_MODE, 1.0f);
    setp(MICRO_P_OSC2_OFFSET, 1.5f);
    setp(MICRO_P_OSC2_SPLIT, 1.0f);
    note_at(60, 0);
    run(L, R, N, 120.0f);
    {
        const float fl = estimate_freq(L + 4800, 96000, SR), fr = estimate_freq(R + 4800, 96000, SR);
        CHECK(fabsf(fl - 1000.0f) < 0.1f && fabsf(fr - 1001.5f) < 0.1f, "micro split beating: L %.3f Hz, R %.3f Hz",
              (double)fl, (double)fr);
    }

    /* ratchet: 4 hits 100 ms apart, each 6 dB down, then nothing */
    fresh();
    setp(MICRO_P_ATTACK, 0.00002f);
    setp(MICRO_P_DECAY, 0.02f);
    setp(MICRO_P_RATCHET, 4.0f);
    setp(MICRO_P_RATCHET_DIV, 0.0f);
    setp(MICRO_P_RATCHET_HZ, 10.0f);
    setp(MICRO_P_RATCHET_DECAY, -6.0f);
    note_at(81, 0);
    off_at(81, 10);
    run(L, R, 24000, 120.0f);
    {
        float pk[5];
        for (int k = 0; k < 5; k++)
            pk[k] = max_abs(L + k * 4800, 2400);
        const int on = first_nonzero(L + 4800, 4800);
        CHECK(pk[0] > 0.05f && fabsf(pk[1] / pk[0] - 0.501f) < 0.03f && fabsf(pk[3] / pk[2] - 0.501f) < 0.03f &&
                  pk[4] == 0.0f && on == 1,
              "micro ratchet: peaks %.3f %.3f %.3f %.3f %.3f, second hit at +%d", (double)pk[0], (double)pk[1],
              (double)pk[2], (double)pk[3], (double)pk[4], on);
    }

    /* gate pattern on 1/16 at 120 BPM (6000 samples a step): steps 1, 3, 5... open (0x5555) */
    fresh();
    beat_pos = 0.0f;
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_GATE_MODE, 1.0f);
    setp(MICRO_P_GATE_DIV, 3.0f);
    setp(MICRO_P_GATE_PATTERN, 21845.0f);
    note_at(81, 0);
    run(L, R, 48000, 120.0f);
    {
        const float open = rms(L + 6000 * 2 + 500, 5000), shut = rms(L + 6000 * 3 + 500, 5000);
        CHECK(open > 0.03f && shut < 1e-6f, "micro gate pattern: open step %g, closed step %g", (double)open,
              (double)shut);
    }

    /* noise layer on the inverse gate: silent on the tone's open steps, loud in its holes; with the tone on too,
     * no step is silent (tone and noise interlock) */
    for (int with_tone = 0; with_tone < 2; with_tone++) {
        fresh();
        beat_pos = 0.0f;
        setp(MICRO_P_SUSTAIN, 1.0f);
        setp(MICRO_P_GATE_MODE, 1.0f);
        setp(MICRO_P_GATE_DIV, 3.0f);
        setp(MICRO_P_GATE_PATTERN, 21845.0f);
        setp(MICRO_P_SINE_LEVEL, with_tone ? 0.0f : -60.0f);
        setp(MICRO_P_NOISE_LEVEL, 0.0f);
        setp(MICRO_P_NOISE_GATE, 2.0f);
        note_at(81, 0);
        run(L, R, 48000, 120.0f);
        const float tone_step = rms(L + 6000 * 2 + 500, 5000), hole = rms(L + 6000 * 3 + 500, 5000);
        if (!with_tone)
            CHECK(tone_step < 1e-6f && hole > 0.01f, "micro noise inverse gate: tone step %g, hole %g",
                  (double)tone_step, (double)hole);
        else
            CHECK(tone_step > 0.03f && hole > 0.01f, "micro tone + noise interlock: tone step %g, hole %g",
                  (double)tone_step, (double)hole);
    }

    /* step envelope: every step opens a 10 ms tick of noise; loud at its start, gone well before the next one */
    fresh();
    beat_pos = 0.0f;
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_GATE_MODE, 1.0f);
    setp(MICRO_P_GATE_DIV, 3.0f);
    setp(MICRO_P_GATE_PATTERN, 65535.0f);
    setp(MICRO_P_NOISE_LEVEL, 0.0f);
    setp(MICRO_P_NOISE_ENV, 2.0f);
    setp(MICRO_P_NOISE_DECAY, 0.01f);
    note_at(81, 0);
    run(L, R, 48000, 120.0f);
    {
        float head = 1.0f, tail = 0.0f;
        for (int k = 1; k < 7; k++) {
            head = fminf(head, rms(L + 6000 * k, 100));
            tail = fmaxf(tail, rms(L + 6000 * k + 3000, 2000));
        }
        CHECK(head > 0.02f && tail < 1e-4f, "micro noise step ticks: head %g, tail %g", (double)head, (double)tail);
    }

    /* periodic noise: a 127-tick sequence clocked at 12.7 kHz repeats every 480 samples (100 Hz); stepped noise
     * at 1 kHz changes value 1000 times a second */
    for (int kind = 2; kind >= 1; kind--) {
        fresh();
        setp(MICRO_P_SUSTAIN, 1.0f);
        setp(MICRO_P_SINE_LEVEL, -60.0f);
        setp(MICRO_P_NOISE_LEVEL, 0.0f);
        setp(MICRO_P_NOISE_TYPE, (float)kind);
        setp(MICRO_P_NOISE_FILTER, 0.0f);
        setp(MICRO_P_NOISE_RATE, kind == 2 ? 12700.0f : 1000.0f);
        note_at(60, 0);
        run(L, R, 48000, 120.0f);
        const float *x = L + 24000;
        if (kind == 2) {
            double c480 = 0.0, c240 = 0.0, e0 = 0.0;
            for (int i = 0; i < 12000; i++) {
                c480 += (double)x[i] * (double)x[i + 480];
                c240 += (double)x[i] * (double)x[i + 240];
                e0 += (double)x[i] * (double)x[i];
            }
            CHECK(c480 / e0 > 0.9 && fabs(c240 / e0) < 0.3, "micro periodic noise: correlation %.3f at 480, %.3f at 240",
                  c480 / e0, c240 / e0);
        } else {
            int changes = 0;
            for (int i = 1; i < 24000; i++)
                changes += fabsf(x[i] - x[i - 1]) > 1e-3f * fabsf(x[i]) + 1e-7f;
            CHECK(changes >= 495 && changes <= 501, "micro stepped noise: %d changes in 0.5 s, expected 500", changes);
        }
    }

    /* glide in mono: from A3 to A4 in 0.1 s, arrived after 0.2 s */
    fresh();
    setp(MICRO_P_VOICE_MODE, 1.0f);
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_GLIDE, 0.1f);
    note_at(57, 0);
    run(L, R, 9600, 120.0f);
    note_at(69, 0);
    run(L, R, N, 120.0f);
    {
        const float f = estimate_freq(L + 9600, 48000, SR);
        CHECK(fabsf(f - 440.0f) < 0.3f, "micro glide: %.3f Hz after the glide, expected 440", (double)f);
    }

    /* click alone: an impulse with no sine is short and finite */
    fresh();
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_CLICK_MODE, 1.0f);
    setp(MICRO_P_CLICK_LEVEL, 0.0f);
    note_at(60, 0);
    run(L, R, 48000, 120.0f);
    {
        const float pk = max_abs(L, 480), tail = max_abs(L + 4800, 4800);
        printf("  micro impulse click: peak %.3f\n", (double)pk);
        CHECK(pk > 0.03f && pk < 0.2f && tail < 1e-4f, "micro impulse click: peak %g, tail %g", (double)pk,
              (double)tail);
    }

    /* the low layer alone, fixed at 50 Hz from its top (phase 90), attack 1 sample, hold 0.1 s, linear decay
     * 0.2 s: it starts on the note's sample (19) and its last sound is 1 + 4800 + 9600 samples later */
    fresh();
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    setp(MICRO_P_LOW_FREQ, 50.0f);
    setp(MICRO_P_LOW_PHASE, 90.0f);
    setp(MICRO_P_LOW_ATTACK, 0.00002f);
    setp(MICRO_P_LOW_HOLD, 0.1f);
    setp(MICRO_P_LOW_DECAY, 0.2f);
    setp(MICRO_P_LOW_CURVE, 0.0f);
    note_at(60, 19);
    run(L, R, 19200, 120.0f);
    {
        const int first = first_nonzero(L, 19200), last = last_nonzero(L, 19200);
        CHECK(first == 19 && last >= 19 + 14397 && last <= 19 + 14400, "micro low envelope: sound from %d to %d, expected 19 to %d",
              first, last, 19 + 14399);
        CHECK(fabsf(L[19 + 2500] - L[19 + 4420]) < 1e-3f * fabsf(L[19 + 2500]) && fabsf(L[19 + 2500]) > 0.01f,
              "micro low hold: %g then %g two periods later", (double)L[19 + 2500], (double)L[19 + 4420]);
    }
    /* a second note once the first is over: the same samples, bit for bit (same phase, same envelope) */
    off_at(60, 0);
    run(L2, R, 4800, 120.0f);
    note_at(60, 19);
    run(L2, R, 19200, 120.0f);
    CHECK(memcmp(L, L2, sizeof(float) * 19200) == 0, "micro low: two hits are not the same samples");

    /* its pitch: 50 Hz held a second; in note mode C#4 is brought into the octave above E1 (41.2 Hz): C#2 */
    for (int mode = 0; mode < 2; mode++) {
        fresh();
        setp(MICRO_P_SINE_LEVEL, -60.0f);
        setp(MICRO_P_LOW_LEVEL, 0.0f);
        setp(MICRO_P_LOW_MODE, (float)mode);
        setp(MICRO_P_LOW_FREQ, mode ? 41.2034f : 50.0f);
        setp(MICRO_P_LOW_HOLD, 2.0f);
        note_at(61, 0);
        run(L, R, 96000, 120.0f);
        const float f = estimate_freq(L + 9600, 65536, SR), want = mode ? 69.2957f : 50.0f;
        CHECK(fabsf(f - want) < 0.005f * want, "micro low pitch (mode %d): %.3f Hz, expected %.3f", mode, (double)f, (double)want);
    }

    /* on its own gate pattern (one step in four of sixteenths at 120 bpm): a hit every 0.5 s while the note is held */
    fresh();
    beat_pos = 0.0f;
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_GATE_MODE, 1.0f);
    setp(MICRO_P_GATE_DIV, 3.0f);
    setp(MICRO_P_GATE_PATTERN, 65535.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    setp(MICRO_P_LOW_GATE, 3.0f);
    setp(MICRO_P_LOW_PATTERN, 4369.0f);
    setp(MICRO_P_LOW_HOLD, 0.0f);
    setp(MICRO_P_LOW_DECAY, 0.1f);
    note_at(60, 0);
    run(L, R, 96000, 120.0f);
    {
        float head = 1.0f, tail = 0.0f;
        for (int k = 1; k < 4; k++) {
            head = fminf(head, rms(L + 24000 * k, 2000));
            tail = fmaxf(tail, rms(L + 24000 * k + 8000, 12000));
        }
        CHECK(head > 0.02f && tail < 1e-5f, "micro low gate: rms %g on its steps, %g between", (double)head, (double)tail);
    }

    /* every parameter at its top, then its bottom, a cluster through the synth: finite, bounded, deterministic */
    for (int ext = 0; ext < 2; ext++)
        for (int run2 = 0; run2 < 2; run2++) {
            synth_t *s = synth_default();
            synth_init(s, SR);
            synth_set_engine(s, SYNTH_ENGINE_MICRO);
            for (int id = 0; id < MICRO_P_COUNT; id++)
                if (id != MICRO_P_VOLUME)
                    synth_set_param(s, id, ext ? 1.0f : 0.0f);
            for (int k = 0; k < 6; k++) {
                const uint8_t m[3] = {0x90, (uint8_t)(24 + 13 * k), 127};
                synth_midi(s, m, 3, 7 * k);
            }
            float *out = run2 ? L2 : L;
            for (int i = 0; i < N; i += 256)
                synth_render(s, out + i, R + i, 256);
            if (run2) {
                CHECK(all_finite_bounded(L, N, 1.0f), "micro extremes %d: NaN or > 0 dBFS (max %g)", ext,
                      (double)max_abs(L, N));
                CHECK(memcmp(L, L2, sizeof(float) * N) == 0, "micro extremes %d: not deterministic", ext);
            }
        }
}
