/* ame-next — zero-dependency test framework (tests/).
 * Exit code 0 = pass, 1 = fail; ctest-friendly; no allocation. */
#ifndef AME_UTEST_H
#define AME_UTEST_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ut_failures = 0;
static int ut_checks = 0;
static const char *ut_case = "";

#define UT_CASE(name) do { ut_case = name; \
    printf("  case %-40s", name); } while (0)

#define UT_OK() do { printf("ok\n"); } while (0)

#define UT_ASSERT(cond) do { \
    ut_checks++; \
    if (!(cond)) { \
        ut_failures++; \
        printf("FAIL\n    %s:%d: assert failed: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } } while (0)

#define UT_ASSERTF(cond, ...) do { \
    ut_checks++; \
    if (!(cond)) { \
        ut_failures++; \
        printf("FAIL\n    %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        return 1; \
    } } while (0)

#define UT_ASSERT_NEAR(a, b, eps) \
    UT_ASSERTF(fabsf((float)(a) - (float)(b)) <= (eps), \
               "%s (%.6g) != %s (%.6g) within %g", #a, (double)(a), #b, (double)(b), (double)(eps))

static inline int ut_done(const char *suite) {
    if (ut_failures) {
        printf("== %s: %d FAILED check(s), %d total ==\n", suite, ut_failures, ut_checks);
        return 1;
    }
    printf("== %s: all %d checks passed ==\n", suite, ut_checks);
    return 0;
}

#endif /* AME_UTEST_H */
