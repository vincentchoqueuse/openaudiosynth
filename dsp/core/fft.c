/* fft.c — iterative radix-2 FFT (see fft.h). */
#include "fft.h"
#include "fastmath.h"

void fft_complex(float *re, float *im, int n, int inverse)
{
    for (int i = 1, j = 0; i < n; i++) { /* bit-reversal permutation */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i];
            re[i] = re[j], re[j] = t;
            t = im[i];
            im[i] = im[j], im[j] = t;
        }
    }
    const float sign = inverse ? 1.0f : -1.0f;
    for (int len = 2; len <= n; len <<= 1) {
        const float step = 1.0f / (float)len; /* twiddle phase step, in cycles */
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; k++) {
                const float wr = fm_cos((float)k * step), wi = sign * fm_sin((float)k * step);
                const int a = i + k, b = a + len / 2;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr, im[b] = im[a] - xi;
                re[a] += xr, im[a] += xi;
            }
        }
    }
}
