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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/*
 *  A suite that applies to only one object memory compiles to a skip stub
 *  under the other, leaving these unused.
 */
#if defined(__GNUC__) || defined(__clang__)
#define ST_TEST_UNUSED      __attribute__((unused))
#else
#define ST_TEST_UNUSED
#endif

static int      st_test_checks    ST_TEST_UNUSED;
static int      st_test_failures  ST_TEST_UNUSED;

/*
 *  strdup is POSIX rather than C, and MSVC spells it with an underscore
 *  while warning about the plain name.  Tests use this instead.
 */
ST_TEST_UNUSED static char *
st_test_strdup(const char *s)
{
    size_t  n = strlen(s) + 1;
    char   *copy = (char *) malloc(n);

    if (copy)
        memcpy(copy, s, n);
    return copy;
}

/*
 *  The st2026 binary and the scratch directory of THIS build, for a test
 *  that runs the binary or writes fixtures.  The Makefile exports both to
 *  every unit test; run by hand, the default build's are assumed.
 *
 *  Tests named build/mt/st2026 and ./st2026 themselves, so a TSAN or ASAN
 *  run tested the uninstrumented binary, a HEADLESS tree skipped and said
 *  ok, and after `make OM=bb' test_image ran a Blue Book ./st2026 (Bugs5
 *  DOCS-4).
 */
ST_TEST_UNUSED static const char *
st_test_binary(void)
{
    const char *bin = getenv("ST2026_BIN");

    return (bin && *bin) ? bin : "build/mt/st2026";
}

ST_TEST_UNUSED static const char *
st_test_dir(void)
{
    const char *dir = getenv("ST_TEST_DIR");

    return (dir && *dir) ? dir : "build/mt/tests";
}

/*
 *  This process's id, for naming a fixture no other run will choose.
 */
#ifdef _WIN32
#include <process.h>
#define st_test_getpid_()   _getpid()
#else
#include <unistd.h>
#define st_test_getpid_()   getpid()
#endif

ST_TEST_UNUSED static long
st_test_pid(void)
{
    return (long) st_test_getpid_();
}

/*
 *  A scratch file of this RUN: "<st_test_dir>/<pid>-<name>".
 *
 *  Fixtures had fixed names -- build/test-round-trip.image, /tmp/st2026-
 *  prim-<name>, the serve-faults batch file -- so two runs from one tree at
 *  once wrote each other's files: one test's image was another's half-
 *  written one, and the batch a server read was the other run's batch
 *  (Bugs5 DOCS-6).  The /tmp ones were also opened for writing at a name
 *  anyone on the machine could have put a symbolic link at first.  The pid
 *  separates runs; the build's own test directory, which is not shared with
 *  other users and not with the other variants, keeps them out of /tmp.
 *  The caller removes the file, as before.  The answer lasts the run: it
 *  is kept in a table here, one entry per name, so that the address
 *  sanitizer sees it reachable rather than leaked -- a bare malloc that
 *  was never freed failed every ASAN run of the suites that use this.
 */
ST_TEST_UNUSED static const char *
st_test_path(const char *name)
{
    static struct { const char *name; char *path; } made[64];
    size_t  i;
    size_t  n;

    for (i = 0; i < sizeof made / sizeof made[0] && made[i].name; ++i)
        if (strcmp(made[i].name, name) == 0)
            return made[i].path;
    if (i == sizeof made / sizeof made[0])
        return name;
    n = strlen(st_test_dir()) + strlen(name) + 32;
    made[i].path = (char *) malloc(n);
    if (!made[i].path)
        return name;
    snprintf(made[i].path, n, "%s/%ld-%s", st_test_dir(), st_test_pid(), name);
    made[i].name = name;
    return made[i].path;
}

/*
 *  The portable `timeout', for a test that shells out to a command it must
 *  be able to kill.  Stock macOS has no timeout(1) (Bugs5 DOCS-8); the
 *  script uses timeout or gtimeout when there is one and a watchdog of its
 *  own when there is not.  Paste it where `timeout' went: the arguments are
 *  the same, -k included.  Relative, like every fixture path here: the
 *  suites run from the top of the tree.
 */
#define ST_TEST_TIMEOUT     "sh tools/timeout.sh"

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
