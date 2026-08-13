/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Processes and semaphores.  See sched.h for the contract.
 */

#include "st_sched.h"
#include "interp.h"
#include "prim.h"
#include "st_port.h"
#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  Asynchronous signals are queued rather than applied where they arise.
 *  An event can land while the interpreter is midway through a bytecode, and
 *  switching processes at that moment would leave the stack inconsistent, so
 *  the queue is drained at the next process-switch check.
 *
 *  Many producers, one consumer at a time, and a mutex rather than a lock-
 *  free ring.  The producers are interrupt-ish -- a keystroke, a timer --
 *  so the queue is touched a few thousand times a second at most, and the
 *  contention that would justify anything cleverer does not exist.  What
 *  DOES matter is that thread 0, which pumps the window system, never
 *  blocks for long: the critical section is a memcpy of at most 64 words
 *  and holds no other lock.
 */
#define ASYNC_QUEUE_MAX     64

static st_oop   async_queue[ASYNC_QUEUE_MAX];
/*
 *  Atomic because drain_async_signals reads it WITHOUT the lock, to skip an
 *  empty queue without paying for one -- and that read is on the hot path
 *  of every send.  The unlocked read was always there and was always a
 *  race; it went unreported because until the delay timer there was no
 *  producer running concurrently with a worker that could be interpreting.
 *  A relaxed load keeps the shortcut and gives it a defined meaning: a
 *  stale zero costs one more pass round the caller's loop and nothing else.
 */
static st_atomic_int    async_count;
static st_mutex async_lock;
static int      async_lock_ready;

/*
 *  Lazily, like the stripes, and for a reason found the hard way: making
 *  it SCHED_reset's job meant a signal arriving before the first reset was
 *  silently dropped, and the input events the Sensor tests post arrive
 *  exactly there.  An initialiser a caller can forget to run is an
 *  initialiser that will be forgotten.
 */
static void
async_lock_init(void)
{
    if (async_lock_ready)
        return;
    ST_mutex_init(&async_lock);
    async_lock_ready = 1;
}

/*  ----------  The delay timer  ----------
 *
 *  Primitive 136, `Processor signal: aSemaphore atTime: ms'.  One pending
 *  request, re-armed on every call and cancelled by a nil semaphore, which
 *  is the whole of the Blue Book contract: the image keeps the queue of
 *  waiting Delays in Smalltalk and asks the VM only for the next one.
 *
 *  It is a thread of its own, and that is forced rather than chosen.  A
 *  delay expires while nothing is running -- that is what a delay IS -- so
 *  whoever notices cannot be a worker interpreting bytecodes.  Nor can it
 *  be the idle loop below: with every process waiting on a Delay, every
 *  worker is in that loop, and a loop that only polls what is ready would
 *  spin until the heat death of the machine.  Something outside the
 *  scheduler has to hold the clock.
 *
 *  Without it the four Chronology test classes that wait on a Delay --
 *  BlockClosureValueWithinTest and its Duration twin, StopwatchTest,
 *  DateAndTimeLeapTest -- parked on a semaphore nothing would ever signal
 *  and took the whole run down with them, so 559 passing tests in the same
 *  image reported nothing at all.
 */

static st_mutex     timer_lock;
static st_cond      timer_cond;
static int          timer_ready;            /*  lock and cond initialised  */
static st_oop       timer_semaphore = ST_NIL;
static int64_t      timer_deadline_ns;
static int          timer_armed;
/*
 *  True from the instant the timer stops being armed until its signal is
 *  actually in the async queue.
 *
 *  Those are not the same moment, and the gap between them is microseconds
 *  wide and perfectly reliable: the waiter's loop asks "is a timer still
 *  pending?", the answer turns false as the timer fires, and the waiter
 *  gives up a moment before the signal it was waiting for arrives.  Every
 *  Delay in the system deadlocked on that window.  A timer that has fired
 *  but not yet delivered is still pending, because to everyone waiting it
 *  has not happened yet.
 */
static int          timer_delivering;
static int          timer_stopping;
static st_thread    timer_thread;
static int          timer_started;

/*
 *  The millisecond clock primitive 135 answers wraps at 2^30, so a target
 *  time does too, and "is this in the past" cannot be a plain comparison.
 *  The difference is taken in the modulus and read as signed: more than
 *  half a wrap away in front means it is really behind, which is what the
 *  Blue Book's own delay arithmetic assumes.
 */
#define MS_CLOCK_MODULUS    (1u << 30)

static int64_t
milliseconds_until(uint32_t target_ms)
{
    uint32_t    now   = ST_time_ms_clock();
    uint32_t    delta = (target_ms - now) & (MS_CLOCK_MODULUS - 1);

    if (delta >= MS_CLOCK_MODULUS / 2)
        return 0;                       /*  already past  */
    return (int64_t) delta;
}

static void drain_async_signals(void);

static void
timer_init(void)
{
    if (timer_ready)
        return;
    ST_mutex_init(&timer_lock);
    ST_cond_init(&timer_cond);
    timer_ready = 1;
}

/*
 *  True while a delay is outstanding.
 *
 *  The idle loop asks, because "every worker is idle" and "nothing can ever
 *  run again" are the same sentence only when no clock is counting.  A
 *  delay of a second in an otherwise quiet image is every worker idle for
 *  a second, and calling that a deadlock would have made Delay unusable in
 *  exactly the case it exists for.
 */
