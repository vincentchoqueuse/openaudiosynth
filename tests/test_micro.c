/* test_micro.c — precision sine engine: a note starts on the sample asked for, envelope lengths exact to the sample
 * (and a 20 s segment that does not drift), curves on the right side of linear, fixed and keyed pitch, a pitch
 * envelope that lands on the note, tone bursts of an exact number of cycles, exact beating in Hz split left/right,
 * ratchets on their interval and dying away, a gate pattern on the beat, the noise layer (inverse gate, a tick per
 * step, the periodic and stepped kinds), the low layer (on its sample, exact length, two hits identical, its
 * pitch fixed or from the note, hits on its own gate), a glide that arrives, and every
 * parameter at its extremes bounded and deterministic; the grid (a frequency moved to a whole number of cycles per
 * step, the low layer too) and the space (hard sides, sides that alternate on the sample of each hit, one channel an
 * exact number of samples late); the pulses (on / off on the sample of the step, an offset, two lengths meeting every
 * six steps, the edge, the target, free mode at audio rate). Engine-level renders (no master stage) unless noted. */
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

/* the grid and the space: lock, space hard / alternate, space_delay */
static void grid_and_space(void)
{
    /* lock 1/16 at 120 BPM (0.125 s): 1003 Hz goes to 1000 Hz, 125 whole cycles, and the low layer's 45 Hz to 48 Hz */
    fresh();
    setp(MICRO_P_PITCH_MODE, 1.0f);
    setp(MICRO_P_FREQ, 1003.0f);
    setp(MICRO_P_LOCK, 3.0f);
    setp(MICRO_P_SUSTAIN, 1.0f); /* a steady level: a step later the wave must be the same */
    note_at(60, 0);
    beat_pos = 0.0f;
    run(L, R, 24000, 120.0f);
    const float f = estimate_freq(L + 2400, 19200, SR);
    printf("  lock 1/16 at 120 BPM: 1003 Hz plays %.3f Hz\n", (double)f);
    CHECK(fabsf(f - 1000.0f) < 0.2f, "lock: 1003 Hz plays %.3f Hz instead of 1000", (double)f);
    /* a whole number of cycles in a step: the wave at the end of a step is the wave at its start */
    float worst = 0.0f;
    for (int i = 2400; i < 2400 + 480; i++)
        worst = fmaxf(worst, fabsf(L[i + 6000] - L[i]));
    CHECK(worst < 2e-3f * max_abs(L + 2400, 480), "lock: a step later the wave differs by %g", (double)worst);
    fresh();
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    setp(MICRO_P_LOW_DECAY, 2.0f);
    setp(MICRO_P_LOCK, 3.0f);
    note_at(60, 0);
    beat_pos = 0.0f;
    run(L, R, 24000, 120.0f);
    const float fl = estimate_freq(L + 2400, 19200, SR);
    printf("  lock 1/16: the low layer's 45 Hz plays %.3f Hz\n", (double)fl);
    CHECK(fabsf(fl - 48.0f) < 0.05f, "lock: low 45 Hz plays %.3f Hz instead of 48", (double)fl);

    /* space hard: a pan of 0.6 is fully right, nothing on the left */
    fresh();
    setp(MICRO_P_PAN, 0.6f);
    setp(MICRO_P_SPACE, 1.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 9600, 120.0f);
    CHECK(max_abs(L, 9600) == 0.0f && max_abs(R, 9600) > 0.01f, "space hard: left %g, right %g", (double)max_abs(L, 9600),
          (double)max_abs(R, 9600));

    /* space alternate: ratchet hits 100 ms apart, left, right, left; the side changes on the hit's own sample */
    fresh();
    setp(MICRO_P_SPACE, 2.0f);
    setp(MICRO_P_RATCHET, 3.0f);
    setp(MICRO_P_RATCHET_DIV, 0.0f); /* off the tempo: at ratchet_hz */
    setp(MICRO_P_RATCHET_HZ, 10.0f);
    setp(MICRO_P_ENV_MODE, 1.0f);
    setp(MICRO_P_DECAY, 0.05f);
    note_at(69, 7);
    beat_pos = 0.0f;
    run(L, R, 19200, 120.0f);
    const int l0 = first_nonzero(L, 19200), r0 = first_nonzero(R, 19200);
    const float rl = max_abs(R, 4800 + 7), lr = max_abs(L + 4800 + 7, 4800), ll = max_abs(L + 9600 + 7, 4800);
    printf("  space alternate: first left sample %d, first right %d (hit 2 at %d)\n", l0, r0, 4800 + 7);
    CHECK(l0 >= 7 && l0 <= 8 && r0 == l0 + 4800 && rl == 0.0f && lr == 0.0f && ll > 0.01f, /* a hit's first sample is its envelope's 0 */
          "space alternate: left from %d, right from %d, right before %g, left during hit 2 %g, left at hit 3 %g", l0, r0,
          (double)rl, (double)lr, (double)ll);

    /* space_delay 10: the right channel is the left one, ten samples late, to the bit */
    fresh();
    setp(MICRO_P_CLICK_MODE, 1.0f);
    setp(MICRO_P_SPACE_DELAY, 10.0f);
    note_at(69, 3);
    beat_pos = 0.0f;
    run(L, R, 9600, 120.0f);
    int same = first_nonzero(R, 9600) == first_nonzero(L, 9600) + 10;
    for (int i = 0; i + 10 < 9600; i++)
        same &= R[i + 10] == L[i];
    CHECK(same, "space_delay 10: the right is not the left ten samples late (left from %d, right from %d)",
          first_nonzero(L, 9600), first_nonzero(R, 9600));
    setp(MICRO_P_SPACE_DELAY, -10.0f);
    fresh();
    setp(MICRO_P_CLICK_MODE, 1.0f);
    setp(MICRO_P_SPACE_DELAY, -10.0f);
    note_at(69, 3);
    beat_pos = 0.0f;
    run(L, R, 9600, 120.0f);
    CHECK(first_nonzero(L, 9600) == first_nonzero(R, 9600) + 10, "space_delay -10: left from %d, right from %d",
          first_nonzero(L, 9600), first_nonzero(R, 9600));
}

