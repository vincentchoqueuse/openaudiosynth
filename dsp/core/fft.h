/* fft.h — in-place complex radix-2 FFT for init-time work (band-limiting wavetables). Not for the
 * audio path. Twiddles come from the fastmath sine table (no libm), error ~1e-6.
 * Reference: Cooley & Tukey (1965), iterative decimation in time with bit-reversal permutation. */
#ifndef FFT_H
#define FFT_H

/* n: power of two. inverse = 1 computes the unscaled inverse transform (divide by n yourself). */
void fft_complex(float *re, float *im, int n, int inverse);

#endif /* FFT_H */
