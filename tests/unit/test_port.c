/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Exercises the portability layer.  The interesting tests are the last
 *  two: they run real contention across every available core, which is the
 *  property the whole project depends on.
 */

#include "st_test.h"
#include "st_port.h"
#include "st_atomic.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define WORKERS         8
#define INCREMENTS      50000

/*  ----------  Threads start and join  ----------  */

static void
touch_worker(void *arg)
{
    int    *flag = (int *) arg;

    *flag = 1;
}

static void
test_thread_create_join(void)
{
    st_thread   t;
    int         flag = 0;

    CHECK_EQ_INT(ST_thread_create(&t, touch_worker, &flag), 0);
    CHECK_EQ_INT(ST_thread_join(t), 0);
    CHECK_EQ_INT(flag, 1);
}

/*  ----------  Mutual exclusion actually excludes  ----------  */

typedef struct {
    st_mutex   *lock;
    long       *counter;
} mutex_work;

static void
mutex_worker(void *arg)
{
    mutex_work *w = (mutex_work *) arg;
    int         i;

    for (i = 0; i < INCREMENTS; ++i) {
        ST_mutex_lock(w->lock);
        /*  Deliberately a non-atomic read-modify-write.  */
        *w->counter += 1;
        ST_mutex_unlock(w->lock);
    }
}

static void
test_mutex_excludes(void)
{
    st_thread   threads[WORKERS];
    mutex_work  work;
    st_mutex    lock;
    long        counter = 0;
    int         i;

    CHECK_EQ_INT(ST_mutex_init(&lock), 0);
    work.lock    = &lock;
    work.counter = &counter;
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_create(&threads[i], mutex_worker, &work), 0);
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_join(threads[i]), 0);
    CHECK_EQ_INT(counter, (long) WORKERS * INCREMENTS);
    ST_mutex_destroy(&lock);
}

static void
test_mutex_trylock(void)
{
    st_mutex    lock;

    CHECK_EQ_INT(ST_mutex_init(&lock), 0);
    CHECK_EQ_INT(ST_mutex_trylock(&lock), 0);
    ST_mutex_unlock(&lock);
    ST_mutex_destroy(&lock);
}

/*  ----------  Condition variables hand off  ----------  */

typedef struct {
    st_mutex    lock;
    st_cond     ready;
    int         value;
} handoff;

static void
handoff_worker(void *arg)
{
    handoff    *h = (handoff *) arg;

    ST_mutex_lock(&h->lock);
    h->value = 42;
    ST_cond_signal(&h->ready);
    ST_mutex_unlock(&h->lock);
}

static void
test_cond_handoff(void)
{
    st_thread   t;
    handoff     h;

    CHECK_EQ_INT(ST_mutex_init(&h.lock), 0);
    CHECK_EQ_INT(ST_cond_init(&h.ready), 0);
    h.value = 0;

    CHECK_EQ_INT(ST_thread_create(&t, handoff_worker, &h), 0);
    ST_mutex_lock(&h.lock);
    while (h.value == 0)
        ST_cond_wait(&h.ready, &h.lock);
    ST_mutex_unlock(&h.lock);
    CHECK_EQ_INT(ST_thread_join(t), 0);
    CHECK_EQ_INT(h.value, 42);

    ST_cond_destroy(&h.ready);
    ST_mutex_destroy(&h.lock);
}

static void
test_cond_timeout(void)
{
    st_mutex    lock;
    st_cond     never;

    CHECK_EQ_INT(ST_mutex_init(&lock), 0);
    CHECK_EQ_INT(ST_cond_init(&never), 0);
    ST_mutex_lock(&lock);
    /*  Nobody will ever signal this, so it must report the timeout.  */
    CHECK_EQ_INT(ST_cond_timedwait(&never, &lock, 10 * 1000 * 1000), -1);
    ST_mutex_unlock(&lock);
    ST_cond_destroy(&never);
    ST_mutex_destroy(&lock);
}

