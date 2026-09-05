/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The mourn queue.  See finalize.h for what it is for.
 */

#include "finalize.h"
#include "interp.h"
#include "prim.h"
#include "st_port.h"
#include "st_sched.h"

#include <stdlib.h>

/*
 *  A plain array with a read cursor rather than a ring.
 *
 *  The producer is a collection, which appends a burst and then does not
 *  run again for a long time; the consumer is one process draining to
 *  empty.  So the array is emptied far more often than it is grown, and
 *  when the cursor catches up with the count both go back to zero and the
 *  storage is reused.  It grows to the largest burst ever queued and stops.
 */
static st_oop      *queue;
static uint32_t     queue_count;        /*  entries written  */
static uint32_t     queue_head;         /*  entries already handed out  */
static uint32_t     queue_capacity;

/*
 *  The Semaphore, and whether a signal is owed.
 *
 *  Both are guarded by the same lock as the queue.  `owed' is set by the
 *  collector, which cannot signal from inside a safepoint -- every worker
 *  is parked there, and the scheduler's async queue is drained by a worker
 *  at a bytecode boundary, which is exactly what none of them can reach
 *  until the safepoint ends.  So the collector records the debt and
 *  OM_mourn_wake pays it once the world is running again.
 */
static st_oop       mourn_semaphore = ST_NIL;
static int          signal_owed;

static st_mutex     mourn_lock;
static int          mourn_lock_ready;

static void
mourn_lock_init(void)
{
    /*
     *  Made on first use, on whichever thread gets there first, and never
     *  destroyed.  The alternative -- an initializer in OM_init -- would
     *  miss the unit tests that drive the collector without one.
     */
    if (!mourn_lock_ready) {
        if (ST_mutex_init(&mourn_lock) != 0)
            return;
        mourn_lock_ready = 1;
    }
}

void
OM_mourn_queue_add(st_oop ephemeron)
{
    if (!OM_is_object(ephemeron))
        return;
    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    if (queue_count >= queue_capacity) {
        uint32_t    want = queue_capacity ? queue_capacity * 2 : 64;
        st_oop     *grown = (st_oop *) realloc(queue,
                                               (size_t) want * sizeof *grown);

        /*
         *  A queue that cannot grow drops the ephemeron rather than
         *  failing the collection.  The consequence is one association
         *  that stays in its dictionary with a key nothing else holds --
         *  the leak this whole file exists to end, for one entry -- and
         *  the alternative at this point, inside a safepoint with the
         *  world stopped, is to have no memory and abandon the collection
         *  that was going to find some.
         */
        if (!grown) {
            ST_mutex_unlock(&mourn_lock);
            return;
        }
        queue          = grown;
        queue_capacity = want;
    }
    queue[queue_count++] = ephemeron;
    signal_owed = 1;
    ST_mutex_unlock(&mourn_lock);
}

void
OM_mourn_queue_visit(om_visit_fn visit)
{
    uint32_t    i;

    if (!visit)
        return;
    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    for (i = queue_head; i < queue_count; ++i) {
        st_oop  p = queue[i];
        uint32_t    n;
        uint32_t    j;

        /*
         *  The ephemeron AND every field of it.
         *
         *  Visiting the ephemeron alone would not do: the collector sets
         *  an ephemeron aside rather than walking it, and would then find
         *  its key unmarked and queue it to be mourned a SECOND time -- and
         *  the key it is about to be asked to remove would already have
         *  been swept.  Marking the fields here is what makes the queue a
         *  reprieve rather than a list of dangling oops: mourn is sent with
         *  the key still there, which is the only reason `container
         *  removeKey: key' can find anything.
         */
        visit(p);
        if (!OM_is_object(p) || !OM_pointer_bit(p))
            continue;
        n = OM_fetch_word_length(p);
        for (j = 0; j < n; ++j)
            visit(OM_fetch_pointer(j, p));
    }
    if (OM_is_object(mourn_semaphore))
        visit(mourn_semaphore);
    ST_mutex_unlock(&mourn_lock);
}

