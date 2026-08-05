/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Minimal test scaffolding.  No framework, no dependencies: a check macro,
 *  a failure counter, and an exit status.  Same shape as the ViewFS unit
 *  tests.
 *
 *  Usage:
 *
 *      #include "st_test.h"
 *
 *      int main(void)
 *      {
 *          ST_TEST_BEGIN("port layer");
 *          CHECK(1 + 1 == 2);
 *          CHECK_EQ_INT(ST_cpu_count() >= 1, 1);
 *          return ST_TEST_END();
 *      }
 */

#ifndef ST_TEST_H
#define ST_TEST_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/*
 *  Unused when a suite compiles to its skip stub, which happens whenever a
 *  test applies to only one object memory.
 */
#if defined(__GNUC__) || defined(__clang__)
#define ST_TEST_MAYBE_UNUSED    __attribute__((unused))
#else
#define ST_TEST_MAYBE_UNUSED
#endif

static int      st_test_checks    ST_TEST_MAYBE_UNUSED;
static int      st_test_failures  ST_TEST_MAYBE_UNUSED;

#define ST_TEST_BEGIN(name)                                             \
    do {                                                                \
        st_test_checks   = 0;                                           \
        st_test_failures = 0;                                           \
        printf("---- %s ----\n", (name));                               \
    } while (0)

#define ST_TEST_END()                                                   \
    (printf("%s: %d checks, %d failure%s\n",                            \
            st_test_failures ? "FAIL" : "ok",                           \
            st_test_checks, st_test_failures,                           \
            st_test_failures == 1 ? "" : "s"),                          \
     st_test_failures ? 1 : 0)

#define CHECK(cond)                                                     \
    do {                                                                \
        ++st_test_checks;                                               \
        if (!(cond)) {                                                  \
            ++st_test_failures;                                         \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                               \
    } while (0)

#define CHECK_EQ_INT(got, want)                                         \
    do {                                                                \
        long long g_ = (long long) (got);                               \
        long long w_ = (long long) (want);                              \
                                                                        \
        ++st_test_checks;                                               \
        if (g_ != w_) {                                                 \
            ++st_test_failures;                                         \
            printf("  FAIL %s:%d: %s == %s (got %lld, want %lld)\n",    \
                   __FILE__, __LINE__, #got, #want, g_, w_);            \
        }                                                               \
    } while (0)

#define CHECK_EQ_STR(got, want)                                         \
    do {                                                                \
        const char *g_ = (got);                                         \
        const char *w_ = (want);                                        \
                                                                        \
        ++st_test_checks;                                               \
        if (!g_ || !w_ || strcmp(g_, w_) != 0) {                        \
            ++st_test_failures;                                         \
            printf("  FAIL %s:%d: %s == %s (got \"%s\", want \"%s\")\n",\
                   __FILE__, __LINE__, #got, #want,                     \
                   g_ ? g_ : "(null)", w_ ? w_ : "(null)");             \
        }                                                               \
    } while (0)

#endif  /*  ST_TEST_H  */