/*  ----------  Thread-local storage is per thread  ----------  */

static st_tls_key   tls_key;

/*
 *  The CHECK macros bump plain counters, so they may only be used from the
 *  main thread.  Workers record a verdict in their own slot and the main
 *  thread does the checking after the join.
 */
typedef struct {
    long    want;
    long    got;
    int     set_ok;
} tls_result;

static void
tls_worker(void *arg)
{
    tls_result *r = (tls_result *) arg;

    r->set_ok = ST_tls_set(tls_key, (void *) (intptr_t) r->want);
    ST_thread_yield();
    /*  Must still see our own value, not another thread's.  */
    r->got = (long) (intptr_t) ST_tls_get(tls_key);
}

static void
test_tls_is_per_thread(void)
{
    st_thread   threads[WORKERS];
    tls_result  results[WORKERS];
    long        i;

    CHECK_EQ_INT(ST_tls_create(&tls_key, NULL), 0);
    CHECK(ST_tls_get(tls_key) == NULL);
    for (i = 0; i < WORKERS; ++i) {
        results[i].want   = i + 1;
        results[i].got    = -1;
        results[i].set_ok = -1;
        CHECK_EQ_INT(ST_thread_create(&threads[i], tls_worker, &results[i]), 0);
    }
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_join(threads[i]), 0);
    for (i = 0; i < WORKERS; ++i) {
        CHECK_EQ_INT(results[i].set_ok, 0);
        CHECK_EQ_INT(results[i].got, results[i].want);
    }
    /*  The main thread's own slot must still be untouched.  */
    CHECK(ST_tls_get(tls_key) == NULL);
    ST_tls_delete(tls_key);
}

/*  ----------  Atomics really are atomic  ----------  */

static void
atomic_worker(void *arg)
{
    st_atomic_int  *counter = (st_atomic_int *) arg;
    int             i;

    for (i = 0; i < INCREMENTS; ++i)
        ST_fetch_add_relaxed(counter, 1);
}

static void
test_atomic_counter(void)
{
    st_thread       threads[WORKERS];
    st_atomic_int   counter;
    int             i;

    ST_store_relaxed(&counter, 0);
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_create(&threads[i], atomic_worker, &counter), 0);
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_join(threads[i]), 0);
    CHECK_EQ_INT(ST_load_seq(&counter), WORKERS * INCREMENTS);
}

/*
 *  The compare-and-swap retry loop, which is the shape the object table's
 *  become: and the scheduler's ready queues will both use.  Each worker
 *  claims tickets until the pool is empty; every ticket must go to exactly
 *  one worker.
 */

typedef struct {
    st_atomic_int   remaining;
    st_atomic_int   claimed;
} ticket_pool;

static void
ticket_worker(void *arg)
{
    ticket_pool    *pool = (ticket_pool *) arg;

    for (;;) {
        int     old = ST_load_relaxed(&pool->remaining);

        if (old <= 0)
            break;
        if (ST_cas_weak(&pool->remaining, &old, old - 1))
            ST_fetch_add_relaxed(&pool->claimed, 1);
    }
}

static void
test_cas_loop(void)
{
    st_thread       threads[WORKERS];
    ticket_pool     pool;
    int             i;
    const int       total = 20000;

    ST_store_relaxed(&pool.remaining, total);
    ST_store_relaxed(&pool.claimed, 0);
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_create(&threads[i], ticket_worker, &pool), 0);
    for (i = 0; i < WORKERS; ++i)
        CHECK_EQ_INT(ST_thread_join(threads[i]), 0);
    CHECK_EQ_INT(ST_load_seq(&pool.remaining), 0);
    CHECK_EQ_INT(ST_load_seq(&pool.claimed), total);
}

/*  ----------  Time and CPU count  ----------  */

static void
test_cpu_count(void)
{
    int     n = ST_cpu_count();

    CHECK(n >= 1);
    printf("  (this machine reports %d CPUs)\n", n);
}