int
SCHED_timer_pending(void)
{
    int result;

    if (!timer_ready)
        return 0;
    ST_mutex_lock(&timer_lock);
    result = timer_armed || timer_delivering;
    ST_mutex_unlock(&timer_lock);
    return result;
}

/*
 *  The semaphore a pending delay will signal, as a root.
 *
 *  Held in C and reachable from nowhere in the image once the Delay has
 *  handed it over, so provide_roots visits it.  Deliberately NOT a slot in
 *  st_om_vm_state: that array is written into every snapshot, so adding to
 *  it changes the image format, and a delay armed before a snapshot should
 *  not fire in the image that resumes it hours later anyway.
 */
st_oop
SCHED_timer_semaphore(void)
{
    return timer_semaphore;
}

static void
timer_main(void *arg)
{
    (void) arg;
    ST_mutex_lock(&timer_lock);
    for (;;) {
        if (timer_stopping)
            break;
        if (!timer_armed) {
            ST_cond_wait(&timer_cond, &timer_lock);
            continue;
        }
        {
            int64_t remaining = timer_deadline_ns - ST_time_monotonic_ns();

            if (remaining > 0) {
                /*
                 *  Timed, not indefinite: re-arming while this waits must
                 *  be able to shorten the deadline, and a spurious wake
                 *  costs one trip round the loop.
                 */
                ST_cond_timedwait(&timer_cond, &timer_lock, remaining);
                continue;
            }
        }
        {
            st_oop  semaphore = timer_semaphore;

            timer_armed      = 0;
            timer_semaphore  = ST_NIL;
            timer_delivering = 1;
            /*
             *  Signalled with the lock dropped.  SCHED_asynchronous_signal
             *  takes the async lock, and holding two of this system's locks
             *  at once is how a lock order gets invented by accident.
             */
            ST_mutex_unlock(&timer_lock);
            SCHED_asynchronous_signal(semaphore);
            OM_decrease_ref(semaphore);
            ST_mutex_lock(&timer_lock);
            /*
             *  Cleared here and not before.  A re-arm during the delivery
             *  above sets timer_armed again, so this must not touch it.
             */
            timer_delivering = 0;
        }
    }
    ST_mutex_unlock(&timer_lock);
}

/*
 *  Arm, re-arm or cancel.  A nil semaphore cancels, which is how the image
 *  says `Processor signal: nil atTime: 0'.
 */
void
SCHED_signal_at_ms(st_oop semaphore, uint32_t target_ms)
{
    timer_init();
    /*
     *  The async queue's lock is made HERE, on the thread arming the timer,
     *  and not left to the timer thread that will use it.
     *
     *  async_lock_init is lazy on purpose (see its own note), and lazy is
     *  fine while every caller is a worker.  The timer is not a worker: it
     *  is created below, and if it were the first to post a signal it would
     *  run pthread_mutex_init on a mutex a worker was already locking.  TSAN
     *  reported exactly that.  Arming always precedes the thread that
     *  delivers, so doing it here makes the initialisation strictly
     *  happen-before every use.
     */
    async_lock_init();
    ST_mutex_lock(&timer_lock);
    if (OM_is_present(timer_semaphore))
        OM_decrease_ref(timer_semaphore);
    timer_semaphore = ST_NIL;
    timer_armed     = 0;
    if (OM_is_present(semaphore)) {
        OM_increase_ref(semaphore);
        timer_semaphore   = semaphore;
        timer_deadline_ns = ST_time_monotonic_ns()
                          + milliseconds_until(target_ms) * 1000000;
        timer_armed       = 1;
    }
    ST_cond_broadcast(&timer_cond);
    ST_mutex_unlock(&timer_lock);

    if (!timer_started) {
        timer_started = 1;
        if (ST_thread_create(&timer_thread, timer_main, NULL) != 0) {
            fprintf(stderr, "st80: cannot start the delay timer\n");
            timer_started = 0;
        }
    }
}

void
SCHED_timer_stop(void)
{
    if (!timer_started)
        return;
    ST_mutex_lock(&timer_lock);
    timer_stopping = 1;
    ST_cond_broadcast(&timer_cond);
    ST_mutex_unlock(&timer_lock);
    ST_thread_join(timer_thread);
    timer_started  = 0;
    timer_stopping = 0;
    if (OM_is_present(timer_semaphore))
        OM_decrease_ref(timer_semaphore);
    timer_semaphore = ST_NIL;
    timer_armed     = 0;
}

static st_oop   input_semaphore = ST_NIL;

/*
 *  new_process, new_process_waiting and the active process used to be file
 *  statics, and are now fields of the per-thread interpreter state -- see
 *  st_interp in interp.h for why there rather than in st_worker.  These
 *  spellings keep the code below reading as it did.
 */
#define new_process             (st_vm.new_process)
#define new_process_waiting     (st_vm.new_process_waiting)

/*
 *  ----------  Semaphore stripe locks  ----------
 *
 *  The bug being closed, which has been in this file since the day it was
 *  written and is Chapter 29's own algorithm:
 *
 *      wait    reads excessSignals, sees zero, and THEN queues the process
 *      signal  finds the list empty, and so spends the signal as an excess
 *
 *  Run those on two threads and a signal can land between the read and the
 *  queueing.  It finds the list still empty, increments excessSignals, and
 *  goes; the waiter then queues itself behind a signal that has already
 *  been spent, and waits for ever.  Single-threaded the sequence cannot
 *  interleave, which is why 1983 could write it this way.
 *
 *  The fix is that the test and the act are ONE critical section, and the
 *  lock is chosen by the semaphore's identity hash: sixty-four stripes, so
 *  two unrelated semaphores almost never contend, and no object gets wider
 *  and no header changes.  The hash is already stable across a collection
 *  and across a snapshot, which is what makes it usable as a key at all.
 *
 *  THE RULE, written down once here rather than rediscovered at each lock:
 *
 *      never poll a safepoint while holding a stripe lock.
 *
 *  A worker parked at a safepoint holding one would stop the collector
 *  dead: the collector waits for the worker, the worker waits for the
 *  collector to let it go.  Same hazard om_mt.c already solved by dropping
 *  table_lock before it collects.  Everything under a stripe lock here is
 *  field reads and writes on objects that already exist -- no allocation,
 *  no send, no poll -- and the debug build asserts it.
 */

