/* wavetable.c — table building (FFT band-limiting per octave) and playback (see wavetable.h). */
#include "wavetable.h"
#include "../core/fastmath.h"
#include "../core/fft.h"

/* level k keeps harmonics 1..512>>k. Sizes give >= 16 samples per period of the top harmonic from level 2
 * on and >= 32 from level 5: the images of the linear interpolation stay below -75 dB in the audible band. */
static const int lvl_size[WT_LEVELS] = {2048, 2048, 2048, 1024, 512, 512, 512, 512, 512, 512};
static const int lvl_offset[WT_LEVELS] = {0, 2049, 4098, 6147, 7172, 7685, 8198, 8711, 9224, 9737};
_Static_assert(9737 + 513 == WT_FRAME_FLOATS, "mip layout");

void wt_build(float *table, wt_gen_fn gen, float *scratch)
{
    float *re = scratch, *im = scratch + WT_GEN_SIZE;
    float *re2 = scratch + 2 * WT_GEN_SIZE, *im2 = re2 + 2048;
    for (int f = 0; f < WT_FRAMES; f++) {
        float *frame = table + f * WT_FRAME_FLOATS;
        gen((float)f / (float)(WT_FRAMES - 1), re, WT_GEN_SIZE);
        for (int i = 0; i < WT_GEN_SIZE; i++)
            im[i] = 0.0f;
        fft_complex(re, im, WT_GEN_SIZE, 0);
        float peak = 0.0f;
        for (int k = 0; k < WT_LEVELS; k++) {
            const int n = lvl_size[k], h_max = 512 >> k;
            for (int i = 0; i < n; i++)
                re2[i] = im2[i] = 0.0f;
            for (int h = 1; h <= h_max; h++) { /* keep harmonics 1..h_max (and their mirror) */
                re2[h] = re[h], im2[h] = im[h];
                re2[n - h] = re[h], im2[n - h] = -im[h];
            }
            fft_complex(re2, im2, n, 1);
            float *dst = frame + lvl_offset[k];
            for (int i = 0; i < n; i++) {
                dst[i] = re2[i] * (1.0f / (float)WT_GEN_SIZE);
                if (k == 0)
                    peak = dsp_maxf(peak, dsp_absf(dst[i]));
            }
            dst[n] = dst[0]; /* guard sample for the interpolation */
        }
        const float norm = peak > 1e-9f ? 1.0f / peak : 0.0f; /* every frame peaks at 1 (full band) */
        for (int i = 0; i < WT_FRAME_FLOATS; i++)
            frame[i] *= norm;
    }
}

void wt_osc_reset(wt_osc_t *o, uint32_t *rng)
{
    for (int u = 0; u < WT_UNISON_MAX; u++) {
        o->phase[u] = u ? dsp_rand_unipolar(rng) : 0.0f;
        o->yprev[u] = 0.0f;
    }
    o->fresh = 1;
}

/* value at phase ph of the frame pair (a, b) mixed by ff, at a mip level of size n */
static inline float wt_read(const float *a, const float *b, int n, float ph, float ff)
{
    const float x = ph * (float)n;
    int i = (int)x;
    const float fr = x - (float)i;
    if (i >= n)
        i -= n;
    const float va = a[i] + fr * (a[i + 1] - a[i]);
    const float vb = b[i] + fr * (b[i + 1] - b[i]);
    return va + ff * (vb - va);
}