static void
test_time(void)
{
    int64_t start = ST_time_monotonic_ns();
    int64_t stamp;
    int64_t elapsed;

    ST_sleep_ns(5 * 1000 * 1000);
    elapsed = ST_time_monotonic_ns() - start;
    CHECK(elapsed > 0);
    CHECK(elapsed >= 4 * 1000 * 1000);

    /*
     *  The Smalltalk clock counts from 1901.  Sanity-check it against the
     *  range a run of this program can plausibly occupy: some time after
     *  2020 and well before 2100.
     */
    stamp = ST_time_smalltalk_ms();
    CHECK(stamp > INT64_C(3755289600000));      /*  1 Jan 2020  */
    CHECK(stamp < INT64_C(6279811200000));      /*  1 Jan 2100  */
}

/*  ----------  Bugs07 round three, the library group: KERNA-3, FILES-7  ----------  */

/*
 *  KERNA-3: primitive 219 on Windows answered the zone's standard offset
 *  all summer.  Its arithmetic is ST_utc_offset_between now, which is
 *  portable, so it is checked here against tm_gmtoff -- the answer the
 *  other platforms give -- every three hours for two years and a day, in
 *  zones with daylight saving north and south, with half- and
 *  three-quarter-hour offsets, and on both sides of the date line, where
 *  the local date is a day (and at New Year a year) away from UTC's.  The
 *  audit's three instants are among them; the old formula answered
 *  -18000, 0 and 36000 there for -14400, 3600 and 39600.  A host without
 *  a zone's data reads it as UTC, which still agrees, so the check cannot
 *  fail for want of tzdata; how many instants were in daylight saving is
 *  printed to show what was covered.  Windows reads TZ in its own form, so
 *  there the check is of the one zone the machine is in.
 */
static void
test_bugs7c_lib(void)
{
    long    offset = 0;

    CHECK(ST_local_utc_offset(&offset));
    CHECK(offset >= -12L * 3600 && offset <= 14L * 3600);
    CHECK(offset % 900 == 0);
#ifdef ST_POSIX
    {
        static const char *const zones[] = {
            "America/New_York", "Europe/London", "Australia/Sydney",
            "Asia/Kolkata", "America/St_Johns", "Asia/Kathmandu",
            "Pacific/Chatham", "Pacific/Kiritimati", "Pacific/Pago_Pago",
            "America/Santiago", "UTC",
        };
        static const time_t audit[] = { 1783000000, 1767225600 };
        const char *was = getenv("TZ");
        char       *saved = was ? strdup(was) : NULL;
        long        compared = 0;
        long        in_dst = 0;
        long        wrong = 0;
        size_t      z;

        for (z = 0; z < sizeof zones / sizeof zones[0]; z++) {
            time_t  t;
            size_t  a;

            setenv("TZ", zones[z], 1);
            tzset();
            for (t = 1767225600; t <= 1767225600 + (2 * 365 + 1) * 86400L; t += 3 * 3600) {
                struct tm   local;
                struct tm   utc;

                localtime_r(&t, &local);
                gmtime_r(&t, &utc);
                ++compared;
                if (local.tm_isdst > 0)
                    ++in_dst;
                if (ST_utc_offset_between(&local, &utc) != (long) local.tm_gmtoff) {
                    if (wrong++ < 5)
                        printf("  FAIL KERNA-3 %s at %lld: %ld, tm_gmtoff %ld\n",
                               zones[z], (long long) t,
                               ST_utc_offset_between(&local, &utc),
                               (long) local.tm_gmtoff);
                }
            }
            for (a = 0; a < sizeof audit / sizeof audit[0]; a++) {
                struct tm   local;
                struct tm   utc;

                localtime_r(&audit[a], &local);
                gmtime_r(&audit[a], &utc);
                ++compared;
                if (ST_utc_offset_between(&local, &utc) != (long) local.tm_gmtoff)
                    ++wrong;
            }
            CHECK(ST_local_utc_offset(&offset));
        }
        if (saved) {
            setenv("TZ", saved, 1);
            free(saved);
        } else
            unsetenv("TZ");
        tzset();
        CHECK(wrong == 0);
        printf("  (KERNA-3: %ld instants in %d zones, %ld of them in daylight saving)\n",
               compared, (int) (sizeof zones / sizeof zones[0]), in_dst);
    }
#endif
}