/*
 *  ----------  The ready lists  ----------
 *
 *  One lock over Processor's quiescentProcessLists, and NOT one queue per
 *  worker -- which is what doc/PLAN-PHARO.md called for, and what reading
 *  the 1983 source talked me out of.
 *
 *  The plan's reasoning was that per-worker queues avoid contention.  What
 *  it did not account for is that the ready lists are not the VM's private
 *  data: ProcessorScheduler>>remove:ifAbsent: and >>suspendFirstAt:ifNone:
 *  walk quiescentProcessLists from SMALLTALK.  Split the lists per worker
 *  and those two methods look in an array that no longer holds the
 *  processes, and answer "not waiting" for a process that is -- silently,
 *  and only when more than one worker is running.
 *
 *  So the lists stay where the image can see them, and the VM's operations
 *  on them are serialized.  The plan's own argument says the cost is
 *  affordable: a green process switch happens at a semaphore wait or a
 *  yield, not per bytecode, so this lock is taken thousands of times a
 *  second rather than millions.  Per-worker queues remain the right
 *  optimisation the day a benchmark says so, and they will want those two
 *  Smalltalk methods reimplemented over a primitive first.
 *
 *  Same rule as the stripes below, for the same reason: nothing under this
 *  lock allocates, sends, or polls a safepoint.
 */
static st_mutex     ready_lock;
static int          ready_lock_ready;

static void
ready_lock_init(void)
{
    if (ready_lock_ready)
        return;
    ST_mutex_init(&ready_lock);
    ready_lock_ready = 1;
}

#define SEMAPHORE_STRIPES   64

static st_mutex     semaphore_stripe[SEMAPHORE_STRIPES];
static int          semaphore_stripes_ready;

#ifndef NDEBUG
static _Thread_local int    stripes_held;

int
SCHED_holding_stripe_lock(void)
{
    return stripes_held > 0;
}
#endif

static void
semaphore_stripes_init(void)
{
    unsigned    i;

    if (semaphore_stripes_ready)
        return;
    for (i = 0; i < SEMAPHORE_STRIPES; ++i)
        ST_mutex_init(&semaphore_stripe[i]);
    semaphore_stripes_ready = 1;
}

static st_mutex *
stripe_for(st_oop semaphore)
{
    semaphore_stripes_init();
    return &semaphore_stripe[OM_identity_hash(semaphore)
                             % SEMAPHORE_STRIPES];
}

static void
stripe_lock(st_mutex *m)
{
    ST_mutex_lock(m);
#ifndef NDEBUG
    ++stripes_held;
#endif
}

static void
stripe_unlock(st_mutex *m)
{
#ifndef NDEBUG
    --stripes_held;
#endif
    ST_mutex_unlock(m);
}

void
SCHED_reset(void)
{
    semaphore_stripes_init();
    async_lock_init();
    ready_lock_init();
    async_count            = 0;
    input_semaphore        = ST_NIL;
    new_process_waiting    = 0;
    new_process            = ST_NIL;
    st_vm.active_process   = ST_NIL;
}

st_oop
SCHED_scheduler(void)
{
    return OM_fetch_pointer(ST_ASSOCIATION_VALUE, ST_SCHEDULER_ASSOCIATION);
}

st_oop
SCHED_active_process(void)
{
    /*
     *  This worker's, if it has one.
     *
     *  Two workers running green processes at once cannot share one
     *  answer, and the image's Processor>>activeProcess is one field.  So
     *  the field becomes the fallback -- what a freshly loaded image says
     *  before any worker has switched, and what a snapshot carries -- and
     *  the authoritative answer is per-thread.  The single-threaded path
     *  reaches the same value by the same route it always did.
     */
    if (OM_is_present(st_vm.active_process))
        return st_vm.active_process;
    return OM_fetch_pointer(ST_SCHEDULER_ACTIVE_PROCESS, SCHED_scheduler());
}

/*  ----------  Linked lists  ----------  */

int
SCHED_is_empty_list(st_oop list)
{
    return OM_fetch_pointer(ST_LIST_FIRST_LINK, list) == ST_NIL;
}

