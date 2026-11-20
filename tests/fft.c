/* fft.c — analysis tools for the tests: iterative radix-2 FFT (Cooley-Tukey), windowed spectrum,
 * frequency estimation. Test code: libm is allowed here. */
#include <math.h>
#include <stdlib.h>

#include "core/dsp.h"
#include "test.h"

static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) { /* bit-reversal permutation */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -6.28318530717959f / (float)len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; k++) {
                const float wr = cosf(ang * (float)k), wi = sinf(ang * (float)k);
                const int a = i + k, b = i + k + len / 2;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
            }
        }
    }
}

void spectrum_db(const float *x, int n, float *db)
{
    float *re = malloc(sizeof(float) * (size_t)n), *im = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) {
        const float t = 6.28318530717959f * (float)i / (float)n;
        const float w = 0.35875f - 0.48829f * cosf(t) + 0.14128f * cosf(2.0f * t) - 0.01168f * cosf(3.0f * t);
        re[i] = x[i] * w;
        im[i] = 0.0f;
    }
    fft(re, im, n);
    for (int k = 0; k <= n / 2; k++)
        db[k] = 10.0f * log10f(re[k] * re[k] + im[k] * im[k] + 1e-30f);
    free(re);
    free(im);
}

float estimate_freq(const float *x, int n, float sr)
{
    float mean = 0.0f;
    for (int i = 0; i < n; i++)
        mean += x[i];
    mean /= (float)n;
    float first = -1.0f, last = -1.0f;
    int count = 0;
    for (int i = 1; i < n; i++) {
        const float a = x[i - 1] - mean, b = x[i] - mean;
        if (a < 0.0f && b >= 0.0f) {
            const float t = (float)(i - 1) + a / (a - b);
            if (first < 0.0f)
                first = t;
            else
                count++;
            last = t;
        }
    }
    if (count < 1)
        return 0.0f;
    return sr * (float)count / (last - first);
}

int all_finite_bounded(const float *x, int n, float bound)
{
    for (int i = 0; i < n; i++)
        if (!dsp_isfinite(x[i]) || fabsf(x[i]) > bound)
            return 0;
    return 1;
}

float max_abs(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > m)
            m = fabsf(x[i]);
    return m;
}

float rms(const float *x, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++)
        s += x[i] * x[i];
    return sqrtf(s / (float)n);
}
