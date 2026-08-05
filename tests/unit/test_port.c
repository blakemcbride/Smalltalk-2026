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

#include <stdlib.h>

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

    return ST_TEST_END();
}