st_oop
SCHED_remove_first_link(st_oop list)
{
    st_oop  first;
    st_oop  last;
    st_oop  next;

    /*
     *  A list whose links are not links.
     *
     *  Chapter 29's removeFirstLinkOf: assumes the chain holds Links,
     *  because in Smalltalk it cannot hold anything else -- the only way in
     *  is addLast:, which is typed by its callers.  Here the fields are raw
     *  memory, so a Semaphore that was never a Semaphore, or a list built by
     *  something that got the layout wrong, writes through field 0 of
     *  whatever it is holding and corrupts the heap somewhere else entirely.
     *
     *  Answering nil instead makes the list look empty, which is what a
     *  malformed one should look like: the signal is remembered as an excess
     *  and nothing is resumed.
     */
    if (!OM_is_present(list) || !OM_pointer_bit(list)
     || OM_fetch_word_length(list) <= ST_LIST_LAST_LINK)
        return ST_NIL;
    first = OM_fetch_pointer(ST_LIST_FIRST_LINK, list);
    if (first == ST_NIL)
        return ST_NIL;
    /*
     *  Pointer objects only.  A byte object -- a String, a Symbol -- has a
     *  word length of its own that says nothing about how many FIELDS it
     *  has, which is none, so the length test alone lets one through and
     *  field 0 is past the end of it.
     */
    if (!OM_is_present(first) || !OM_pointer_bit(first)
     || OM_fetch_word_length(first) <= ST_LINK_NEXT)
        return ST_NIL;

    /*
     *  Hold it before the list lets go.
     *
     *  The list is the only thing referring to a waiting process, so
     *  unlinking it drops the last reference and it is reclaimed before the
     *  caller has seen it -- the scheduler then resumes an object that is no
     *  longer there.  The reference is taken here, where it is still safe to
     *  take, and every caller releases it once it has stored the process
     *  somewhere that holds it.
     */
    OM_increase_ref(first);
    last = OM_fetch_pointer(ST_LIST_LAST_LINK, list);
    next = (first == last) ? ST_NIL
                           : OM_fetch_pointer(ST_LINK_NEXT, first);

    /*
     *  Clear the link's own pointer BEFORE the list lets go of it.
     *
     *  Chapter 29 writes it the other way round -- unlink, then
     *  "link nextLink: nil" -- which is fine where nothing is counting.
     *  Here the list holds the only reference: dropping it first takes the
     *  count to zero, the body is released, and the store that follows lands
     *  in freed memory.  The order below never leaves the link unreferenced
     *  while it is still being written to.
     */
    OM_store_pointer(ST_LINK_NEXT, first, ST_NIL);
    /*
     *  And it is on no list now, which is what myList is for.  Leaving it
     *  set says the process is still queued when it is not, and the guard in
     *  addLastLink: below believes it.
     */
    OM_store_pointer(ST_PROCESS_MY_LIST, first, ST_NIL);
    if (first == last) {
        OM_store_pointer(ST_LIST_FIRST_LINK, list, ST_NIL);
        OM_store_pointer(ST_LIST_LAST_LINK, list, ST_NIL);
    }  else  {
        OM_store_pointer(ST_LIST_FIRST_LINK, list, next);
    }
    return first;
}

void
SCHED_add_last_link(st_oop link, st_oop list)
{
    /*  The same guard from the other side: never chain a non-link.  */
    if (!OM_is_present(link) || !OM_is_present(list)
     || !OM_pointer_bit(link) || !OM_pointer_bit(list)
     || OM_fetch_word_length(link) <= ST_PROCESS_MY_LIST
     || OM_fetch_word_length(list) <= ST_LIST_LAST_LINK)
        return;
    /*
     *  A process already on a list is not put on another.
     *
     *  Chaining one twice makes its nextLink point at itself, and from then
     *  on the list either loops forever or ends early depending on which end
     *  is walked.  It is also how a process comes to be both running and
     *  queued, so that suspending it hands control back to itself: the
     *  scheduler transfers to the process it just left, execution carries on
     *  from where it was, and a terminating process returns from the
     *  terminate it was never supposed to return from.
     */
    if (OM_is_present(OM_fetch_pointer(ST_PROCESS_MY_LIST, link)))
        return;
    if (SCHED_is_empty_list(list))
        OM_store_pointer(ST_LIST_FIRST_LINK, list, link);
    else
        OM_store_pointer(ST_LINK_NEXT,
                         OM_fetch_pointer(ST_LIST_LAST_LINK, list), link);
    OM_store_pointer(ST_LIST_LAST_LINK, list, link);
    OM_store_pointer(ST_PROCESS_MY_LIST, link, list);
}

/*  ----------  Scheduling  ----------  */

/*
 *  Put a process on the run queue for its priority.  Priorities are
 *  one-relative and index quiescentProcessLists.
 */
void
SCHED_sleep(st_oop process)
{
    st_oop      priority = OM_fetch_pointer(ST_PROCESS_PRIORITY, process);
    st_oop      lists;
    st_oop      list;

    if (!OM_is_int(priority))
        return;
    lists = OM_fetch_pointer(ST_SCHEDULER_PROCESS_LISTS, SCHED_scheduler());
    if (!OM_is_present(lists))
        return;
    if (OM_int_value(priority) < 1
     || (uint32_t) OM_int_value(priority) > OM_fetch_word_length(lists))
        return;
    list = OM_fetch_pointer((uint32_t) OM_int_value(priority) - 1, lists);
    /*
     *  Under the same lock the take is under, so that a process being
     *  queued and a process being taken cannot interleave halfway through
     *  the four field writes that put a link on a list.
     */
    ready_lock_init();
    ST_mutex_lock(&ready_lock);
    SCHED_add_last_link(process, list);
    ST_mutex_unlock(&ready_lock);
}

/*
 *  Nominate the process to run when the interpreter next reaches a point
 *  where it can switch.
 *
 *  The nomination is held in C, so it is counted here and visited by the
 *  root walk -- a reference held only in C protects nothing, because a
 *  marking collection recomputes every count from the roots.  Without the
 *  count the process nominated by removeFirstLinkOf: belongs to nobody at
 *  all between the two, and is reclaimed before it ever runs.
 */
