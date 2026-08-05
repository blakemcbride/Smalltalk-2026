/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The worker pool and the safepoint protocol.  See worker.h.
 */

#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

st_atomic_int   st_safepoint_requested;

static st_worker    workers[ST_MAX_WORKERS];
static unsigned     worker_count;

/*
 *  One mutex and one condition variable coordinate the whole protocol.  The
 *  fast path never touches them: a worker that is not being asked to park
 *  does a single relaxed load and carries on.
 */
static st_mutex         safepoint_lock;
static st_cond          safepoint_reached;   /*  workers -> requester  */
static st_cond          safepoint_released;  /*  requester -> workers  */
static st_atomic_int    parked_count;
static int              lock_ready;

/*
 *  Which worker is this thread?  A thread-local pointer rather than a search,
 *  since the interpreter asks on every allocation.
 */
static _Thread_local st_worker *current_worker;

unsigned
WORKER_count(void)
{
    return worker_count;
}

st_worker *
WORKER_self(void)
{
    return current_worker;
}

/*  ----------  The safepoint  ----------  */

void
WORKER_poll_slow(void)
{
    st_worker  *self = current_worker;

    if (!self)
        return;

    ST_mutex_lock(&safepoint_lock);
    ST_store_relaxed(&self->at_safepoint, 1);
    ST_fetch_add_acq_rel(&parked_count, 1);
    ST_cond_broadcast(&safepoint_reached);

    /*
     *  Wait for the request to clear.  A condition variable rather than a
     *  spin: a collection can take a while, and burning a core to notice its
     *  end sooner is the wrong trade on a machine whose whole point is to
     *  use its cores for Smalltalk.
     */
    while (ST_load_relaxed(&st_safepoint_requested) != 0)
        ST_cond_wait(&safepoint_released, &safepoint_lock);

    ST_store_relaxed(&self->at_safepoint, 0);
    ST_fetch_sub_acq_rel(&parked_count, 1);
    ST_mutex_unlock(&safepoint_lock);
}

/*
 *  How many threads still have to park before the requester may proceed.
 *
 *  Two exclusions, both required for the protocol to terminate:
 *
 *      A worker that has finished its body will never poll again, so
 *      waiting for it would hang forever.  Note the asymmetry with a worker
 *      that has not STARTED its body: that one must be waited for, and
 *      WORKER_start marks it running before creating it so that it is.
 *
 *      The requester itself is a worker in the common case -- a collection
 *      is triggered by whoever ran out of room -- and it plainly will not
 *      park while it is the one waiting.  Counting itself is a deadlock
 *      that stays hidden until a worker, rather than the main thread, asks
 *      for the safepoint.
 */
static unsigned
threads_to_park(const st_worker *requester)
{
    unsigned    n = 0;
    unsigned    i;

    for (i = 0; i < worker_count; ++i) {
        if (&workers[i] == requester)
            continue;
        if (ST_load_relaxed(&workers[i].running))
            ++n;
    }
    return n;
}

unsigned
WORKER_unparked_count(void)
{
    const st_worker    *self = current_worker;
    unsigned            n = 0;
    unsigned            i;

    for (i = 0; i < worker_count; ++i) {
        if (&workers[i] == self)
            continue;
        if (ST_load_seq(&workers[i].exited))
            continue;
        if (!ST_load_seq(&workers[i].at_safepoint))
            ++n;
    }
    return n;
}

void
WORKER_request_safepoint(void)
{
    const st_worker    *requester = current_worker;

    if (!lock_ready)
        return;                 /*  single-threaded: nothing to stop  */

    ST_mutex_lock(&safepoint_lock);
    ST_store_seq(&st_safepoint_requested, 1);
    while ((unsigned) ST_load_relaxed(&parked_count)
            < threads_to_park(requester)) {
        if (ST_cond_timedwait(&safepoint_reached, &safepoint_lock,
                              INT64_C(1000000000)) != 0
         && getenv("ST_SAFEPOINT_LOG")) {
            unsigned    i;

            fprintf(stderr, "  safepoint stalled: requester=%d parked=%d "
                            "needed=%u\n",
                    requester ? (int) requester->index : -1,
                    ST_load_relaxed(&parked_count),
                    threads_to_park(requester));
            for (i = 0; i < worker_count; ++i)
                fprintf(stderr, "    worker %u running=%d parked=%d\n", i,
                        ST_load_relaxed(&workers[i].running),
                        ST_load_relaxed(&workers[i].at_safepoint));
        }
    }
    ST_mutex_unlock(&safepoint_lock);
}