/* the pulses: on / off on the exact sample, lengths that drift apart, an offset, the edge, what they open (a mask) */
static void set_pulse(int k, float mode, float div, float steps, float pattern, float offset, float open)
{
    setp(MICRO_PU(k, MICRO_PU_MODE), mode);
    setp(MICRO_PU(k, MICRO_PU_DIV), div);
    setp(MICRO_PU(k, MICRO_PU_STEPS), steps);
    setp(MICRO_PU(k, MICRO_PU_PATTERN), pattern);
    setp(MICRO_PU(k, MICRO_PU_OFFSET), offset);
    setp(MICRO_PU(k, MICRO_PU_OPEN), open);
}
static void pulses(void)
{
    /* pulse 1 on 1/16 at 120 BPM (6000 samples a step), every other step from step 1: open, shut, open... and each
     * change on the sample where its step starts (edge one sample) */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 21845.0f, 0.0f, 1.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 30000, 120.0f);
    const float s0 = max_abs(L + 100, 5800), s1 = max_abs(L + 6000, 6000), s2 = max_abs(L + 12000, 6000);
    int edge_ok = L[5999] != 0.0f && L[6000] == 0.0f && L[11999] == 0.0f && L[12000] != 0.0f;
    printf("  pulse 1/16 every other step: %.3f, %.3f, %.3f; edges on samples 6000 and 12000: %s\n", (double)s0, (double)s1,
           (double)s2, edge_ok ? "yes" : "no");
    CHECK(s0 > 0.05f && s1 == 0.0f && s2 > 0.05f && edge_ok, "pulse: steps %g %g %g, edges %d", (double)s0, (double)s1,
          (double)s2, edge_ok);
    /* an offset of one step: the same pattern shifted (shut, open, shut) */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 21845.0f, 1.0f, 1.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 18000, 120.0f);
    CHECK(max_abs(L, 6000) == 0.0f && max_abs(L + 6000, 6000) > 0.05f && max_abs(L + 12000, 6000) == 0.0f,
          "pulse offset 1: %g %g %g", (double)max_abs(L, 6000), (double)max_abs(L + 6000, 6000), (double)max_abs(L + 12000, 6000));
    /* two pulses of 3 and 2 steps on one layer, each open on its first step only: the layer sounds where both are
     * open, every 6 steps (steps 0 and 6), and nowhere else */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    set_pulse(0, 1.0f, 3.0f, 3.0f, 1.0f, 0.0f, 1.0f);
    set_pulse(1, 1.0f, 3.0f, 2.0f, 1.0f, 0.0f, 1.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 48000, 120.0f);
    int open = 0, where = 0;
    for (int s = 0; s < 8; s++)
        if (max_abs(L + s * 6000 + 50, 5900) > 0.05f)
            open++, where |= 1 << s;
    printf("  pulses of 3 and 2 steps: open on steps mask 0x%x (expected 0x41)\n", where);
    CHECK(where == 0x41 && open == 2, "pulses 3 x 2: open steps mask 0x%x", where);
    /* the edge: 5 ms (240 samples) from shut to open, half way after 120 */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_PITCH_MODE, 1.0f);
    setp(MICRO_P_FREQ, 4000.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 2.0f, 0.0f, 1.0f);
    setp(MICRO_PU(0, MICRO_PU_EDGE), 0.005f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 18000, 120.0f);
    const float full = max_abs(L + 6000 + 600, 1200), mid = max_abs(L + 6000 + 110, 20);
    printf("  pulse edge 5 ms: %.2f of full 120 samples in\n", (double)(mid / full));
    CHECK(max_abs(L + 5000, 1000) == 0.0f && mid > 0.4f * full && mid < 0.6f * full, "pulse edge: %.2f of full halfway",
          (double)(mid / full));
    /* the target: a pulse on the noise leaves the sine alone */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 0.0f, 0.0f, 4.0f); /* always shut, on the noise */
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 9600, 120.0f);
    CHECK(max_abs(L + 100, 9000) > 0.05f, "a pulse on the noise silences the sine");
    /* free mode at 1 kHz on 2 steps (pattern 1): a 500 Hz on / off, the first switch 48 samples after the note */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    set_pulse(0, 2.0f, 3.0f, 2.0f, 1.0f, 0.0f, 1.0f);
    setp(MICRO_PU(0, MICRO_PU_HZ), 1000.0f);
    note_at(69, 16);
    beat_pos = 0.0f;
    run(L, R, 9600, 120.0f);
    /* the note's hit is the sample before its first sound (its envelope starts at 0): the steps count from there */
    int z0 = -1, o1 = -1;
    const int h = first_nonzero(L, 9600) - 1;
    for (int i = h + 1; i < 400; i++) {
        if (z0 < 0 && L[i] == 0.0f && L[i - 1] != 0.0f)
            z0 = i;
        else if (z0 >= 0 && o1 < 0 && L[i] != 0.0f)
            o1 = i;
    }
    printf("  free pulse 1 kHz: hit at %d, shut from %d, open again at %d\n", h, z0, o1);
    CHECK(z0 == h + 48 && o1 == h + 96, "free pulse 1 kHz: hit at %d, shut from %d, open again at %d", h, z0, o1);
}