uint32_t
OM_mourn_pending(void)
{
    uint32_t    n;

    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    n = queue_count - queue_head;
    ST_mutex_unlock(&mourn_lock);
    return n;
}

st_oop
OM_take_mourned(void)
{
    st_oop  p = ST_NIL;

    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    if (queue_head < queue_count) {
        p = queue[queue_head++];
        if (queue_head == queue_count)
            queue_head = queue_count = 0;
    }
    ST_mutex_unlock(&mourn_lock);
    return p;
}

void
OM_set_mourn_semaphore(st_oop semaphore)
{
    int     owed;

    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    mourn_semaphore = semaphore;
    /*
     *  A debt run up while nobody was listening is paid the moment somebody
     *  is.  The semaphore lives in C and an image file does not carry it,
     *  so an image snapshotted while the finalization process was asleep
     *  comes back with a queue the collector can fill and nowhere to say
     *  so; the first drain from anywhere -- Finalizer restart, or a test
     *  driving the queue itself -- arms it again, and this is what makes
     *  that drain also wake the process for whatever had piled up.
     */
    owed = signal_owed && queue_head < queue_count;
    ST_mutex_unlock(&mourn_lock);
    if (owed)
        OM_mourn_wake();
}

st_oop
OM_mourn_semaphore(void)
{
    st_oop  s;

    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    s = mourn_semaphore;
    ST_mutex_unlock(&mourn_lock);
    return s;
}

void
OM_mourn_wake(void)
{
    st_oop  semaphore = ST_NIL;

    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    /*
     *  The debt is only discharged when there is somewhere to discharge it
     *  TO.  Clearing it against a nil semaphore would lose the wake for
     *  good -- and nil is exactly the state a freshly loaded image is in,
     *  since the registration is C state and no image file carries it.
     */
    if (signal_owed && queue_head < queue_count
     && OM_is_present(mourn_semaphore)) {
        semaphore   = mourn_semaphore;
        signal_owed = 0;
    }
    ST_mutex_unlock(&mourn_lock);
    /*
     *  Outside the lock.  SCHED_asynchronous_signal takes a lock of its
     *  own, and the network I/O thread posts to that one without knowing
     *  anything about this one; taking them in one order here and the
     *  other there is the shape of a deadlock nobody would reproduce.
     */
    if (OM_is_present(semaphore))
        SCHED_asynchronous_signal(semaphore);
}

void
OM_mourn_reset(void)
{
    mourn_lock_init();
    ST_mutex_lock(&mourn_lock);
    free(queue);
    queue           = NULL;
    queue_count     = 0;
    queue_head      = 0;
    queue_capacity  = 0;
    mourn_semaphore = ST_NIL;
    signal_owed     = 0;
    ST_mutex_unlock(&mourn_lock);
}

/*
 *  236: Finalizer class>>primNextMournedObjectSignalling: aSemaphore
 *
 *  Two things in one send, because they belong to one process and are only
 *  ever wanted together: remember the Semaphore to signal when an ephemeron
 *  is queued, and answer the oldest queued ephemeron, or nil when there is
 *  none.  The finalization process asks in a loop until it gets nil and
 *  then waits on the same semaphore, so registering it on every pass costs
 *  a store and removes the window a separate `install the semaphore' send
 *  would open -- one where the collector has something to say and nowhere
 *  to say it.
 *
 *  A signal that arrives between the last nil answer and the wait is not
 *  lost: a Semaphore counts its excess signals, so the wait returns at once.
 */
int
OM_primitive_next_mourned(void)
{
    st_oop  semaphore = ST_stack_value(0);

    if (semaphore != ST_NIL && !OM_is_object(semaphore))
        return 0;
    OM_set_mourn_semaphore(semaphore);
    ST_pop_n(2);
    ST_push(OM_take_mourned());
    return 1;
}
