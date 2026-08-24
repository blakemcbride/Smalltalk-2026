/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The worker pool and the safepoint protocol.  See worker.h.
 */

#include "worker.h"
#include "interp.h"

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
WORKER_at(unsigned index)
{
    if (index >= WORKER_count())
        return NULL;
    return &workers[index];
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

    /*
     *  Write this worker's registers back into its context before parking.
     *
     *  The collector marks a context only as far as its stack pointer, and
     *  that pointer lives in the context while the interpreter keeps its own
     *  copy in a register.  Parking without storing it back leaves the
     *  collector reading the value from the last context switch -- too small
     *  and the live stack is not marked, too large and dead slots are.
     */
    ST_store_active_context();
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
 *  ----------  Blocking outside the object memory  ----------
 *
 *  These two are WORKER_poll_slow cut in half.  The first half parks; the
 *  second half waits for the release and unparks.  A blocking region is the
 *  same thing with the caller's own work in the middle, so the code that
 *  makes a worker count as stopped is the code above, not a second copy of
 *  it that could drift.
 *
 *  lock_ready is checked because the bootstrap runs before the pool exists,
 *  and a database opened from a startup expression would otherwise lock an
 *  uninitialised mutex.  current_worker being NULL is the same case seen
 *  from the other side -- the main thread is not a worker, nobody is waiting
 *  for it to park, and it may block freely.
 */
void
WORKER_enter_native(void)
{
    st_worker  *self = current_worker;

    if (!self || !lock_ready)
        return;

    /*
     *  The same store, for the same reason, as the poll does: the collector
     *  marks a context only as far as the stack pointer recorded IN the
     *  context, and the interpreter keeps its live copy in a register.
     *  Blocking without writing it back leaves the collector reading a
     *  pointer from the last context switch, and the parked worker's live
     *  stack either under-marked or over-marked -- the first of which frees
     *  an object the worker is about to use.
     */
    ST_store_active_context();
    ST_mutex_lock(&safepoint_lock);
    ST_store_relaxed(&self->at_safepoint, 1);
    ST_fetch_add_acq_rel(&parked_count, 1);
    /*
     *  Broadcast even though nobody may be asking yet.  A requester that
     *  arrives a microsecond later reads parked_count and finds this worker
     *  already counted; one that is mid-wait needs the wake.  The cost of
     *  the broadcast is paid once per database call, against a round trip.
     */
    ST_cond_broadcast(&safepoint_reached);
    ST_mutex_unlock(&safepoint_lock);
}

void
WORKER_leave_native(void)
{
    st_worker  *self = current_worker;

    if (!self || !lock_ready)
        return;

    ST_mutex_lock(&safepoint_lock);
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

/*
 *  How long the world has been stopped, in total, and how often.
 *
 *  Without this a scaling failure cannot be attributed and you will guess.
 *  Eight workers that go half as fast as four could be contending on a
 *  lock, or thrashing a cache line, or simply spending their time parked
 *  while one of them collects -- and those want three different fixes.
 *  The number is free to keep and decisive to have.
 */
static st_atomic_i64    safepoint_pause_ns;
static st_atomic_int    safepoint_count;
/*
 *  The worst single stop, and the best.
 *
 *  A mean is the wrong statistic for a pause: seven stops averaging 74 ms
 *  could be seven stops of 74 ms, or six of half a millisecond and one of
 *  half a second, and those are different bugs with different fixes.
 */
static st_atomic_i64    safepoint_worst_ns;
static st_atomic_i64    safepoint_best_ns;
static _Thread_local int64_t    safepoint_began;

int64_t
WORKER_safepoint_pause_ns(void)
{
    return ST_load_seq(&safepoint_pause_ns);
}

int
WORKER_safepoint_count(void)
{
    return ST_load_seq(&safepoint_count);
}

int64_t
WORKER_safepoint_worst_ns(void)
{
    return ST_load_seq(&safepoint_worst_ns);
}

int64_t
WORKER_safepoint_best_ns(void)
{
    int64_t best = ST_load_seq(&safepoint_best_ns);

    return best == INT64_MAX ? 0 : best;
}

void
WORKER_reset_safepoint_statistics(void)
{
    ST_store_seq(&safepoint_pause_ns, 0);
    ST_store_seq(&safepoint_count, 0);
    ST_store_seq(&safepoint_worst_ns, 0);
    ST_store_seq(&safepoint_best_ns, INT64_MAX);
}

/*
 *  Whether a stop-the-world is already being arranged, and by whom.
 *
 *  Without this two workers could request one at the same moment, and
 *  then wait for each other for ever: each counts the other as a thread
 *  that still has to park, and neither can park, because both are inside
 *  this function rather than at a poll.  Nothing times out and nothing
 *  crashes -- the process simply stops, with every worker "running".
 *
 *  It went unseen because the only test that collected under load
 *  collected from worker zero and nowhere else.  The scaling benchmark
 *  has every worker allocating, which is the ordinary case and the one
 *  that finds it: two collections wanted at once.
 *
 *  A worker that loses the race does NOT queue behind the winner -- that
 *  is the same deadlock wearing a mutex.  It parks for the winner's
 *  safepoint like any other worker, and asks again afterwards.
 */
static st_atomic_int    safepoint_in_progress;

void
WORKER_request_safepoint(void)
{
    const st_worker    *requester = current_worker;

    if (!lock_ready) {
        safepoint_began = ST_time_monotonic_ns();
        return;                 /*  single-threaded: nothing to stop  */
    }

    for (;;) {
        int expected = 0;

        ST_mutex_lock(&safepoint_lock);
        if (ST_cas_strong(&safepoint_in_progress, &expected, 1)) {
            /*
             *  The clock starts HERE, not on entry.
             *
             *  A worker that loses this race parks for the winner's
             *  safepoint, and timing from entry counted that wait as its
             *  own pause -- so eight workers wanting one collection
             *  reported eight pauses, seven of them measuring how long
             *  they had waited for the eighth.  It said 322 ms of
             *  stop-the-world where the collector's own work was 0.4 ms.
             */
            safepoint_began = ST_time_monotonic_ns();
            break;
        }
        ST_mutex_unlock(&safepoint_lock);
        /*
         *  Somebody else is stopping the world.  Be stopped: park exactly
         *  as the poll would, so the winner's count can be reached, and
         *  come back when it lets go.
         */
        WORKER_poll_slow();
    }
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
    if (safepoint_began) {
        int64_t took = ST_time_monotonic_ns() - safepoint_began;
        int64_t seen;

        ST_fetch_add_relaxed(&safepoint_pause_ns, took);
        ST_fetch_add_relaxed(&safepoint_count, 1);
        seen = ST_load_relaxed(&safepoint_worst_ns);
        while (took > seen && !ST_cas_weak(&safepoint_worst_ns, &seen, took))
            ;
        seen = ST_load_relaxed(&safepoint_best_ns);
        while (took < seen && !ST_cas_weak(&safepoint_best_ns, &seen, took))
            ;
        safepoint_began = 0;
    }
    if (!lock_ready)
        return;
    ST_mutex_lock(&safepoint_lock);
    ST_store_seq(&st_safepoint_requested, 0);
    ST_store_seq(&safepoint_in_progress, 0);
    ST_cond_broadcast(&safepoint_released);
    ST_mutex_unlock(&safepoint_lock);
}

uint32_t
WORKER_at_safepoint(uint32_t (*fn)(void *user), void *user)
{
    uint32_t    result;
    int64_t     asked;
    int64_t     got;
    int64_t     done;

    /*
     *  The requester parks nobody, so it writes its own registers back here
     *  -- same reason as WORKER_poll_slow, and the requester is the thread
     *  most likely to be in the middle of something, since a collection is
     *  usually asked for by whoever ran out of room.
     */
    ST_store_active_context();
    asked = ST_time_monotonic_ns();
    WORKER_request_safepoint();
    got = ST_time_monotonic_ns();
    result = fn(user);
    done = ST_time_monotonic_ns();
    if (getenv("ST_SAFEPOINT_LOG")
     && (done - asked) > INT64_C(2000000))
        fprintf(stderr, "st80: safepoint %.2f ms = %.2f waiting for %u "
                        "worker(s) + %.2f doing the work\n",
                (double) (done - asked) / 1e6,
                (double) (got - asked) / 1e6,
                worker_count,
                (double) (done - got) / 1e6);
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