/* what a pulse opens beyond the layers: ortho (an open step is the ortho noise, a closed one the sine) and the low
 * layer's hit (every step that opens hits it on its own sample) */
static void pulse_targets(void)
{
    /* the reference: the plain sine, no pulse */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L2, R, 18000, 120.0f);
    /* ortho at 1 (noise), pulse 1 on 1/16 every other step from step 1, target ortho */
    fresh();
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_ORTHO, 1.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 21845.0f, 0.0f, 16.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 18000, 120.0f);
    float open_d = 0.0f, shut_d = 0.0f;
    for (int i = 100; i < 5900; i++)
        open_d = fmaxf(open_d, fabsf(L[i] - L2[i]));
    for (int i = 6100; i < 11900; i++)
        shut_d = fmaxf(shut_d, fabsf(L[i] - L2[i]));
    printf("  pulse on ortho: open step %.3f from the sine, closed step %.2g\n", (double)open_d, (double)shut_d);
    CHECK(open_d > 0.05f && shut_d < 1e-5f, "pulse ortho: open %g, closed %g from the sine", (double)open_d, (double)shut_d);

    /* trigger: the low layer alone (short), pulse 1 open on steps 1, 5, 9 (0x111): a hit on each, silence between */
    fresh();
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    setp(MICRO_P_LOW_DECAY, 0.02f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 273.0f, 0.0f, 32.0f);
    note_at(69, 0);
    beat_pos = 0.0f;
    run(L, R, 60000, 120.0f);
    const float h4 = max_abs(L + 24000, 2000), gap = max_abs(L + 15000, 9000), h8 = max_abs(L + 48000, 2000);
    const int f4 = first_nonzero(L + 23000, 2000), on4 = f4 == 1000 || f4 == 1001; /* the low sine starts at phase 0 */
    printf("  pulse low hits: hits %.3f and %.3f on steps 5 and 9, %.2g between; the hit on sample 24000: %s\n",
           (double)h4, (double)h8, (double)gap, on4 ? "yes" : "no");
    CHECK(h4 > 0.05f && h8 > 0.05f && gap < 1e-4f && on4, "pulse low hits: %g %g, between %g, first sound at %d", (double)h4,
          (double)h8, (double)gap, 23000 + f4);
}