void
SCHED_transfer_to(st_oop process)
{
    OM_increase_ref(process);
    OM_decrease_ref(new_process);       /*  any nomination this supersedes  */
    new_process_waiting = 1;
    new_process         = process;
}

st_oop
SCHED_pending_process(void)
{
    return new_process;
}

/*
 *  Find the highest-priority runnable process.  Scanning from the top is
 *  what makes priorities preemptive between levels.
 */
st_oop
SCHED_wake_highest_priority(void)
{
    st_oop      lists = OM_fetch_pointer(ST_SCHEDULER_PROCESS_LISTS,
                                         SCHED_scheduler());
    st_oop      found = ST_NIL;
    uint32_t    count;
    uint32_t    i;

    if (!OM_is_present(lists))
        return ST_NIL;
    /*
     *  Finding the highest non-empty list and taking from it is ONE step.
     *  Two workers that both look, both see the same process at the head,
     *  and both take it end up running it twice -- on two native threads,
     *  through one context.
     */
    ready_lock_init();
    ST_mutex_lock(&ready_lock);
    count = OM_fetch_word_length(lists);
    for (i = count; i > 0; --i) {
        st_oop  list = OM_fetch_pointer(i - 1, lists);

        if (OM_is_object(list) && !SCHED_is_empty_list(list)) {
            found = SCHED_remove_first_link(list);
            break;
        }
    }
    ST_mutex_unlock(&ready_lock);
    return found;
}

/*
 *  How long a worker with nothing to run waits before calling it a
 *  deadlock, and how finely it looks while it waits.
 *
 *  A tenth of a second is far longer than any critical section this system
 *  has, and far shorter than a human notices at the end of a genuinely
 *  wedged run.
 */
#define IDLE_WAIT_SLICE_NS  INT64_C(100000)     /*  0.1 ms  */
/*
 *  A backstop, not the criterion.  Five minutes is longer than any real
 *  wait and short enough that a wedged run still ends.
 */
#define IDLE_WAIT_SLICES    3000000
/*
 *  How many consecutive looks must agree that every worker is idle.  A
 *  hundred of them is ten milliseconds of a genuinely still system.
 */
#define ALL_IDLE_CONFIRMATIONS  100

/*
 *  How many workers are sitting in the wait below with nothing to run.
 *
 *  This is the honest test for deadlock, and a timeout is not.  A worker
 *  cannot know whether the lock it wants will be released in a microsecond
 *  or never, so any deadline it picks is wrong in one direction or the
 *  other -- one second was long enough on bare metal and far too short
 *  under the thread sanitizer, where thirty-one workers ran ten times
 *  slower and thirty of them gave up on a queue that was about to be
 *  filled.
 *
 *  But if EVERY worker is in here at once, no worker is running Smalltalk,
 *  so nothing can ever make anything ready again.  That is a deadlock by
 *  construction rather than by guess, and it is true at any speed.
 */
static st_atomic_int    idle_workers;

/*
 *  Set between parking a process and switching away from it.
 *
 *  Distinguishes "this worker deliberately gave its process up" from "this
 *  worker never had one".  They look identical through
 *  SCHED_active_process, which falls back to the scheduler's shared field
 *  -- and the difference matters exactly once: the switch below must park
 *  the context of a process this worker still owns, and must NOT park over
 *  one it has handed to somebody else.
 */
static _Thread_local int    disowned;

