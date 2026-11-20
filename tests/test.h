/* test.h — minimal test framework (no dependencies) and shared analysis helpers:
 * FFT, spectrum, frequency estimation, NaN/bounds checks. */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern int t_checks, t_fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        t_checks++;                                                          \
        if (!(cond)) {                                                       \
            t_fails++;                                                       \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

/* Suites */
#define TEST_SUITE(name) void suite_##name(void);
#include "suites.h"
#undef TEST_SUITE
void run_bench(void);

/* ---- helpers (fft.c) ---- */
/* Power spectrum in dB (4-term Blackman-Harris window), n a power of 2.
 * db must hold n/2+1 values. */
void spectrum_db(const float *x, int n, float *db);
/* Fundamental frequency from (interpolated) rising zero crossings, after removing the mean. */
float estimate_freq(const float *x, int n, float sr);
/* 1 if all samples are finite and |x| <= bound. */
int all_finite_bounded(const float *x, int n, float bound);
float max_abs(const float *x, int n);
float rms(const float *x, int n);

#endif /* TEST_H */