/*
 *  FILES-7: Windows' ST_sleep_ns truncated to whole milliseconds, so every
 *  sleep the scheduler takes -- all of them under a millisecond -- was
 *  Sleep(0), and an idle worker spun a core.  Its two roundings are
 *  portable and checked here: up, never to nothing, never to Sleep's
 *  INFINITE.  Then the promise itself, on whatever this is: fifty sleeps
 *  of the scheduler's 0.1 ms idle slice take at least five milliseconds.
 *  On Windows before the fix they took next to nothing.
 */
static void
test_bugs7c_lib_sleep(void)
{
    int64_t start;
    int     i;

    CHECK(ST_sleep_ms_rounded_up(0) == 0);
    CHECK(ST_sleep_ms_rounded_up(-5) == 0);
    CHECK(ST_sleep_ms_rounded_up(1) == 1);
    CHECK(ST_sleep_ms_rounded_up(1000) == 1);
    CHECK(ST_sleep_ms_rounded_up(100000) == 1);
    CHECK(ST_sleep_ms_rounded_up(1000000) == 1);
    CHECK(ST_sleep_ms_rounded_up(1000001) == 2);
    CHECK(ST_sleep_ms_rounded_up(INT64_C(30000000000)) == 30000);
    CHECK(ST_sleep_ms_rounded_up(INT64_MAX) == UINT32_C(0xFFFFFFFE));
    CHECK(ST_sleep_100ns_ticks(0) == 0);
    CHECK(ST_sleep_100ns_ticks(-1) == 0);
    CHECK(ST_sleep_100ns_ticks(1) == 1);
    CHECK(ST_sleep_100ns_ticks(1000) == 10);
    CHECK(ST_sleep_100ns_ticks(100000) == 1000);
    CHECK(ST_sleep_100ns_ticks(100001) == 1001);
    start = ST_time_monotonic_ns();
    for (i = 0; i < 50; i++)
        ST_sleep_ns(100000);
    CHECK(ST_time_monotonic_ns() - start >= 50 * INT64_C(100000));
}

/*
 *  FILES-11: on Windows a second st2026 on an image another one has open was
 *  admitted, because the changes-file lock was POSIX-only; it is now
 *  ST_file_lock_for_life on `<changes file>.lock' there (src/main.c).  The
 *  Windows half cannot run here; its contract can, through the POSIX half:
 *  the first taker holds, every later one -- this process's own second
 *  descriptor included, which on Windows is a second handle -- is refused
 *  with 0, and a file that cannot be made is -1 rather than a refusal, which
 *  main.c lets through to fail where the file is needed.  The lock is held
 *  for the rest of the run, as it is for the life of st2026.
 */
static void
test_bugs7e_files(void)
{
    const char *path = st_test_path("bugs7e-files11.lock");
    char        nowhere[1024];

    remove(path);
    CHECK_EQ_INT(ST_file_lock_for_life(path), 1);
    CHECK_EQ_INT(ST_file_lock_for_life(path), 0);
    CHECK_EQ_INT(ST_file_lock_for_life(path), 0);
    snprintf(nowhere, sizeof nowhere, "%s.no-such-directory/x.lock", path);
    CHECK_EQ_INT(ST_file_lock_for_life(nowhere), -1);
    remove(path);
}

