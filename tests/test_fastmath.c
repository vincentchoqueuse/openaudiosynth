/* test_fastmath.c — accuracy of fastmath.h against libm (double reference here, test code). */
#include <math.h>

#include "core/fastmath.h"
#include "test.h"

void suite_fastmath(void)
{
    double e;

    e = 0.0;
    for (int i = -20000; i <= 20000; i++) {
        const double ph = (double)i * 0.000173;
        e = fmax(e, fabs((double)fm_sin((float)ph) - sin(2.0 * M_PI * (double)(float)ph)));
    }
    CHECK(e < 2e-6, "fm_sin: max error %.3g", e);

    e = 0.0;
    for (int i = -4000; i <= 4000; i++) {
        const double x = (double)i * 0.01;
        e = fmax(e, fabs((double)fm_exp2((float)x) / exp2((double)(float)x) - 1.0));
    }
    CHECK(e < 1e-6, "fm_exp2: max relative error %.3g", e);

    e = 0.0;
    for (int i = -60; i <= 60; i++) {
        for (int k = 0; k < 50; k++) {
            const float x = (float)(pow(10.0, (double)i * 0.1) * (1.0 + 0.02 * k));
            e = fmax(e, fabs((double)fm_log2(x) - log2((double)x)));
        }
    }
    CHECK(e < 2e-6, "fm_log2: max absolute error %.3g", e); /* 1 ulp at |log2| = 20 is 1.9e-6 */

    CHECK(fabs((double)fm_mtof(69.0f) - 440.0) < 1e-3, "mtof(69) = %f", (double)fm_mtof(69.0f));
    CHECK(fabs((double)fm_mtof(60.0f) - 261.625565) < 1e-3, "mtof(60) = %f", (double)fm_mtof(60.0f));
    CHECK(fabs((double)fm_db2lin(-6.0206f) - 0.5) < 1e-4, "db2lin(-6) = %f", (double)fm_db2lin(-6.0206f));
    CHECK(fabs((double)fm_lin2db(0.1f) + 20.0) < 1e-4, "lin2db(0.1) = %f", (double)fm_lin2db(0.1f));

    e = 0.0;
    for (int i = 1; i <= 450; i++) {
        const double x = (double)i * 0.001;
        e = fmax(e, fabs((double)fm_tan_pi((float)x) / tan(M_PI * (double)(float)x) - 1.0));
    }
    CHECK(e < 1e-4, "fm_tan_pi: max relative error %.3g", e);

    e = 0.0;
    int mono = 1;
    float prev = -2.0f;
    for (int i = -1000; i <= 1000; i++) {
        const float x = (float)i * 0.005f;
        const float y = fm_tanh(x);
        e = fmax(e, fabs((double)y - tanh((double)x)));
        mono &= (y >= prev) && fabsf(y) <= 1.0f;
        prev = y;
    }
    CHECK(e < 0.025, "fm_tanh: max error %.3g", e);
    CHECK(mono, "fm_tanh not monotonic or outside [-1, 1]");
}
