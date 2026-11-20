/* test_main.c — entry point: `synth_tests [suite]` runs one suite (or all),
 * `synth_tests --bench` measures the cost in ns/voice/sample. */
#include <stdio.h>
#include <string.h>

#include "test.h"

int t_checks = 0, t_fails = 0;

static const struct {
    const char *name;
    void (*fn)(void);
} suites[] = {
#define TEST_SUITE(name) {#name, suite_##name},
#include "suites.h"
#undef TEST_SUITE
};

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        run_bench();
        return 0;
    }
    int ran = 0;
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
        if (argc > 1 && strcmp(argv[1], suites[i].name) != 0)
            continue;
        printf("[%s]\n", suites[i].name);
        const int before = t_fails;
        suites[i].fn();
        printf("  %s\n", t_fails == before ? "ok" : "FAILURES");
        ran++;
    }
    if (!ran) {
        printf("unknown suite: %s\n", argv[1]);
        return 2;
    }
    printf("%d checks, %d failure(s)\n", t_checks, t_fails);
    return t_fails ? 1 : 0;
}