void
SCHED_suspend_active(void)
{
    st_oop  next;

    /*
     *  Park this process BEFORE looking for another, and then disown it.
     *
     *  By the time anything calls this, the process is already on the list
     *  it is waiting on -- SCHED_primitive_wait puts it there first, and
     *  says in its own comment that a signal arriving in the gap "finds it
     *  and resumes it, which is the ordinary case, not a race".  It is a
     *  race, and this is where it lives: the process is reachable and
     *  resumable while its suspendedContext still points at wherever it
     *  was LAST parked, and this thread is still executing the real
     *  registers.  Another worker that resumes it in that window runs a
     *  stale context, and two native threads end up inside one context.
     *  ASAN finds it as a corrupt temp frame under pushRemoteTemp.
     *
     *  The window used to be the width of one list operation, so it almost
     *  never happened.  Waiting here for work made it a second wide and
     *  therefore certain, which is how it was finally seen.
     *
     *  Parking first closes it.  Disowning matters just as much: once the
     *  process is resumable it belongs to whoever takes it, and the switch
     *  below must not write this thread's stale context over the progress
     *  that thread has since made.
     */
    {
        st_oop  self = SCHED_active_process();

        if (OM_is_object(self)) {
            ST_store_active_context();
            OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, self,
                             st_vm.active_context);
            disowned = 1;
        }
    }

    next = SCHED_wake_highest_priority();

    /*
     *  Nothing ready HERE is not the same as nothing ready ANYWHERE.
     *
     *  With one thread it was: no runnable process meant the system was
     *  wedged, and saying so was right.  With a worker pool it is wrong,
     *  and wrong in the way that looks like a library bug.  A worker whose
     *  green process blocks on a Mutex held by another worker finds its own
     *  ready list empty, declares deadlock, and stops -- while the worker
     *  holding the lock is a microsecond from releasing it.  Eight workers
     *  contending for one Mutex printed this fourteen times in a run that
     *  should have printed it never, and seven of the eight returned no
     *  answer at all.
     *
     *  So while any other worker is still running, wait and look again.
     *  The safepoint is polled each time round: a collection must be able
     *  to happen while this worker idles, or the collector waits for a
     *  worker that is waiting for the collector.
     */
    if (next == ST_NIL && WORKER_count() > 1) {
        unsigned    slice;

        unsigned    all_idle = 0;

        ST_fetch_add_relaxed(&idle_workers, 1);
        for (slice = 0; slice < IDLE_WAIT_SLICES; ++slice) {
            /*
             *  Every worker idle at once means nobody is left to make
             *  anything ready.  Checked before sleeping, so the last
             *  worker to arrive is the one that notices.
             */
            /*
             *  Every worker idle at once means nobody is left to make
             *  anything ready.  But it has to STAY true: a worker that has
             *  just been signalled and has not yet been picked up leaves
             *  everyone briefly idle with work already in flight, and
             *  believing that instant costs exactly one process.
             */
            if (ST_load_seq(&idle_workers) >= (int) WORKER_count()
             && !SCHED_timer_pending()) {
                if (++all_idle >= ALL_IDLE_CONFIRMATIONS)
                    break;
            }  else {
                all_idle = 0;
            }
            WORKER_poll();
            ST_sleep_ns(IDLE_WAIT_SLICE_NS);
            drain_async_signals();
            next = SCHED_wake_highest_priority();
            if (next != ST_NIL)
                break;
        }
        ST_fetch_sub_relaxed(&idle_workers, 1);
    }

    /*
     *  A delay outstanding is not a deadlock, it is a wait -- and with one
     *  worker the loop above does not run at all, so this is the only place
     *  that notices.  Sleep in slices rather than on the timer's condvar:
     *  this thread must keep polling safepoints, and a thread asleep on
     *  another subsystem's condvar is a thread the collector waits for.
     */
    while (next == ST_NIL && !new_process_waiting && SCHED_timer_pending()) {
        WORKER_poll();
        ST_sleep_ns(IDLE_WAIT_SLICE_NS);
        drain_async_signals();
        next = SCHED_wake_highest_priority();
    }
    if (next == ST_NIL && !new_process_waiting) {
        /*  One last look: the timer may have fired as the loop gave up.  */
        drain_async_signals();
        next = SCHED_wake_highest_priority();
    }

    /*
     *  A nomination is not an empty run queue, and reading it as one is how
     *  a woken process was lost.
     *
     *  Signalling a semaphore resumes whoever waited on it, and SCHED_resume
     *  does not queue a process that outranks the running one -- it NOMINATES
     *  it, leaving the ready lists empty on purpose so the switch happens at
     *  the top of the interpreter's loop.  Delay's timing process runs at
     *  priority 8 and every delay is awaited from a lower one, so every
     *  single delay took this path: the semaphore was signalled, the right
     *  process was chosen to run next, and this function looked at the empty
     *  ready lists and announced that the image was deadlocked.
     *
     *  Nothing more is needed here -- the nomination IS the answer, and
     *  returning lets check_process_switch act on it.
     */
    if (new_process_waiting)
        return;

    if (next == ST_NIL) {
        fprintf(stderr, "st80: every process is blocked; nothing can run\n");
        st_vm.running = 0;
        return;
    }
    SCHED_transfer_to(next);
    /*  The nomination holds it now; this releases the loan from removal.  */
    OM_decrease_ref(next);
}

/*
 *  Resuming a higher-priority process preempts the active one; resuming a
 *  lower or equal one merely queues it.  Under the green scheduler this is
 *  the whole of the preemption rule.
 */
/*
 *  The process that will be running, which is not always the one that is.
 *
 *  A transfer only NOMINATES; the switch happens when the interpreter next
 *  reaches the top of its loop.  Until then activeProcess still names the
 *  process being left, and scheduling against it gets the wrong answer as
 *  soon as two things want to preempt before a single bytecode boundary --
 *  the second displaces a process that has already been displaced and puts
 *  it on a run queue a second time.
 *
 *  Moving the mouse does exactly that.  Every motion posts an X event and a
 *  Y event, each signalling the input semaphore, and both are drained in one
 *  pass before any bytecode runs.
 */
static st_oop
effective_active_process(void)
{
    return new_process_waiting ? new_process : SCHED_active_process();
}

void
SCHED_resume(st_oop process)
{
    st_oop  active = effective_active_process();
    st_oop  active_priority;
    st_oop  new_priority;

    if (!OM_is_present(process))
        return;
    active_priority = OM_fetch_pointer(ST_PROCESS_PRIORITY, active);
    new_priority    = OM_fetch_pointer(ST_PROCESS_PRIORITY, process);
    if (!OM_is_int(active_priority) || !OM_is_int(new_priority)) {
        SCHED_sleep(process);
        return;
    }
    if (OM_int_value(new_priority) > OM_int_value(active_priority)) {
        SCHED_sleep(active);
        SCHED_transfer_to(process);
    }  else  {
        SCHED_sleep(process);
    }
}

void
SCHED_synchronous_signal(st_oop semaphore)
{
    st_oop  excess;

    if (!OM_is_present(semaphore))
        return;
    if (SCHED_is_empty_list(semaphore)) {
        /*  Nobody is waiting, so the signal is remembered.  */
        excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore);
        if (OM_is_int(excess))
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) + 1));
        return;
    }
    {
        st_oop  woken = SCHED_remove_first_link(semaphore);

        SCHED_resume(woken);
        /*  A list or the nomination holds it; release the removal's loan.  */
        OM_decrease_ref(woken);
    }
}