/*
 *  FILES-9: a wall clock stepped back stalled every Delay by the size of
 *  the step.  The delay timer measures what is left of a wait on the
 *  monotonic clock and hands it to ST_cond_timedwait, which turned it
 *  into an absolute deadline on CLOCK_REALTIME for a condition variable
 *  made with the default (real-time) attribute -- so a step back during
 *  the wait moved the deadline away by the step.
 *
 *  The machine's clock is not this test's to change, so the step is
 *  simulated: on Linux this binary carries its own clock_gettime, in front
 *  of the C library's, which while `realtime_skew_ns' is set reads
 *  CLOCK_REALTIME that far AHEAD of the kernel's.  That is exactly the
 *  state of a process just after its wall clock was stepped back by the
 *  skew: a deadline worked out on the wall clock is that much further off
 *  than the clock the kernel compares it with.  Every other clock, and
 *  every read while the skew is zero, goes straight to the kernel.  The
 *  first check makes sure the simulation reaches st_port.c at all -- its
 *  wall-clock reading must jump by the skew -- so the second cannot pass
 *  for want of a skew.  Before the fix the 100 ms wait took the skew too,
 *  five seconds and a hundred milliseconds; now the wall clock is not
 *  read for it and it takes 100 ms.
 *
 *  Everywhere, the plain wait must last about what was asked -- at least
 *  half of it, since a Windows wait may end a clock tick early.  The
 *  condition variable and the deadline have to be on one clock, and a
 *  deadline on one compared with the other is decades out, which ends
 *  the wait at once (or never): this is the check that they agree.
 *  macOS waits relative and Windows takes milliseconds, neither reading
 *  the wall clock, so the skewed half is Linux's alone.
 */
#if defined(__linux__)

static int64_t  realtime_skew_ns;

int
clock_gettime(clockid_t id, struct timespec *ts)
{
    long    rc = syscall(SYS_clock_gettime, id, ts);

    if (rc == 0 && id == CLOCK_REALTIME && realtime_skew_ns != 0) {
        int64_t n = (int64_t) ts->tv_sec * INT64_C(1000000000)
                  + (int64_t) ts->tv_nsec + realtime_skew_ns;

        ts->tv_sec  = (time_t) (n / INT64_C(1000000000));
        ts->tv_nsec = (long) (n % INT64_C(1000000000));
    }
    return (int) rc;
}

#endif

static void
test_bugs7e_sched(void)
{
    const int64_t   asked = INT64_C(100000000);         /*  100 ms  */
    st_mutex        lock;
    st_cond         never;
    int64_t         start;
    int64_t         took;

    CHECK_EQ_INT(ST_mutex_init(&lock), 0);
    CHECK_EQ_INT(ST_cond_init(&never), 0);
    ST_mutex_lock(&lock);

    start = ST_time_monotonic_ns();
    CHECK_EQ_INT(ST_cond_timedwait(&never, &lock, asked), -1);
    took = ST_time_monotonic_ns() - start;
    CHECK(took >= asked / 2);
    CHECK(took < INT64_C(2000000000));

#if defined(__linux__)
    {
        const int64_t   skew = INT64_C(5000000000);     /*  5 s  */
        int64_t         wall_before;
        int64_t         wall_after;

        wall_before = ST_time_smalltalk_ms();
        realtime_skew_ns = skew;
        wall_after = ST_time_smalltalk_ms();
        start = ST_time_monotonic_ns();
        CHECK_EQ_INT(ST_cond_timedwait(&never, &lock, asked), -1);
        took = ST_time_monotonic_ns() - start;
        realtime_skew_ns = 0;
        /*  The simulation reached the port layer's wall-clock reads.  */
        CHECK(wall_after - wall_before >= skew / 1000000 - 100);
        /*  And the wait did not care.  */
        CHECK(took >= asked / 2);
        CHECK(took < INT64_C(2000000000));
        printf("  (FILES-9: a 100 ms timed wait with the wall clock 5 s behind "
               "the deadline's took %lld ms)\n", (long long) (took / 1000000));
    }
#endif

    ST_mutex_unlock(&lock);
    ST_cond_destroy(&never);
    ST_mutex_destroy(&lock);
}