void suite_micro(void)
{
    grid_and_space();
    pulses();
    pulse_targets();
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
    setp(MICRO_P_RATCHET_DIV, 0.0f); /* off the tempo: at ratchet_hz */
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

    /* the noise on the pulse's inverse (NOT): silent on the tone's open steps, loud in its holes; with the tone on
     * the pulse too, no step is silent (tone and noise interlock) */
    for (int with_tone = 0; with_tone < 2; with_tone++) {
        fresh();
        beat_pos = 0.0f;
        setp(MICRO_P_SUSTAIN, 1.0f);
        set_pulse(0, 1.0f, 3.0f, 16.0f, 21845.0f, 0.0f, with_tone ? 1.0f : 0.0f);
        setp(MICRO_PU(0, MICRO_PU_OPEN_NOT), 4.0f);
        setp(MICRO_P_SINE_LEVEL, with_tone ? 0.0f : -60.0f);
        setp(MICRO_P_NOISE_LEVEL, 0.0f);
        note_at(81, 0);
        run(L, R, 48000, 120.0f);
        const float tone_step = rms(L + 6000 * 2 + 500, 5000), hole = rms(L + 6000 * 3 + 500, 5000);
        if (!with_tone)
            CHECK(tone_step < 1e-6f && hole > 0.01f, "micro noise on NOT: tone step %g, hole %g", (double)tone_step,
                  (double)hole);
        else
            CHECK(tone_step > 0.03f && hole > 0.01f, "micro tone + noise interlock: tone step %g, hole %g",
                  (double)tone_step, (double)hole);
    }
    /* and exactly complementary: tone alone plus noise alone, rendered apart, is the two together */
    {
        static float a[48000], b[48000];
        for (int part = 0; part < 3; part++) {
            fresh();
            beat_pos = 0.0f;
            setp(MICRO_P_SUSTAIN, 1.0f);
            set_pulse(0, 1.0f, 3.0f, 16.0f, 46261.0f, 0.0f, 1.0f);
            setp(MICRO_PU(0, MICRO_PU_OPEN_NOT), 4.0f);
            setp(MICRO_P_SINE_LEVEL, part == 1 ? -60.0f : 0.0f);
            setp(MICRO_P_NOISE_LEVEL, part == 0 ? -60.0f : 0.0f);
            note_at(81, 0);
            run(part == 0 ? a : part == 1 ? b : L, R, 48000, 120.0f);
        }
        float d = 0.0f;
        for (int i = 0; i < 48000; i++)
            d = fmaxf(d, fabsf(L[i] - (a[i] + b[i])));
        CHECK(d < 1e-5f, "micro open / open_not not complementary: %g", (double)d);
    }

    /* step envelope: every step opens a 10 ms tick of noise; loud at its start, gone well before the next one */
    fresh();
    beat_pos = 0.0f;
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 65535.0f, 0.0f, 4.0f); /* every step opens the noise */
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

    /* two low sines 4 Hz apart, equal: the level falls to nothing 0.125 s after the hit and is back at 0.25 s */
    fresh();
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    setp(MICRO_P_LOW_FREQ, 100.0f);
    setp(MICRO_P_LOW_HOLD, 2.0f);
    setp(MICRO_P_LOW_BEAT, 4.0f);
    setp(MICRO_P_LOW_BEAT_MIX, 0.5f);
    note_at(60, 0);
    run(L, R, 48000, 120.0f);
    {
        const float node = rms(L + 6000 - 240, 480), full = rms(L + 12000 - 240, 480);
        CHECK(full > 0.02f && node < 0.08f * full, "micro low beating at 4 Hz: rms %g at 0.125 s, %g at 0.25 s", (double)node,
              (double)full);
    }

    /* hit by a pulse (one step in four of sixteenths at 120 bpm): a hit every 0.5 s while the note is held */
    fresh();
    beat_pos = 0.0f;
    setp(MICRO_P_SUSTAIN, 1.0f);
    setp(MICRO_P_SINE_LEVEL, -60.0f);
    setp(MICRO_P_LOW_LEVEL, 0.0f);
    set_pulse(0, 1.0f, 3.0f, 16.0f, 4369.0f, 0.0f, 32.0f);
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
        CHECK(head > 0.02f && tail < 1e-5f, "micro low on a pulse: rms %g on its steps, %g between", (double)head, (double)tail);
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