DSP_FAST_CODE void wt_osc_process(wt_osc_t *o, const float *table, const wt_osc_set_t *s, float inv_sr, const float *mod,
                                  float *out, int n)
{
    const int units = dsp_clampi(s->unison, 1, WT_UNISON_MAX);
    const int mode = s->warp_mode;
    const float w = dsp_clampf(s->warp, 0.0f, 1.0f);
    const float pos1 = dsp_clampf(s->position, 0.0f, 1.0f), lvl1 = s->level;
    if (o->fresh) {
        o->pos = pos1, o->level = lvl1, o->fresh = 0;
    }

    /* warp constants and the frequency factor it implies for the mip choice */
    const float ratio = 1.0f + 7.0f * w;              /* sync: slave runs 1..8 x faster */
    const float knee = 1.0f - 0.5f * w;               /* mirror: forward up to knee, back after */
    const float inv_knee = 1.0f / knee, inv_back = w > 0.0f ? 1.0f / (1.0f - knee) : 0.0f;
    const float bend = 0.155f * w;                    /* < 1/(2 pi): monotonic phase map */
    float factor = 1.0f;
    switch (mode) {
    case WT_WARP_SYNC: factor = ratio; break;
    case WT_WARP_MIRROR: factor = inv_knee; break;
    case WT_WARP_BEND: factor = 1.0f + w; break;
    case WT_WARP_FM: factor = 1.0f + 2.0f * w; break;
    default: break;
    }
    const float f_hi = s->freq_hz * fm_exp2(s->detune_cents * (1.0f / 1200.0f)) * factor;
    const float lx = fm_log2(dsp_maxf(512.0f * f_hi * inv_sr * (1.0f / 0.55f), 1.0f));
    int level = (int)lx;
    if ((float)level < lx)
        level++;
    level = dsp_clampi(level, 0, WT_LEVELS - 1);
    const int size = lvl_size[level];

    const float gain_norm = 1.0f / (units == 1 ? 1.0f : fm_exp2(0.5f * fm_log2((float)units))); /* 1/sqrt(n) */
    const float inv_n = 1.0f / (float)n;
    const float dpos = (pos1 - o->pos) * inv_n, dlvl = (lvl1 - o->level) * inv_n;

    for (int u = 0; u < units; u++) {
        const float spread = units == 1 ? 0.0f : -1.0f + 2.0f * (float)u / (float)(units - 1);
        const float dt = dsp_minf(s->freq_hz * fm_exp2(spread * s->detune_cents * (1.0f / 1200.0f)) * inv_sr, 0.45f);
        const float inv_dt = 1.0f / dt;
        float ph = o->phase[u], yprev = o->yprev[u], pos = o->pos, lvl = o->level;
        for (int i = 0; i < n; i++) {
            pos += dpos, lvl += dlvl;
            const float fpos = pos * (float)(WT_FRAMES - 1);
            int f0 = (int)fpos;
            if (f0 > WT_FRAMES - 2)
                f0 = WT_FRAMES - 2;
            const float ff = fpos - (float)f0;
            const float *a = table + f0 * WT_FRAME_FLOATS + lvl_offset[level];
            const float *b = a + WT_FRAME_FLOATS;

            ph += dt;
            int wrapped = 0;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                wrapped = 1;
            }
            float p = ph, corr = 0.0f;
            switch (mode) {
            case WT_WARP_SYNC: {
                p = fm_frac(ph * ratio);
                if (wrapped) { /* slave reset: jump from the end of its cycle to its start, polyBLEP */
                    const float h = wt_read(a, b, size, 0.0f, ff) - wt_read(a, b, size, fm_frac(ratio), ff);
                    const float d = dsp_minf(ph * inv_dt, 1.0f), r = 1.0f - d;
                    yprev += h * 0.5f * d * d;
                    corr = -h * 0.5f * r * r;
                }
                break;
            }
            case WT_WARP_BEND: p = fm_frac(ph - bend * fm_sin(ph)); break;
            case WT_WARP_MIRROR: p = ph < knee ? ph * inv_knee : (1.0f - ph) * inv_back; break;
            case WT_WARP_FM: p = fm_frac(ph + w * mod[i]); break;
            default: break;
            }
            float y = wt_read(a, b, size, p, ff);
            if (mode == WT_WARP_RING)
                y *= 1.0f - w + w * mod[i];
            out[i] += yprev * lvl * gain_norm;
            yprev = y + corr;
        }
        o->phase[u] = ph, o->yprev[u] = yprev;
    }
    o->pos = pos1, o->level = lvl1;
}