/*
 *  FILES-10: on Windows every file name and every ODBC string went to an
 *  -A function, which reads its bytes in the ANSI code page, while the
 *  bytes are UTF-8: a name with an e-acute in it was created with two
 *  other characters in its place, on a Western install.  The fix is
 *  the application manifest that makes UTF-8 the process's code page,
 *  which Linux cannot load; so here the tree is checked to carry it where
 *  Makefile.msvc takes it from, well-formed (no double hyphen inside its
 *  comment, which the manifest tool refuses), and the link line is checked
 *  to embed it.  On Windows -- the suites link with the same line -- this
 *  process's own code page is checked to be UTF-8, which is the fix itself.
 */
static char *
bugs7e_read_all(const char *path)
{
    FILE   *f = fopen(path, "rb");
    char   *text = NULL;
    long    n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0
     && fseek(f, 0, SEEK_SET) == 0
     && (text = (char *) malloc((size_t) n + 1)) != NULL) {
        if (fread(text, 1, (size_t) n, f) != (size_t) n) {
            free(text);
            text = NULL;
        } else
            text[n] = '\0';
    }
    fclose(f);
    return text;
}

static void
test_bugs7e_files_text(void)
{
    char   *manifest = bugs7e_read_all("src/st2026.manifest");
    char   *makefile = bugs7e_read_all("Makefile.msvc");

    CHECK(manifest != NULL);
    CHECK(makefile != NULL);
    if (manifest != NULL) {
        const char *open  = strstr(manifest, "<!--");
        const char *close = open ? strstr(open + 4, "-->") : NULL;
        const char *dash  = open ? strstr(open + 4, "--") : NULL;

        CHECK(strstr(manifest, "<activeCodePage xmlns=\"http://schemas."
                     "microsoft.com/SMI/2019/WindowsSettings\">UTF-8"
                     "</activeCodePage>") != NULL);
        CHECK(open == NULL || (close != NULL && dash == close));
    }
    if (makefile != NULL)
        CHECK(strstr(makefile, "/MANIFEST:EMBED /MANIFESTINPUT:src\\st2026.manifest")
              != NULL);
#ifdef ST_WINDOWS
    CHECK(GetACP() == CP_UTF8);
#endif
    free(manifest);
    free(makefile);
}

/*
 *  FILES-15: on Windows a timed wait of less than a millisecond was a
 *  wait of none -- the nanoseconds were rounded DOWN to milliseconds, and
 *  SleepConditionVariableSRW(..., 0) returns at once -- so the delay timer
 *  spun through the last fraction of every Delay.  A wait that times out
 *  must have lasted what was asked, a fraction of a millisecond included,
 *  on every platform; on Windows the old rounding fails the first one.
 *  (A wait that ends early with 0 is a wakeup, which is allowed, and is
 *  not counted.)
 */
static void
test_bugs7f_files(void)
{
    static const int64_t    asked[] = {
        INT64_C(1), INT64_C(250000), INT64_C(400000), INT64_C(999999),
        INT64_C(1000001), INT64_C(1500000)
    };
    st_mutex    lock;
    st_cond     never;
    unsigned    i;
    unsigned    short_waits = 0;

    CHECK_EQ_INT(ST_mutex_init(&lock), 0);
    CHECK_EQ_INT(ST_cond_init(&never), 0);
    ST_mutex_lock(&lock);
    for (i = 0; i < sizeof asked / sizeof asked[0]; ++i) {
        int64_t start = ST_time_monotonic_ns();
        int     rc = ST_cond_timedwait(&never, &lock, asked[i]);
        int64_t took = ST_time_monotonic_ns() - start;

        if (rc == -1 && took < asked[i]) {
            ++short_waits;
            printf("  (FILES-15: a %lld ns timed wait timed out after %lld ns)\n",
                   (long long) asked[i], (long long) took);
        }
    }
    CHECK_EQ_INT((int) short_waits, 0);
    ST_mutex_unlock(&lock);
    ST_cond_destroy(&never);
    ST_mutex_destroy(&lock);
}