void
SCHED_asynchronous_signal(st_oop semaphore)
{
    if (!OM_is_present(semaphore))
        return;
    async_lock_init();
    ST_mutex_lock(&async_lock);
    {
        int n = ST_load_relaxed(&async_count);

        if (n < ASYNC_QUEUE_MAX) {
            async_queue[n] = semaphore;
            ST_store_release(&async_count, n + 1);
        }
        /*  else drop rather than corrupt, as before  */
    }
    ST_mutex_unlock(&async_lock);
}

/*
 *  Turn signals posted by other threads into ready processes.
 *
 *  Called from the interpreter's loop and again from the scheduler's idle
 *  wait, because those are two different moments: the loop runs when a
 *  process is running, and the wait runs when none is.  A delay expiring
 *  is precisely the second case, so draining only in the first left the
 *  timer's signal sitting in the queue while the scheduler concluded that
 *  nothing could ever run.
 */
static void
drain_async_signals(void)
{
    if (ST_load_acquire(&async_count) > 0) {
        st_oop      pending[ASYNC_QUEUE_MAX];
        int         count;
        int         i;

        ST_mutex_lock(&async_lock);
        count = ST_load_relaxed(&async_count);
        for (i = 0; i < count; ++i)
            pending[i] = async_queue[i];
        ST_store_relaxed(&async_count, 0);
        ST_mutex_unlock(&async_lock);

        for (i = 0; i < count; ++i)
            SCHED_synchronous_signal(pending[i]);
    }
}

void
SCHED_check_process_switch(void)
{
    /*
     *  Drain into a local copy and signal outside the lock.  Signalling
     *  can resume a process, which can transfer, which polls a safepoint --
     *  and holding a lock across a safepoint poll is the deadlock the
     *  stripe-lock rule above exists to forbid.  The same reasoning
     *  applies here and to every lock this system will ever add.
     */
    drain_async_signals();
    if (!new_process_waiting)
        return;
    new_process_waiting = 0;

    /*
     *  Park the running process's context in its Process object, then make
     *  the incoming one's context active.  Everything the old process needs
     *  to resume is in that one pointer.
     */
    ST_store_active_context();
    /*
     *  Unless this worker has already parked and handed the process on.
     *  Parking again would write this thread's stale context over whatever
     *  progress the worker that took it has since made.
     */
    if (!disowned)
        OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, SCHED_active_process(),
                         st_vm.active_context);
    disowned = 0;
    /*
     *  The image's field as well as this worker's, because a snapshot
     *  carries the field and the image's own reflection reads it.  With
     *  several workers running processes it holds whichever switched last,
     *  which is the honest answer to a question that no longer has one --
     *  and is why Processor>>activeProcess becomes a primitive that asks
     *  the calling worker instead.
     */
    st_vm.active_process = new_process;
    OM_store_pointer(ST_SCHEDULER_ACTIVE_PROCESS, SCHED_scheduler(),
                     new_process);
    ST_set_active_context(
        OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT, new_process));
    /*  activeProcess holds it now; this releases the nomination's count.  */
    OM_decrease_ref(new_process);
    new_process = ST_NIL;
}

/*
 *  ----------  What Smalltalk used to do to the ready lists itself  --------
 *
 *  ProcessorScheduler>>remove:ifAbsent: and >>suspendFirstAt:ifNone: walked
 *  quiescentProcessLists from Smalltalk, field by field, with no lock and
 *  no idea that another worker might be walking the same chain.  These two
 *  do the same jobs inside the VM, under the ready lock that every other
 *  list operation takes.
 *
 *  That closes a hole, and it also unblocks something: while those methods
 *  read the array directly, the ready lists cannot be split per worker,
 *  because a split array is not the array they are reading.  Asking the VM
 *  instead means the VM can keep the processes wherever it likes.
 */

/*
 *  Unlink a process from the middle of a list.
 *
 *  Chapter 29 has no such operation -- 1983 only ever took from the head --
 *  so the walk is here.  Answers 1 if it was found and removed.
 */
static int
remove_link_from_list(st_oop link, st_oop list)
{
    st_oop  previous = ST_NIL;
    st_oop  current;

    if (!OM_is_present(link) || !OM_is_present(list))
        return 0;
    current = OM_fetch_pointer(ST_LIST_FIRST_LINK, list);
    while (OM_is_present(current) && current != link) {
        previous = current;
        current  = OM_fetch_pointer(ST_LINK_NEXT, current);
    }
    if (current != link)
        return 0;
    {
        st_oop  next = OM_fetch_pointer(ST_LINK_NEXT, link);
        st_oop  last = OM_fetch_pointer(ST_LIST_LAST_LINK, list);

        /*
         *  The link is counted up first and released by the caller, and
         *  its own pointers are cleared before the list lets go -- the
         *  same discipline removeFirstLink follows, and for the same
         *  reason: the list may hold the only reference, and a store into
         *  a body that has just been released lands in freed memory.
         */
        OM_increase_ref(link);
        OM_store_pointer(ST_LINK_NEXT, link, ST_NIL);
        OM_store_pointer(ST_PROCESS_MY_LIST, link, ST_NIL);
        if (OM_is_present(previous))
            OM_store_pointer(ST_LINK_NEXT, previous, next);
        else
            OM_store_pointer(ST_LIST_FIRST_LINK, list, next);
        if (link == last)
            OM_store_pointer(ST_LIST_LAST_LINK, list,
                             OM_is_present(previous) ? previous : ST_NIL);
        OM_decrease_ref(link);
    }
    return 1;
}

