/* selfosc.c — Van der Pol / Duffing oscillator (see selfosc.h). One step of length h (scaled time), a symmetric
 * (Strang) split, hence second order; the same scheme computes the pitch table and the audio:
 *   v *= exp(h/2 mu (1 - x^2))      half of the (anti-)damping, solved exactly: stable for any mu h
 *   leapfrog for the spring a(x) = -x - beta x^3 + F:  v += h/2 a(x);  x += h v;  v += h/2 a(x)
 *   v *= exp(h/2 mu (1 - x^2))      the other half, at the new position */
#include "selfosc.h"
#include "../core/fastmath.h"

static inline void step(float *x, float *v, float h, float mu, float beta, float force)
{
    *v *= fm_exp2(dsp_clampf(0.5f * FM_LOG2E * h * mu * (1.0f - *x * *x), -20.0f, 4.0f));
    *v += 0.5f * h * (-*x - beta * *x * *x * *x + force);
    *x = dsp_clampf(*x + h * *v, -4.0f, 4.0f);
    *v += 0.5f * h * (-*x - beta * *x * *x * *x + force);
    *v *= fm_exp2(dsp_clampf(0.5f * FM_LOG2E * h * mu * (1.0f - *x * *x), -20.0f, 4.0f));
}

const selfosc_table_t selfosc_table = {{
    1.00054979f, 0.996663392f, 0.985262632f, 0.967121124f, 0.943475604f,
    0.915870249f, 0.885896802f, 0.854922891f, 0.823954105f, 0.793640673f,
    0.764364839f, 0.736331224f, 0.709632695f, 0.684292436f, 0.660294116f,
    0.637593567f, 0.616136074f, 0.595853508f, 0.576681137f, 0.558551013f,
    0.541396916f,
}};

void selfosc_table_init(selfosc_table_t *t)
{
    for (int k = 0; k < SELFOSC_TABLE; k++) {
        const float mu = SELFOSC_MU_MAX * (float)k / (float)(SELFOSC_TABLE - 1);
        const float h = 0.002f;
        float x = 0.5f, v = 0.0f, last = -1.0f, first = -1.0f, tau = 0.0f;
        int crossings = 0;
        for (int i = 0; i < 60000; i++) { /* 120 units: 40 to settle, then count rising zero crossings */
            const float xp = x;
            step(&x, &v, h, mu, 0.0f, 0.0f);
            tau += h;
            if (tau > 40.0f && xp < 0.0f && x >= 0.0f) {
                const float tc = tau - h * x / (x - xp); /* interpolated crossing time */
                if (first < 0.0f)
                    first = tc;
                last = tc;
                crossings++;
            }
        }
        /* period in scaled time = (last - first) / (crossings - 1); free frequency / natural = 2 pi / period */
        t->ratio[k] = crossings > 1 ? FM_TWO_PI * (float)(crossings - 1) / (last - first) : 1.0f;
    }
}

float selfosc_ratio(const selfosc_table_t *t, float mu)
{
    const float x = dsp_clampf(mu, 0.0f, SELFOSC_MU_MAX) * (float)(SELFOSC_TABLE - 1) / SELFOSC_MU_MAX;
    int i = (int)x;
    if (i > SELFOSC_TABLE - 2)
        i = SELFOSC_TABLE - 2;
    const float f = x - (float)i;
    return t->ratio[i] + f * (t->ratio[i + 1] - t->ratio[i]);
}

void selfosc_reset(selfosc_t *s, float x0)
{
    s->x = x0;
    s->v = 0.0f;
    s->fph = 0.0f;
    hb2x_reset(&s->d1);
    hb2x_reset(&s->d2);
}

DSP_FAST_CODE void selfosc_process(selfosc_t *s, float sample_rate, float f_nat, float f_force, float mu, float beta,
                                   float force, float *out, int n)
{
    const float h = FM_TWO_PI * f_nat / (4.0f * sample_rate); /* scaled-time step at 4x */
    const float m = dsp_clampf(mu, 0.0f, SELFOSC_MU_MAX);
    const float dph = f_force / (4.0f * sample_rate);
    float x = s->x, v = s->v, ph = s->fph;
    for (int i = 0; i < n; i++) {
        float sub[4];
        for (int j = 0; j < 4; j++) {
            /* step() with the two half dampings of consecutive steps (same position) merged into one: the
             * half at the start of the block and at its end keep the scheme identical across blocks */
            const float a = force * fm_cos(ph);
            v *= fm_exp2(dsp_clampf((i == 0 && j == 0 ? 0.5f : 1.0f) * FM_LOG2E * h * m * (1.0f - x * x), -20.0f, 4.0f));
            v += 0.5f * h * (-x - beta * x * x * x + a);
            x = dsp_clampf(x + h * v, -4.0f, 4.0f);
            v += 0.5f * h * (-x - beta * x * x * x + a);
            ph += dph;
            if (ph >= 1.0f)
                ph -= 1.0f;
            sub[j] = 0.5f * x; /* the Van der Pol cycle swings about +/-2 */
        }
        const float a = hb2x_down(&s->d1, sub[0], sub[1]), b = hb2x_down(&s->d1, sub[2], sub[3]);
        out[i] = hb2x_down(&s->d2, a, b);
    }
    v *= fm_exp2(dsp_clampf(0.5f * FM_LOG2E * h * m * (1.0f - x * x), -20.0f, 4.0f)); /* closing half damping */
    s->x = x, s->v = v, s->fph = ph;
}