void
WORKER_release_safepoint(void)
{
    if (!lock_ready)
        return;
    ST_mutex_lock(&safepoint_lock);
    ST_store_seq(&st_safepoint_requested, 0);
    ST_cond_broadcast(&safepoint_released);
    ST_mutex_unlock(&safepoint_lock);
}

uint32_t
WORKER_at_safepoint(uint32_t (*fn)(void *user), void *user)
{
    uint32_t    result;

    WORKER_request_safepoint();
    result = fn(user);
    WORKER_release_safepoint();
    return result;
}

/*  ----------  The pool  ----------  */

static void
worker_main(void *arg)
{
    st_worker  *self = (st_worker *) arg;
    char        name[16];

    current_worker = self;
    snprintf(name, sizeof name, "st-worker-%u", self->index);
    ST_thread_set_name(name);

    /*  running was set by WORKER_start, before this thread existed.  */
    if (self->body)
        self->body(self, self->user);

    /*
     *  Leaving the pool has to be visible to a requester that may already be
     *  counting, and it has to wake one that is waiting -- otherwise a
     *  safepoint requested just as this worker exits would wait for a thread
     *  that will never poll again.
     */
    ST_mutex_lock(&safepoint_lock);
    ST_store_seq(&self->exited, 1);
    ST_store_seq(&self->running, 0);
    ST_cond_broadcast(&safepoint_reached);
    ST_mutex_unlock(&safepoint_lock);
}

int
WORKER_start(unsigned count, void (*body)(st_worker *self, void *user),
             void *user)
{
    unsigned    i;

    if (worker_count != 0)
        return -1;
    if (count == 0) {
        int cpus = ST_cpu_count();

        /*  One core is the SDL pump's; the rest run Smalltalk.  */
        count = (cpus > 1) ? (unsigned) (cpus - 1) : 1;
    }
    if (count > ST_MAX_WORKERS)
        count = ST_MAX_WORKERS;

    if (!lock_ready) {
        if (ST_mutex_init(&safepoint_lock) != 0
         || ST_cond_init(&safepoint_reached) != 0
         || ST_cond_init(&safepoint_released) != 0)
            return -1;
        lock_ready = 1;
    }
    ST_store_seq(&st_safepoint_requested, 0);
    ST_store_seq(&parked_count, 0);

    memset(workers, 0, sizeof workers);
    worker_count = count;
    /*
     *  A worker counts as running from the moment it is created, not from
     *  the moment it starts.
     *
     *  Setting this inside the thread itself opens a window that is invisible
     *  in every ordinary run and fatal in the ones that matter: worker_count
     *  is already the full count, so threads_to_park walks every slot, but a
     *  thread that has not reached its first instruction still reads
     *  running == 0 and is skipped.  A collection requested during start-up
     *  therefore proceeds without waiting for it, and that worker's first act
     *  is to allocate -- into the table the collector is sweeping.  The
     *  object it creates has no references yet, so the collector reclaims it
     *  and hands its table slot to the next allocation, which frees the body
     *  its creator is still initialising.
     *
     *  Counting an unstarted worker costs at most a short wait: it will reach
     *  a poll, because polling is what the interpreter loop does.
     */
    for (i = 0; i < count; ++i) {
        workers[i].index = i;
        workers[i].body  = body;
        workers[i].user  = user;
        ST_store_seq(&workers[i].running, 1);
        ST_store_seq(&workers[i].exited, 0);
        ST_store_seq(&workers[i].at_safepoint, 0);
    }
    for (i = 0; i < count; ++i) {
        if (ST_thread_create(&workers[i].thread, worker_main, &workers[i]) != 0) {
            /*  This one will never start, so nothing may wait for it.  */
            ST_store_seq(&workers[i].exited, 1);
            ST_store_seq(&workers[i].running, 0);
            worker_count = i;
            WORKER_stop();
            return -1;
        }
    }
    return 0;
}

void
WORKER_stop(void)
{
    unsigned    i;

    for (i = 0; i < worker_count; ++i)
        ST_thread_join(workers[i].thread);
    worker_count = 0;
}