/*  The ready list a process of this priority waits on, or nil.  */
static st_oop
ready_list_at(st_int priority)
{
    st_oop  lists = OM_fetch_pointer(ST_SCHEDULER_PROCESS_LISTS,
                                     SCHED_scheduler());

    if (!OM_is_present(lists) || priority < 1
     || (uint32_t) priority > OM_fetch_word_length(lists))
        return ST_NIL;
    return OM_fetch_pointer((uint32_t) priority - 1, lists);
}

int
SCHED_remove_ready_process(st_oop process)
{
    st_oop  priority;
    st_oop  list;
    int     removed;

    if (!OM_is_present(process) || !OM_pointer_bit(process)
     || OM_fetch_word_length(process) <= ST_PROCESS_MY_LIST)
        return 0;
    priority = OM_fetch_pointer(ST_PROCESS_PRIORITY, process);
    if (!OM_is_int(priority))
        return 0;

    ready_lock_init();
    ST_mutex_lock(&ready_lock);
    list = ready_list_at(OM_int_value(priority));
    /*
     *  Only the ready list at its own priority, which is what the 1983
     *  method did: a process waiting on a SEMAPHORE is not "waiting for
     *  the processor", and quietly taking it off the semaphore's list
     *  would lose the signal it is waiting for.
     */
    removed = OM_is_present(list)
           && OM_fetch_pointer(ST_PROCESS_MY_LIST, process) == list
           && remove_link_from_list(process, list);
    ST_mutex_unlock(&ready_lock);
    return removed;
}

st_oop
SCHED_first_ready_process_at(st_int priority)
{
    st_oop  list;
    st_oop  first = ST_NIL;

    ready_lock_init();
    ST_mutex_lock(&ready_lock);
    list = ready_list_at(priority);
    if (OM_is_present(list))
        first = OM_fetch_pointer(ST_LIST_FIRST_LINK, list);
    ST_mutex_unlock(&ready_lock);
    return first;
}

/*  ----------  Primitives 85 to 88  ----------  */

int
SCHED_primitive_signal(void)
{
    st_oop      semaphore = ST_stack_top();
    st_mutex   *lock;
    st_oop      woken;

    if (!OM_is_object(semaphore))
        return 0;
    if (OM_fetch_class(semaphore) != ST_CLASS_SEMAPHORE)
        return 0;

    /*
     *  Under the stripe: deciding whether anyone is waiting and acting on
     *  the answer are one step.  Waking the process is NOT under it --
     *  SCHED_resume can transfer, which polls -- so the lock decides and
     *  releases, and the wake happens after.
     */
    lock = stripe_for(semaphore);
    stripe_lock(lock);
    if (SCHED_is_empty_list(semaphore)) {
        st_oop  excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS,
                                          semaphore);

        if (OM_is_int(excess))
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) + 1));
        stripe_unlock(lock);
        return 1;
    }
    woken = SCHED_remove_first_link(semaphore);
    stripe_unlock(lock);

    if (OM_is_present(woken)) {
        SCHED_resume(woken);
        /*  A list or the nomination holds it; release the removal's loan. */
        OM_decrease_ref(woken);
    }
    return 1;               /*  answers the receiver, already on the stack  */
}

int
SCHED_primitive_wait(void)
{
    st_oop  semaphore = ST_stack_top();
    st_oop  excess;

    if (!OM_is_object(semaphore))
        return 0;
    if (OM_fetch_class(semaphore) != ST_CLASS_SEMAPHORE)
        return 0;
    /*
     *  The whole of the fix.  Reading excessSignals and either spending it
     *  or queueing behind it is one critical section, so a signal cannot
     *  land in the middle and be spent on a list that is about to stop
     *  being empty.
     *
     *  Suspending is outside it, because suspending transfers to another
     *  process and that polls.  By then the process is already on the
     *  semaphore's list, so a signal arriving in the gap finds it and
     *  resumes it -- which is the ordinary case, not a race.
     */
    {
        st_mutex   *lock = stripe_for(semaphore);
        int         must_wait;

        stripe_lock(lock);
        excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore);
        if (!OM_is_int(excess)) {
            stripe_unlock(lock);
            return 0;
        }
        must_wait = OM_int_value(excess) <= 0;
        if (must_wait)
            SCHED_add_last_link(SCHED_active_process(), semaphore);
        else
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) - 1));
        stripe_unlock(lock);
        if (!must_wait)
            return 1;
    }
    SCHED_suspend_active();
    return 1;
}

int
SCHED_primitive_resume(void)
{
    st_oop  process = ST_stack_top();

    if (!OM_is_object(process))
        return 0;
    SCHED_resume(process);
    return 1;
}

int
SCHED_primitive_suspend(void)
{
    st_oop  process = ST_stack_top();

    if (process != SCHED_active_process())
        return 0;               /*  only the active process may suspend  */
    ST_pop_n(1);
    ST_push(ST_NIL);
    SCHED_suspend_active();
    return 1;
}

/*  ----------  Input  ----------  */

void
SCHED_set_input_semaphore(st_oop semaphore)
{
    /*  Counted, because provide_roots visits it as a root.  */
    OM_increase_ref(semaphore);
    OM_decrease_ref(input_semaphore);
    input_semaphore = semaphore;
    /*  Published so a snapshot carries it -- see om.h.  */
    st_om_vm_state[ST_VM_INPUT_SEMAPHORE] = semaphore;
}

st_oop
SCHED_input_semaphore(void)
{
    return input_semaphore;
}