/*
 *  X-24: the other half of FILES-10.  The manifest makes the ANSI code
 *  page UTF-8 only on Windows 10 1903 and later; before that every narrow
 *  file call read a name's UTF-8 bytes in the code page.  Now every path
 *  goes to a -W call through ST_wide_from_utf8, so a name means the same
 *  whatever the code page.  Here a file is made, sized, found and removed
 *  by a name holding an e-acute and a CJK character, through the port's
 *  calls, and ST_path_kind tells a directory from a file from nothing.
 *  On Windows the name it really got is asked of the system in UTF-16,
 *  the conversions are checked both ways, and a name that is not UTF-8
 *  (Latin-1 bytes, which named the right file through the -A calls on a
 *  1252 system) is checked to be read in the code page, as it was.  Run
 *  under Wine without the manifest -- code page 1252 -- this is the test
 *  of the fix; on Linux the names are bytes and it checks the contract.
 */
static void
test_bugs7f_files_names(void)
{
    const char *dir = st_test_path("bugs7f-x24");
    char        name[1024];
    char       *args[] = { "st2026", "caf\xC3\xA9", NULL };
    FILE       *f;
    int64_t     size = -1;

    snprintf(name, sizeof name, "%s.caf\xC3\xA9-\xE6\x97\xA5.txt", dir);
    ST_remove(name);
    f = ST_fopen(name, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        CHECK_EQ_INT(fwrite("abc", 1, 3, f), 3);
        CHECK_EQ_INT(fclose(f), 0);
    }
    CHECK_EQ_INT(ST_path_kind(name, &size), 0);
    CHECK_EQ_INT(size, 3);
    f = ST_fopen(name, "rb");
    CHECK(f != NULL);
    if (f != NULL)
        fclose(f);
#ifdef ST_WINDOWS
    {
        wchar_t    *wide = ST_wide_from_utf8(name);
        wchar_t    *latin = ST_wide_from_utf8("caf\xE9");
        char       *back;
        size_t      n = wide ? wcslen(wide) : 0;

        /*  The file's real name ends in e-acute, `-', U+65E5, `.txt'.  */
        CHECK(n > 7 && wcscmp(wide + n - 7, L"\u00e9-\u65e5.txt") == 0);
        CHECK(wide != NULL
              && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES);
        back = wide ? ST_utf8_from_wide(wide) : NULL;
        CHECK(back != NULL && strcmp(back, name) == 0);
        if (GetACP() == 1252)
            CHECK(latin != NULL && wcscmp(latin, L"caf\u00e9") == 0);
        free(back);
        free(latin);
        free(wide);
    }
#endif
    CHECK_EQ_INT(ST_remove(name), 0);
    CHECK_EQ_INT(ST_path_kind(name, NULL), -1);
    CHECK_EQ_INT(errno, ENOENT);
    CHECK_EQ_INT(ST_path_kind(".", NULL), 1);
    CHECK_EQ_INT(ST_path_kind("src/port/st_port.c", NULL), 0);
    /*  argv is the CRT's when the code page is UTF-8, and always here.  */
#ifdef ST_WINDOWS
    if (GetACP() == CP_UTF8)
#endif
        CHECK(ST_utf8_arguments(2, args) == args);
}

int
main(void)
{
    ST_TEST_BEGIN("port layer");

    test_thread_create_join();
    test_mutex_excludes();
    test_mutex_trylock();
    test_cond_handoff();
    test_cond_timeout();
    test_tls_is_per_thread();
    test_atomic_counter();
    test_cas_loop();
    test_cpu_count();
    test_time();
    test_bugs7c_lib();
    test_bugs7c_lib_sleep();
    test_bugs7e_files();
    test_bugs7e_sched();
    test_bugs7e_files_text();
    test_bugs7f_files();
    test_bugs7f_files_names();

    return ST_TEST_END();
}
