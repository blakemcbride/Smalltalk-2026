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
/*
 *  1024 and not 64 since the network arrived.  A keystroke or a timer is
 *  one signal; a server with a thousand connections waiting is a thousand
 *  Semaphores the I/O thread may signal in one pass of poll(), and a full
 *  queue DROPS the signal -- which for a socket is a request that never
 *  wakes.  The I/O thread is told when that happens and tries again on its
 *  next pass, so the size is about how often it has to, not correctness;
 *  the drain's stack copy is eight kilobytes, which a worker affords.
 */
#define ASYNC_QUEUE_MAX     1024

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

/*
 *  The same, for a caller about to start a thread that will post signals
 *  -- the network I/O thread -- so that the mutex exists before the thread
 *  does, on the thread that creates it.  The delay timer learnt this the
 *  hard way (see SCHED_signal_at_ms); a second thread must not learn it
 *  again.
 */
void
SCHED_async_init(void)
{
    async_lock_init();
}

/*
 *  ----------  Stopping, from outside  ----------
 *
 *  A server ends on a signal from the operating system, which arrives on
 *  no particular thread and inside no particular bytecode.  The handler
 *  may do almost nothing, so it does exactly one thing: sets this.  Every
 *  worker sees it at its next process-switch check or its next idle slice
 *  and lets its interpreter loop fall out, which is how the pool winds
 *  down without anybody being interrupted mid-object.
 */
static st_atomic_int    stop_requested;

void
SCHED_request_stop(void)
{
    ST_store_seq(&stop_requested, 1);
}

int
SCHED_stop_requested(void)
{
    return ST_load_relaxed(&stop_requested) != 0;
}

/*
 *  ----------  Waits the scheduler cannot see  ----------
 *
 *  The idle loop below decides that nothing can ever run again when every
 *  worker is idle and no delay is armed.  A socket somebody is parked on
 *  is a wait of the same kind as a delay: something outside the scheduler
 *  will end it.  The network layer registers a hook that says whether any
 *  such wait is outstanding, and the loop asks it beside the timer.  A
 *  hook rather than a call, so that this file knows nothing about sockets.
 */
static int  (*external_wait_hook)(void);

void
SCHED_set_external_wait_hook(int (*hook)(void))
{
    external_wait_hook = hook;
}

static int
waits_pending(void)
{
    if (SCHED_timer_pending())
        return 1;
    return external_wait_hook && external_wait_hook();
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
static st_atomic_int timer_ready;           /*  0 not yet, 1 in progress, 2 ready  */
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
    int     expected = 0;

    /*
     *  Once, whoever arrives first, and the rest wait for it: the first
     *  arming worker used to write the flag plainly while idle workers read
     *  it in SCHED_timer_pending, and two workers arming at once would both
     *  have initialised the lock.
     */
    if (ST_load_acquire(&timer_ready) == 2)
        return;
    if (ST_cas_strong(&timer_ready, &expected, 1)) {
        ST_mutex_init(&timer_lock);
        ST_cond_init(&timer_cond);
        ST_store_release(&timer_ready, 2);
        return;
    }
    while (ST_load_acquire(&timer_ready) != 2)
        ST_sleep_ns(1000);
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

    if (ST_load_acquire(&timer_ready) != 2)
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
    st_oop  semaphore;

    /*
     *  Under the timer's lock, because the timer thread writes this field
     *  under that lock and the root walk reads it from a worker at a
     *  safepoint -- which parks every worker and not the timer thread.
     *  ThreadSanitizer saw the two meet under a gate that collects while
     *  delays are pending; a torn read cannot happen on this platform,
     *  but a stale one could visit a semaphore the timer had already let
     *  go of, or miss the one it had just taken.
     */
    if (ST_load_acquire(&timer_ready) != 2)
        return ST_NIL;
    ST_mutex_lock(&timer_lock);
    semaphore = timer_semaphore;
    ST_mutex_unlock(&timer_lock);
    return semaphore;
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
 *  What to do while nothing can run.
 *
 *  Both waits below sleep in tenth-of-a-millisecond slices rather than on a
 *  condvar, so there is a natural place to let the host breathe, and the
 *  host needs it: an interpreter idling on a Delay does not come back out
 *  of ST_interp_run, so a window driven from that call's return sees
 *  nothing for the whole of the wait.  With DisplayScreen>>flash: waiting
 *  60 ms between its two reverses, that is the difference between a screen
 *  that shows both halves and one that shows whichever half it happened to
 *  catch.
 *
 *  Left null for every headless run, which is every test.
 */
static void     (*idle_hook)(void);

void
SCHED_set_idle_hook(void (*hook)(void))
{
    idle_hook = hook;
}

/*
 *  new_process, new_process_waiting and the active process used to be file
 *  statics, and are now fields of the per-thread interpreter state -- see
 *  st_interp in interp.h for why there rather than in st_worker.  These
 *  spellings keep the code below reading as it did.
 */
#define new_process             (st_vm.new_process)
#define new_process_waiting     (st_vm.new_process_waiting)

/*
 *  ----------  What each worker has in its hands  ----------
 *
 *  Bugs3 B16 and B17: `terminate', `suspend' and `signalException:' of a
 *  process running on ANOTHER worker did nothing, and `Processor
 *  activeProcess resume' handed the running process to a second worker.
 *  Both come from the same gap: the state above -- active_process,
 *  new_process, disowned -- is this worker's own, read and written
 *  plainly, and no other worker can ask "is that process running
 *  somewhere?" without a data race.  Chapter 29 never needed the
 *  question; with one thread a process that is not the active process is
 *  on a list or suspended, and there is nothing else it can be.
 *
 *  So every worker publishes, in a row of this table, the processes it
 *  has in its hands and nowhere else:
 *
 *      held      the process it is executing, or has parked and not yet
 *                LANDED -- put on a list, or deliberately set free
 *      nominee   the process it will switch to at its next check
 *      taken     a process it has taken off a list and not yet landed
 *
 *  Three words that only the owning worker writes, with release, and that
 *  any worker reads, with acquire.  The discipline that makes them exact:
 *  a process is written into the slot it is moving TO before it is
 *  cleared from the one it is moving FROM (taken before nominee before
 *  held), and it is cleared only once it has landed -- `land' below runs
 *  after SCHED_add_last_link, not before.  A reader that sees no slot
 *  naming a process on any worker, and no list holding it, has therefore
 *  seen it free, provided nothing can make it un-free behind the reader's
 *  back.  The detach table further down is what provides that.
 *
 *  A STATIC table, and not three fields of st_interp read through the
 *  registry, because the registry is read safely only at a safepoint: a
 *  worker that leaves the pool takes its thread-locals with it, and a
 *  scanner that had just loaded the pointer would read freed memory.
 *  A row here outlives every thread; the worst a scanner can read is a
 *  stale nil.
 */
typedef struct {
    st_atomic_ptr   held;
    st_atomic_ptr   nominee;
    st_atomic_ptr   taken;
} st_hands;

static st_hands     hands[ST_MAX_INTERPRETERS];

/*  This worker's row, or NULL for a thread that never registered.  */
static st_hands *
my_hands(void)
{
    return st_vm.hands_slot ? &hands[st_vm.hands_slot - 1] : NULL;
}

static void
clear_hands(st_hands *h)
{
    ST_store_release(&h->held,    (uintptr_t) ST_OOP_INVALID);
    ST_store_release(&h->nominee, (uintptr_t) ST_OOP_INVALID);
    ST_store_release(&h->taken,   (uintptr_t) ST_OOP_INVALID);
}

void
SCHED_hands_register(unsigned slot)
{
    if (slot >= ST_MAX_INTERPRETERS)
        return;
    clear_hands(&hands[slot]);
    st_vm.hands_slot = slot + 1;
}

void
SCHED_hands_unregister(void)
{
    st_hands   *h = my_hands();

    if (h)
        clear_hands(h);
    st_vm.hands_slot = 0;
}

static void
publish(st_atomic_ptr *slot, st_oop process)
{
    ST_store_release(slot, (uintptr_t) process);
}

/*
 *  The process has arrived where it will wait -- on a list, or free on
 *  purpose -- and this worker no longer answers for it.
 */
static void
land(st_oop process)
{
    st_hands   *h = my_hands();

    if (!h)
        return;
    if ((st_oop) ST_load_relaxed(&h->taken) == process)
        publish(&h->taken, ST_OOP_INVALID);
    if ((st_oop) ST_load_relaxed(&h->nominee) == process)
        publish(&h->nominee, ST_OOP_INVALID);
    if ((st_oop) ST_load_relaxed(&h->held) == process)
        publish(&h->held, ST_OOP_INVALID);
}

/*  Does any worker -- this one included -- have the process in hand?  */
static int
in_anyones_hands(st_oop process)
{
    unsigned    i;

    if (!OM_is_present(process))
        return 0;
    for (i = 0; i < ST_MAX_INTERPRETERS; ++i) {
        st_hands   *h = &hands[i];

        /*
         *  In the order the slots are cleared -- taken, nominee, held --
         *  so that a process moving between two of them on one worker is
         *  seen in at least one: it is written into the next before it
         *  is cleared from the last.
         */
        if ((st_oop) ST_load_acquire(&h->taken)   == process
         || (st_oop) ST_load_acquire(&h->nominee) == process
         || (st_oop) ST_load_acquire(&h->held)    == process)
            return 1;
    }
    return 0;
}

/*
 *  ----------  Processes that must not run  ----------
 *
 *  The other half of B16.  Knowing where a process is does not let
 *  another worker stop it: its registers are in that worker's
 *  interpreter, and only that worker can park them.  So a worker wanting
 *  a process stopped NAMES it here, and every worker honours the table
 *  at the three places a process can start or continue running:
 *
 *      SCHED_check_process_switch   a worker executing a named process
 *                                   parks it onto its ready list at its
 *                                   next bytecode boundary and goes to
 *                                   find something else; a named nominee
 *                                   is released to its ready list rather
 *                                   than switched to
 *      take_first_runnable          a named process is left on whatever
 *                                   list it waits on; the signal or the
 *                                   idle worker takes the next one
 *      SCHED_primitive_resume       a named process is refused
 *
 *  Between them a named process can only move TOWARDS being free: from a
 *  worker's hands to a list, never from a list or from freedom into
 *  anyone's hands.  That is what lets SCHED_detach, which names a process
 *  and then looks, conclude anything from what it sees.
 *
 *  Lock-free, and small: an entry is an oop stored with a compare-and-
 *  swap into an empty slot, cleared with a store, and the count beside
 *  the table is what every hot-path reader checks first -- one relaxed
 *  load, zero for the whole life of an image that never detaches
 *  anything.  A stale zero on that load is harmless: the requester's
 *  own list operations take the same locks the takers do, and a taker
 *  that took the process before the name landed shows it in `taken'.
 *
 *  Sixty-four entries, one per worker at most, since a worker names one
 *  process at a time and waits for it.  A full table makes the requester
 *  wait for a slot rather than proceed without one, because proceeding
 *  without one is the old behaviour -- a terminate that terminates
 *  nothing.
 */
#define DETACH_MAX          ST_MAX_INTERPRETERS

static st_atomic_ptr    detach_table[DETACH_MAX];
static st_atomic_int    detach_count;

/*
 *  And the same rule applied to EVERY process at once, for a snapshot
 *  (B9): while frozen, no worker switches to anything, and every worker
 *  parks what it runs onto its ready list and idles, so that the image
 *  written meanwhile holds every process on a list that a reloaded
 *  worker can take it from.  Signals are still delivered during a
 *  freeze -- a waiter taken off a semaphore lands on its ready list --
 *  because a signal spent as an excess while its waiter sits on the list
 *  would leave a semaphore that never wakes.
 */
static st_atomic_int    frozen;

static int
is_named(st_oop process)
{
    unsigned    i;

    if (ST_load_relaxed(&detach_count) == 0)
        return 0;
    for (i = 0; i < DETACH_MAX; ++i)
        if ((st_oop) ST_load_acquire(&detach_table[i]) == process)
            return 1;
    return 0;
}

static int
must_not_run(st_oop process)
{
    if (!OM_is_present(process))
        return 0;
    return ST_load_relaxed(&frozen) || is_named(process);
}

/*  Whether either rule is in force; the one load the hot path pays.  */
static int
attention_wanted(void)
{
    return ST_load_relaxed(&frozen) || ST_load_relaxed(&detach_count);
}

static int
name_process(st_oop process)
{
    unsigned    i;

    for (i = 0; i < DETACH_MAX; ++i) {
        uintptr_t   empty = (uintptr_t) ST_OOP_INVALID;

        if (ST_cas_strong(&detach_table[i], &empty, (uintptr_t) process)) {
            ST_fetch_add_acq_rel(&detach_count, 1);
            return 1;
        }
    }
    return 0;
}

static void
unname_process(st_oop process)
{
    unsigned    i;

    for (i = 0; i < DETACH_MAX; ++i) {
        uintptr_t   mine = (uintptr_t) process;

        if (ST_cas_strong(&detach_table[i], &mine, (uintptr_t) ST_OOP_INVALID)) {
            ST_fetch_sub_acq_rel(&detach_count, 1);
            return;
        }
    }
}

static int  remove_link_from_list(st_oop link, st_oop list);

/*
 *  The first process on the list that is allowed to run, taken off it
 *  and held; or nil.  SCHED_remove_first_link when nothing is named,
 *  which is always, and a walk past the named ones otherwise.  Under the
 *  list's lock, like every caller of SCHED_remove_first_link.
 */
static st_oop
take_first_runnable(st_oop list)
{
    st_oop  link;

    if (ST_load_relaxed(&detach_count) == 0)
        return SCHED_remove_first_link(list);
    if (!OM_is_present(list) || !OM_pointer_bit(list)
     || OM_fetch_word_length(list) <= ST_LIST_LAST_LINK)
        return ST_NIL;
    link = OM_fetch_pointer(ST_LIST_FIRST_LINK, list);
    while (OM_is_present(link)) {
        if (!OM_pointer_bit(link)
         || OM_fetch_word_length(link) <= ST_PROCESS_MY_LIST)
            return ST_NIL;
        if (!is_named(link)) {
            if (link == OM_fetch_pointer(ST_LIST_FIRST_LINK, list))
                return SCHED_remove_first_link(list);
            /*
             *  From the middle: counted first, as remove_link_from_list
             *  asks, and published exactly as SCHED_remove_first_link
             *  publishes what it takes.
             */
            OM_increase_ref(link);
            remove_link_from_list(link, list);
            {
                st_hands   *h = my_hands();

                if (h)
                    publish(&h->taken, link);
            }
            return link;
        }
        link = OM_fetch_pointer(ST_LINK_NEXT, link);
    }
    return ST_NIL;
}

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
    /*
     *  And the cross-worker tables, which a test harness that resets the
     *  scheduler between runs would otherwise carry from one to the next:
     *  a name left in the detach table is a process that can never run.
     */
    {
        unsigned    i;

        for (i = 0; i < DETACH_MAX; ++i)
            ST_store_release(&detach_table[i], (uintptr_t) ST_OOP_INVALID);
        ST_store_seq(&detach_count, 0);
        ST_store_seq(&frozen, 0);
        if (my_hands())
            clear_hands(my_hands());
    }
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
     *  The list takes hold of the next link BEFORE this one lets go of it.
     *
     *  A link in the middle of a list is held by nothing but the link
     *  before it: the list itself refers only to its first and its last.
     *  Chapter 29 unlinks and then writes "link nextLink: nil", which is
     *  fine where nothing is counting.  Here that store was made first, and
     *  with three or more processes queued it dropped the second one's only
     *  reference -- and releasing it released ITS next, and so on down the
     *  chain, so the list's new first link named a freed object.  Ten
     *  connection processes forked at once found it: the slots were reused
     *  by contexts, and the scheduler resumed a MethodContext's instruction
     *  pointer as a suspended context.  Re-pointing the list first keeps
     *  every link counted at every step; `first' is safe throughout because
     *  of the reference taken above.
     */
    if (first == last) {
        OM_store_pointer(ST_LIST_FIRST_LINK, list, ST_NIL);
        OM_store_pointer(ST_LIST_LAST_LINK, list, ST_NIL);
    }  else  {
        OM_store_pointer(ST_LIST_FIRST_LINK, list, next);
    }
    OM_store_pointer(ST_LINK_NEXT, first, ST_NIL);
    /*
     *  Published as in this worker's hands BEFORE myList says it is on no
     *  list, and before the caller's lock is dropped -- every caller
     *  holds the list's lock here -- so that a worker asking after the
     *  process finds it somewhere at every instant: on the list, or here.
     *  The window between a removal and the nomination or requeue that
     *  follows it is a few instructions wide, and it is the one place a
     *  process is held by nothing but a C local.  Cleared by `land' from
     *  SCHED_sleep or SCHED_transfer_to.
     */
    {
        st_hands   *h = my_hands();

        if (h)
            publish(&h->taken, first);
    }
    /*
     *  And it is on no list now, which is what myList is for.  Leaving it
     *  set says the process is still queued when it is not, and the guard in
     *  addLastLink: below believes it.
     */
    OM_store_pointer(ST_PROCESS_MY_LIST, first, ST_NIL);
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
    /*
     *  Landed: the list holds it, and only now does this worker stop
     *  answering for it.  Inside the lock, so that a worker which then
     *  takes the lock to look for the process sees it on the list.
     */
    land(process);
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
    /*
     *  A nominee is on no list: whoever nominates it took it off one, or
     *  it was never on one.  A process nominated while still linked would
     *  be run from here and again by whoever next takes it from the list,
     *  which is the one fault every other scheduler symptom hides behind,
     *  so it is refused here rather than diagnosed later.
     */
    if (OM_is_present(process)
     && OM_is_present(OM_fetch_pointer(ST_PROCESS_MY_LIST, process))) {
        fprintf(stderr, "st80: a process still on a list was nominated to "
                        "run; the scheduler's invariant is broken\n");
        ST_report_backtrace();
        abort();
    }
    OM_increase_ref(process);
    OM_decrease_ref(new_process);       /*  any nomination this supersedes  */
    new_process_waiting = 1;
    new_process         = process;
    /*
     *  Into `nominee' before out of `taken': a scanner reading the two in
     *  the opposite order still finds it in one of them.
     */
    {
        st_hands   *h = my_hands();

        if (h) {
            publish(&h->nominee, process);
            if ((st_oop) ST_load_relaxed(&h->taken) == process)
                publish(&h->taken, ST_OOP_INVALID);
        }
    }
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
     *  Nothing is ready while a snapshot is being written: the ready
     *  lists are what the image is being saved WITH, and a process taken
     *  off one now would be running, on no list, when the file is closed.
     */
    if (ST_load_relaxed(&frozen))
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
            /*
             *  A list whose only occupants are being detached answers
             *  nil, and the search goes on to the next priority down
             *  rather than stopping at a list that has nothing to give.
             */
            found = take_first_runnable(list);
            if (OM_is_present(found))
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
#define disowned                (st_vm.disowned)

/*
 *  Park the active process where another worker can pick it up from: its
 *  registers into its context, the context into the process.  After this
 *  the process belongs to whoever resumes it, and this worker must not
 *  write to it again -- disowned tells SCHED_check_process_switch so.
 *  Idempotent, because the wait primitive parks before it links and
 *  SCHED_suspend_active would otherwise park a second time.
 *
 *  Parking is not landing: `held' still names the process until the
 *  caller has put it on a list or set it free, which is the caller's
 *  next line in every case.
 */
static void
store_active_for_suspension(void)
{
    st_oop  self = SCHED_active_process();

    if (OM_is_object(self) && !disowned) {
        ST_store_active_context();
        OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, self,
                         st_vm.active_context);
        disowned = 1;
    }
}

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
     *  Parking first closes most of it, and the rest was closed later: the
     *  width of one list operation is still a window, and twenty-four
     *  workers spinning in Processor yield found it in seconds -- one
     *  context pushed on by two threads overflowed its frame.  So a
     *  process that waits on a Semaphore is now parked INSIDE
     *  SCHED_primitive_wait, under the semaphore's lock and before it is
     *  linked, and arrives here already disowned; store_active_for_
     *  suspension does nothing twice.  Disowning matters just as much:
     *  once the process is resumable it belongs to whoever takes it, and
     *  the switch below must not write this thread's stale context over
     *  the progress that thread has since made.
     */
    store_active_for_suspension();

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
            if (SCHED_stop_requested())
                break;
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
            /*
             *  And it has to be true with nothing owed from outside.  A
             *  delay armed, or a socket a process is parked on, is a wait
             *  somebody else will end: not a deadlock, and not the five-
             *  minute backstop's business either, so the slice count is
             *  reset while one is outstanding.  A quiet server -- every
             *  worker idle, a listener armed, no client yet -- sits here
             *  counted, and the verdict stays reachable the moment the
             *  last socket is closed.
             */
            /*
             *  A freeze is a wait of the same kind: the worker writing
             *  the snapshot is not idle and will thaw the rest when the
             *  file is closed.  Counted as neither idle nor stuck.
             */
            if (waits_pending() || ST_load_relaxed(&frozen)) {
                all_idle = 0;
                slice    = 0;
            }  else if (ST_load_seq(&idle_workers) >= (int) WORKER_count()) {
                if (++all_idle >= ALL_IDLE_CONFIRMATIONS)
                    break;
            }  else {
                all_idle = 0;
            }
            WORKER_poll();
            if (idle_hook)
                idle_hook();
            ST_sleep_ns(IDLE_WAIT_SLICE_NS);
            drain_async_signals();
            /*
             *  The drain above can NOMINATE rather than enqueue.  A signal
             *  delivered from the timer resumes its waiter through the
             *  same path a running process would use, and when the waiter
             *  outranks this worker's (parked) active process that path is
             *  SCHED_transfer_to: the process is put in new_process and
             *  new_process_waiting is set, for the interpreter loop to
             *  pick up at its next send.  This loop is not the interpreter
             *  loop, and it went on idling with the process in its hand.
             *  The other workers saw every worker idle and no timer armed
             *  -- the timer had fired, that is why there was a signal --
             *  and after a hundred confirmations one of them declared that
             *  every process was blocked and stopped, taking its thread
             *  with it; the rest could then never again count every worker
             *  idle, and waited for ever.  Two workers waiting on
             *  one-millisecond Delays hung every run, and one worker never
             *  did, because the single-worker path below checks exactly
             *  this.  A nomination is something to run: leave the loop and
             *  let the switch at the bottom happen.
             */
            if (new_process_waiting)
                break;
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
    while (next == ST_NIL && !new_process_waiting && waits_pending()
        && !SCHED_stop_requested()) {
        WORKER_poll();
        if (idle_hook)
            idle_hook();
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
     *  Asked to stop, and holding nothing: this worker's run is over.  A
     *  process that was taken above is run rather than dropped -- it is a
     *  held reference, and the stop is seen again at its next switch.
     */
    if (next == ST_NIL && !new_process_waiting && SCHED_stop_requested()) {
        st_vm.running = 0;
        return;
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
        /*
         *  Said with the evidence, because this verdict has been wrong
         *  before -- a lost nomination looked exactly like this -- and the
         *  state that decides it is what a reader needs first.
         */
        fprintf(stderr, "st80: every process is blocked; nothing can run\n");
        fprintf(stderr, "       timer pending %d, external waits %d, "
                        "async signals queued %d, %d of %u workers idle\n",
                SCHED_timer_pending(),
                external_wait_hook ? external_wait_hook() : 0,
                ST_load_relaxed(&async_count),
                ST_load_seq(&idle_workers), WORKER_count());
        ST_interp_dump_workers();
        st_vm.running = 0;
        return;
    }
    SCHED_transfer_to(next);
    /*  The nomination holds it now; this releases the loan from removal.  */
    OM_decrease_ref(next);
}

/*
 *  Join the scheduler with no process of one's own.
 *
 *  A worker started by `st80 -serve' other than the first has nothing to
 *  run: the image's startup process belongs to worker 0, and everything
 *  else is forked later.  The parallel tests give such a worker a compiled
 *  `Semaphore new wait' to park in, but a run mode has no compiler to hand
 *  after an image is loaded.  So the worker declares itself idle directly:
 *  no active process, and DISOWNED, so that store_active_for_suspension,
 *  SCHED_resume's requeue and SCHED_check_process_switch's park all skip
 *  the process SCHED_active_process would otherwise fall back to -- the
 *  scheduler's shared field, which names whatever some other worker
 *  switched to last.  Then the ordinary wait: the ready lists, the idle
 *  loop, and a nomination the interpreter loop picks up at its first
 *  switch check before it fetches a bytecode.  Returns with a nomination
 *  pending, or with running cleared because the image stopped.
 */
void
SCHED_enter_idle(void)
{
    st_vm.active_process = ST_NIL;
    new_process          = ST_NIL;
    new_process_waiting  = 0;
    disowned             = 1;
    st_vm.running        = 1;
    if (my_hands())
        clear_hands(my_hands());
    SCHED_suspend_active();
}

/*
 *  A worker that took its first process by hand -- `-serve''s worker 0
 *  adopts the image's startup process before it runs a bytecode -- says
 *  so here, so that a terminate from another worker finds that process
 *  in somebody's hands and not free.
 */
void
SCHED_publish_active(void)
{
    st_hands   *h = my_hands();

    if (h && OM_is_present(st_vm.active_process))
        publish(&h->held, st_vm.active_process);
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
        /*
         *  The process being preempted goes onto its ready list, and from
         *  that moment any idle worker may take it and run it -- from its
         *  suspendedContext.  So a running one is parked FIRST: registers
         *  into the context, context into the process, and disowned so
         *  that the switch this nomination causes does not write over it
         *  again.  Chapter 29 sleeps the active process and switches later,
         *  which is fine when only one thread can run it; here the gap
         *  between the two was a running process on the ready list, and
         *  with twenty-four workers scanning that list every tenth of a
         *  millisecond it was taken inside the gap.
         *
         *  A nominee -- the active process as far as priority goes, but
         *  not yet running -- is parked already and on no list, and goes
         *  back on its list.  A disowned one is NOT requeued: this worker
         *  parked it and handed it on, so it is on the list it waits on,
         *  or it has finished.  Chapter 29 puts "the active process" back
         *  unconditionally, and on an idle worker that was one that had
         *  terminated -- the helper Processor yield forks, which ends
         *  itself with terminateActive.  Requeued, it was run again from
         *  where it had stopped: its block signalled its semaphore a
         *  second time, and when it fell off the bottom of the block it
         *  took the worker's whole run with it.
         */
        if (new_process_waiting) {
            SCHED_sleep(active);
        }  else if (!disowned) {
            store_active_for_suspension();
            SCHED_sleep(active);
        }
        SCHED_transfer_to(process);
    }  else  {
        SCHED_sleep(process);
    }
}

void
SCHED_synchronous_signal(st_oop semaphore)
{
    st_mutex   *lock;
    st_oop      woken = ST_NIL;

    if (!OM_is_present(semaphore))
        return;
    /*
     *  Under the semaphore's stripe lock, exactly as SCHED_primitive_signal
     *  is, because SCHED_primitive_wait is: a waiter reads the excess count
     *  and links itself under that lock, and a signal that runs between
     *  the read and the link without it sees an empty list, spends itself
     *  as an excess, and leaves the waiter linked for ever.  This is the
     *  path the timer's and the input's signals take, drained by whichever
     *  worker drains them, so the waiter it lost was the Delay timing
     *  process -- on TimingSemaphore, with the excess count at one and
     *  nobody ever coming to take it.  Every Delay in the image then
     *  waited behind it.  One worker never showed it, since the drain and
     *  the wait were then the same thread; sixteen and up hung within a
     *  second.  The resume happens outside the lock, as in the primitive:
     *  resuming can transfer, which polls a safepoint, and a lock held
     *  across a safepoint poll is the deadlock the stripe rule forbids.
     */
    lock = stripe_for(semaphore);
    stripe_lock(lock);
    if (!SCHED_is_empty_list(semaphore))
        woken = take_first_runnable(semaphore);
    /*
     *  Nobody to wake -- an empty list, or one holding only a process
     *  that is being detached, which its detacher will take off the list
     *  and which must not be handed this signal -- is an excess.
     */
    if (!OM_is_present(woken)) {
        st_oop  excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS,
                                          semaphore);

        if (OM_is_int(excess))
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) + 1));
    }
    stripe_unlock(lock);
    if (OM_is_present(woken)) {
        SCHED_resume(woken);
        OM_decrease_ref(woken);
    }
}

int
SCHED_asynchronous_signal(st_oop semaphore)
{
    int queued = 0;

    if (!OM_is_present(semaphore))
        return 1;                       /*  nothing to deliver: not owed  */
    async_lock_init();
    ST_mutex_lock(&async_lock);
    {
        int n = ST_load_relaxed(&async_count);

        if (n < ASYNC_QUEUE_MAX) {
            async_queue[n] = semaphore;
            ST_store_release(&async_count, n + 1);
            queued = 1;
        }
        /*
         *  else drop rather than corrupt -- and SAY SO, because for the
         *  network a dropped signal is a request that never wakes.  The
         *  I/O thread keeps the socket armed and tries again.
         */
    }
    ST_mutex_unlock(&async_lock);
    return queued;
}

/*  The same, with the signature the network layer's hook wants.  */
int
SCHED_signal_token(uintptr_t token)
{
    return SCHED_asynchronous_signal((st_oop) token);
}

/*
 *  The queued semaphores, as roots.
 *
 *  Between a producer's enqueue and a worker's drain the queue holds a bare
 *  oop that nothing counts.  For the timer the Delay still holds its
 *  semaphore, and for input the image does; for a socket whose owner was
 *  dropped in the same instant, nothing might.  A collection in that
 *  window -- and the I/O thread enqueues during collections, having no
 *  idea one is running -- would then reclaim what the drain is about to
 *  signal.  So the root walk visits the queue.  Under the lock, which the
 *  producers hold only for a store.
 */
void
SCHED_visit_async_roots(om_visit_fn visit)
{
    int     i;

    async_lock_init();
    ST_mutex_lock(&async_lock);
    for (i = 0; i < ST_load_relaxed(&async_count); ++i)
        visit(async_queue[i]);
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
SCHED_release_nomination(void)
{
    st_oop  process;

    if (!new_process_waiting)
        return;
    process             = new_process;
    new_process_waiting = 0;
    new_process         = ST_NIL;
    if (!OM_is_present(process))
        return;
    SCHED_sleep(process);
    OM_decrease_ref(process);           /*  the nomination's count  */
}

void
SCHED_check_process_switch(void)
{
    /*
     *  A stop asked for from outside ends this worker's run here, at a
     *  bytecode boundary, where its registers are consistent.  One relaxed
     *  load beside the one the drain already does.
     */
    if (SCHED_stop_requested()) {
        st_vm.running = 0;
        return;
    }
    /*
     *  Drain into a local copy and signal outside the lock.  Signalling
     *  can resume a process, which can transfer, which polls a safepoint --
     *  and holding a lock across a safepoint poll is the deadlock the
     *  stripe-lock rule above exists to forbid.  The same reasoning
     *  applies here and to every lock this system will ever add.
     */
    /*
     *  A run that has ended -- its process returned off the bottom -- must
     *  neither drain nor switch.  The check runs at the top of the cycle,
     *  before the loop looks at `running', so on the cycle after the return
     *  it used to drain the queue, wake the Delay timing process, nominate
     *  it here because its priority is higher, and switch onto it; the loop
     *  then broke and the worker went home with the timing process as its
     *  active process, on no list, run by nobody.  Every Delay after that
     *  waited for ever, with the timer disarmed and nothing to re-arm it.
     *  The parallel REST gate found it: its drivers return when the count
     *  is reached, and the wake that raced the last return lost the timer
     *  for the next phase.  The queue is left for a worker that is still
     *  running, and ST_interp_run hands a nominee already held back to its
     *  ready list on the way out.
     */
    if (!st_vm.running)
        return;
    drain_async_signals();

    /*
     *  The two rare cases, behind one load: a snapshot has frozen the
     *  scheduler, or some worker is detaching a process (see the two
     *  tables above).  A process this worker is executing and must not
     *  run is parked onto its ready list exactly as Processor yield parks
     *  one; a nominee it must not switch to goes back to its ready list.
     *  Then, holding nothing runnable, the worker goes to find something
     *  else -- and looks again at what it finds, since during a freeze
     *  nothing it finds may be run either, and the idle loop is where a
     *  frozen worker waits.
     */
    if (attention_wanted()) {
        for (;;) {
            if (new_process_waiting && must_not_run(new_process))
                SCHED_release_nomination();
            if (!disowned && must_not_run(SCHED_active_process())) {
                st_oop  active = SCHED_active_process();

                store_active_for_suspension();
                SCHED_sleep(active);
            }
            if (new_process_waiting || !disowned)
                break;
            SCHED_suspend_active();
            if (!st_vm.running)
                return;
        }
    }

    for (;;) {
        st_oop  incoming;

        if (!new_process_waiting)
            return;
        new_process_waiting = 0;

        /*
         *  A nominee whose suspendedContext is not a context is never
         *  run.  Bugs3 B3: nil is the mark of a terminated process, and
         *  a terminated process can still arrive here -- taken off a
         *  semaphore by a signal that raced its terminate, or resumed by
         *  an image written before primitive 87 learnt to refuse it --
         *  and switching to it stopped the worker's whole run, and under
         *  -serve the whole image, on `asked to run an object that is not
         *  a context'.  Dropped instead: the nomination's count is
         *  released, the process stays terminated, and this worker either
         *  carries on with the process it never stopped running or, if it
         *  had parked that one already, looks for another.  Anything
         *  else in the field is a process somebody has broken, which is
         *  reported, once, and dropped the same way.
         */
        incoming = OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT, new_process);
        if (!OM_is_object(incoming)
         || (OM_fetch_class(incoming) != ST_CLASS_METHOD_CONTEXT
          && OM_fetch_class(incoming) != ST_CLASS_BLOCK_CONTEXT)) {
            st_oop  dead = new_process;

            if (incoming != ST_NIL)
                fprintf(stderr, "st80: a process whose suspended context is "
                                "not a context was scheduled; it is dropped\n");
            new_process = ST_NIL;
            land(dead);
            OM_decrease_ref(dead);
            if (!disowned)
                return;
            SCHED_suspend_active();
            if (!st_vm.running)
                return;
            continue;
        }
        break;
    }

    /*
     *  Park the running process's context in its Process object, then make
     *  the incoming one's context active.  Everything the old process needs
     *  to resume is in that one pointer.
     */
    /*
     *  Unless this worker has already parked and handed the process on.
     *  Parking again would write this thread's stale context over whatever
     *  progress the worker that took it has since made -- and so would
     *  storing the registers alone, since they are stored INTO the
     *  context, which the worker that took the process is executing.
     *  Both stores are skipped, not just the second.
     */
    if (!disowned) {
        ST_store_active_context();
        OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, SCHED_active_process(),
                         st_vm.active_context);
    }
    disowned = 0;
    /*
     *  The image's field as well as this worker's, because a snapshot
     *  carries the field and the image's own reflection reads it.  With
     *  several workers running processes it holds whichever switched last,
     *  which is the honest answer to a question that no longer has one --
     *  and is why Processor>>activeProcess becomes a primitive that asks
     *  the calling worker instead.
     */
    /*
     *  A running process is held by its worker, and the count says so.
     *
     *  This used to release the nomination's count here, once the process
     *  was active, on the strength of the field above: the image's
     *  activeProcess variable holds one count, and with one worker it
     *  always held the running process.  With N workers it holds whichever
     *  switched last, and every other running process -- on no list, since
     *  it is running -- had a count of zero.  The next switch on any
     *  worker stored over the field, the count of the process it had held
     *  went from one to nothing, and a process that was executing on some
     *  core was freed and its slot handed out: the Delay timing process
     *  came back as a MethodContext, with the semaphore it waited on
     *  pointing at it.  The nomination's count is now kept as the active
     *  count for as long as the process is active here, and the process
     *  that was active gives its own up.  That is also what the collector
     *  counts when it visits each worker's active process, so the two
     *  agree.
     */
    {
        st_oop  was = st_vm.active_process;
        st_hands   *h = my_hands();

        st_vm.active_process = new_process;
        /*  Into `held' before out of `nominee', for the same reader.  */
        if (h) {
            publish(&h->held, new_process);
            publish(&h->nominee, ST_OOP_INVALID);
        }
        new_process = ST_NIL;
        /*
         *  One slot, written by every worker on every switch: exchanged,
         *  not stored, so the process this evicts is released once and by
         *  one worker.  See OM_exchange_pointer.
         */
        OM_exchange_pointer(ST_SCHEDULER_ACTIVE_PROCESS, SCHED_scheduler(),
                            st_vm.active_process);
        ST_set_active_context(
            OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT,
                             st_vm.active_process));
        OM_decrease_ref(was);
    }
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
        /*
         *  The same order as SCHED_remove_first_link, for the same reason:
         *  whoever comes before takes hold of `next' before this link lets
         *  go of it, or a link in the middle of the list loses its only
         *  reference.
         */
        if (OM_is_present(previous))
            OM_store_pointer(ST_LINK_NEXT, previous, next);
        else
            OM_store_pointer(ST_LIST_FIRST_LINK, list, next);
        if (link == last)
            OM_store_pointer(ST_LIST_LAST_LINK, list,
                             OM_is_present(previous) ? previous : ST_NIL);
        OM_store_pointer(ST_LINK_NEXT, link, ST_NIL);
        OM_store_pointer(ST_PROCESS_MY_LIST, link, ST_NIL);
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
    woken = SCHED_is_empty_list(semaphore) ? ST_NIL
                                           : take_first_runnable(semaphore);
    /*  As in SCHED_synchronous_signal: nobody to wake is an excess.  */
    if (!OM_is_present(woken)) {
        st_oop  excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS,
                                          semaphore);

        if (OM_is_int(excess))
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) + 1));
        stripe_unlock(lock);
        return 1;
    }
    stripe_unlock(lock);

    if (OM_is_present(woken)) {
        SCHED_resume(woken);
        /*  A list or the nomination holds it; release the removal's loan. */
        OM_decrease_ref(woken);
    }
    return 1;               /*  answers the receiver, already on the stack  */
}

/*
 *  167: Processor yield -- let another process at my priority run.
 *
 *  1983 had no primitive for this and wrote yield as `[semaphore signal]
 *  fork. semaphore wait': a helper process per call, whose block is a
 *  BlockContext with its home in the caller's frame, ended by
 *  terminateActive.  Under one thread that is a neat trick.  Under
 *  thirty-one it is a process created and destroyed on every call, whose
 *  context is shared between the two, and it was the one thing that still
 *  went wrong after every semaphore race in this file had been closed --
 *  a run of thirty-one workers yielding in a loop failed one time in two.
 *
 *  Reorganize, in the contract's word: the primitive parks the active
 *  process, puts it at the END of its ready list, and switches to whatever
 *  is ready -- which is itself, when nothing else is, in which case
 *  nothing happens at all.  No helper, no second context, no semaphore.
 */
int
SCHED_primitive_yield(void)
{
    st_oop  active = SCHED_active_process();
    st_oop  priority;
    st_oop  lists;
    st_oop  list;
    int     someone_waiting;

    if (!OM_is_object(active))
        return 0;
    priority = OM_fetch_pointer(ST_PROCESS_PRIORITY, active);
    lists    = OM_fetch_pointer(ST_SCHEDULER_PROCESS_LISTS, SCHED_scheduler());
    if (!OM_is_int(priority) || !OM_is_present(lists)
     || OM_int_value(priority) < 1
     || (uint32_t) OM_int_value(priority) > OM_fetch_word_length(lists))
        return 0;
    /*
     *  Only worth a switch if something at my priority or above is ready:
     *  a yield with nothing to yield to answers at once, as Chapter 29's
     *  version effectively did.  Read without the lock -- a process that
     *  becomes ready a moment later will get its turn at the next yield.
     */
    {
        uint32_t    i;

        someone_waiting = 0;
        for (i = OM_fetch_word_length(lists); i >= (uint32_t) OM_int_value(priority); --i) {
            list = OM_fetch_pointer(i - 1, lists);
            if (OM_is_object(list) && !SCHED_is_empty_list(list)) {
                someone_waiting = 1;
                break;
            }
        }
    }
    if (!someone_waiting)
        return 1;               /*  answers the receiver, already on the stack  */
    store_active_for_suspension();
    SCHED_sleep(active);
    SCHED_suspend_active();
    return 1;
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
        if (must_wait) {
            /*
             *  Parked BEFORE it is linked, and under the lock.
             *
             *  Once this process is on the semaphore's list, any worker
             *  can signal the semaphore, take the process off and run it
             *  -- from suspendedContext.  Linking first and parking in
             *  SCHED_suspend_active afterwards left a window, one list
             *  operation wide, in which another worker ran the process
             *  from the context it had been parked with LAST time while
             *  this worker was still finishing the wait.  One context
             *  pushed on by two threads overflowed its frame inside
             *  ProcessorScheduler>>yield; a Delay reported itself waited
             *  on twice; a mutual-exclusion Semaphore was signalled more
             *  often than it was taken.  Twenty-four workers yielding at
             *  once found it in seconds and sixteen never did, which is
             *  how wide the window was.
             */
            store_active_for_suspension();
            SCHED_add_last_link(SCHED_active_process(), semaphore);
            /*  Landed on the semaphore, under its lock: no longer ours. */
            land(SCHED_active_process());
        }  else
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) - 1));
        stripe_unlock(lock);
        if (!must_wait)
            return 1;
    }
    SCHED_suspend_active();
    return 1;
}

/*
 *  87: Process>>resume.  Fails, and so raises in the image, for a process
 *  that is anything but parked and free.  Bugs3 B3 and B17.
 *
 *  Chapter 29 queued whatever it was given.  A terminated process -- its
 *  suspendedContext nil, which is how both 1983's terminate and this
 *  system's leave it -- was queued too, and the next worker to take it
 *  called set_active_context(nil) and stopped the whole image; a watchdog
 *  that cleans up twice is exactly the shape that produced it.  And a
 *  process that is RUNNING -- `Processor activeProcess resume', or a
 *  process on another worker -- was queued while its registers were
 *  still in some interpreter, so an idle worker ran it from its stale
 *  context and two threads were inside one context.  Squeak's
 *  primitiveResume checks the first two conditions; the third has no
 *  meaning on one thread and is this system's own.
 *
 *  The order of the checks is the order of their cost.  The scan of the
 *  hands table is sixty-four rows of three loads, paid on every fork;
 *  it is nothing beside making the process.
 */
int
SCHED_primitive_resume(void)
{
    st_oop  process = ST_stack_top();
    st_oop  context;

    if (!OM_is_object(process) || !OM_pointer_bit(process)
     || OM_fetch_word_length(process) <= ST_PROCESS_MY_LIST)
        return 0;
    context = OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT, process);
    if (!OM_is_object(context)
     || (OM_fetch_class(context) != ST_CLASS_METHOD_CONTEXT
      && OM_fetch_class(context) != ST_CLASS_BLOCK_CONTEXT))
        return 0;               /*  terminated, or never given a context  */
    if (OM_is_present(OM_fetch_pointer(ST_PROCESS_MY_LIST, process)))
        return 0;               /*  already waiting somewhere  */
    if (process == SCHED_active_process())
        return 0;               /*  running: here  */
    if (in_anyones_hands(process) || is_named(process))
        return 0;               /*  running, or being stopped: elsewhere  */
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
    /*
     *  Parked and then set free on purpose -- on no list, for a resume
     *  to find -- so it is landed here, before this worker goes looking
     *  for something else to run.
     */
    store_active_for_suspension();
    land(process);
    SCHED_suspend_active();
    return 1;
}

/*
 *  232: Process>>primTerminateActive -- end the process this worker is
 *  running, for good.  Bugs3 B3 and B13.
 *
 *  1983's Process>>terminate, for the active process, was `thisContext
 *  removeSelf suspend': the process parked itself in the terminate frame
 *  and stayed resumable, and resuming it ran off the bottom of that frame
 *  and took the worker's whole run with it.  What marks a process as
 *  terminated here is a nil suspendedContext -- the same mark 1983's
 *  other branch left, the one primitive 87 refuses and the switch drops
 *  -- so the active process's context is discarded rather than parked.
 *  The unwind blocks have already been run by the Smalltalk side, from
 *  thisContext outwards, before this is sent.
 *
 *  Nothing is written to the process after this except by its own
 *  worker's switch, which releases the count it held; disowned is what
 *  keeps the switch from parking the dead registers over the nil.
 */
int
SCHED_primitive_terminate_active(void)
{
    st_oop  process = SCHED_active_process();

    if (!OM_is_object(process) || disowned)
        return 0;
    disowned = 1;
    OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, process, ST_NIL);
    land(process);
    SCHED_suspend_active();
    return 1;               /*  the receiver stays on the dead stack  */
}

/*
 *  231: Process>>primDetach: takeFromSemaphore -- bring the receiver to
 *  a stop wherever it is, and say where that was.  Bugs3 B16.
 *
 *  Answers a SmallInteger:
 *
 *      0   it was nowhere: on no list and in no worker's hands, which is
 *          a process already suspended, terminated, or never resumed
 *      1   it was running on a worker, or about to be; that worker has
 *          parked it and let go
 *      2   it was waiting for the processor on a ready list
 *      3   it was waiting on a Semaphore and was taken off it
 *      4   it was waiting on a Semaphore and was LEFT there, because the
 *          argument was false -- suspend keeps 1983's refusal to take a
 *          process out of a wait it would later continue past
 *
 *  and in every case but 4 the receiver is afterwards parked, on no list,
 *  in nobody's hands, with its suspendedContext where it stopped: the
 *  state Process>>terminate, >>suspend and >>signalException: each go on
 *  from.  Fails for the caller's own active process, which those methods
 *  handle themselves, and for anything that is not a Process.
 *
 *  The loop is the whole argument for the two tables above.  Naming the
 *  process first means that from here on it can only move towards being
 *  free: a worker executing it parks it onto its ready list at its next
 *  bytecode, a worker about to switch to it releases it to its ready
 *  list instead, no signal and no idle worker takes it off a list, and
 *  primitive 87 will not resume it.  So each pass either finds it on a
 *  list -- and takes it off under that list's lock, which is the lock the
 *  takers use, so the two cannot interleave -- or finds it in some
 *  worker's hands and waits a moment for that worker to land it, or
 *  finds it in neither and is done: with the exits closed, nowhere is
 *  free.  The order within a pass matters and is the order written:
 *  the hands, and THEN the list again, because a worker lands a process
 *  on a list before it clears `held' -- so a process seen in no hands is
 *  on a list or free, and only a look at the list taken after that
 *  sighting can tell which.
 *
 *  Waiting polls the safepoint and holds no lock, since the worker being
 *  waited for may be the one asking for a collection.  A stop request
 *  ends the wait: the run is over and the answer no longer matters.
 */
int
SCHED_primitive_detach(void)
{
    st_oop  take    = ST_stack_value(0);
    st_oop  process = ST_stack_value(1);
    int     where   = 0;
    int     seen_in_hands = 0;

    if (!OM_is_object(process) || !OM_pointer_bit(process)
     || OM_fetch_word_length(process) <= ST_PROCESS_MY_LIST)
        return 0;
    if (take != ST_TRUE && take != ST_FALSE)
        return 0;
    if (process == SCHED_active_process())
        return 0;
    /*
     *  This worker's own nominee is the one case it can settle itself:
     *  the nomination goes back to the ready list, where the loop below
     *  finds it.
     */
    if (new_process_waiting && new_process == process) {
        SCHED_release_nomination();
        seen_in_hands = 1;
    }
    while (!name_process(process)) {
        /*  Every slot taken by another worker's detach: wait for one.  */
        if (SCHED_stop_requested())
            return 0;
        WORKER_poll();
        ST_sleep_ns(1000);
    }
    for (;;) {
        st_oop  list = OM_fetch_pointer(ST_PROCESS_MY_LIST, process);

        if (SCHED_stop_requested())
            break;
        if (OM_is_present(list)) {
            st_oop  priority = OM_fetch_pointer(ST_PROCESS_PRIORITY, process);
            st_oop  ready    = OM_is_int(priority)
                             ? ready_list_at(OM_int_value(priority)) : ST_NIL;

            if (list == ready) {
                if (SCHED_remove_ready_process(process)) {
                    where = seen_in_hands ? 1 : 2;
                    break;
                }
                continue;           /*  it moved between the read and the lock  */
            }
            if (take == ST_FALSE) {
                where = 4;
                break;
            }
            {
                st_mutex   *lock = stripe_for(list);
                int         removed;

                stripe_lock(lock);
                removed = OM_fetch_pointer(ST_PROCESS_MY_LIST, process) == list
                       && remove_link_from_list(process, list);
                stripe_unlock(lock);
                if (removed) {
                    where = seen_in_hands ? 1 : 3;
                    break;
                }
                continue;
            }
        }
        if (in_anyones_hands(process)) {
            seen_in_hands = 1;
            WORKER_poll();
            ST_sleep_ns(1000);
            continue;
        }
        /*
         *  In nobody's hands -- and the list is looked at AGAIN, after
         *  that, not before it.  A worker that parks the process links it
         *  before it clears `held', so between this pass's first read of
         *  myList and its read of the hands the process can have landed
         *  on a semaphore: read in the other order that is a process on
         *  no list and in no hands, which is the conclusion, and wrong.
         *  Read in this order it is on the list, or it is free, and
         *  nothing can change either while it is named.
         */
        if (OM_is_present(OM_fetch_pointer(ST_PROCESS_MY_LIST, process)))
            continue;
        where = seen_in_hands ? 1 : 0;
        break;
    }
    unname_process(process);
    ST_pop_n(2);
    ST_push(OM_int_oop(where));
    return 1;
}

/*
 *  ----------  Freezing every worker, for a snapshot  ----------
 *
 *  Bugs3 B9.  A snapshot from a worker pool parked the process taking it
 *  and nothing else: every process running on another worker at that
 *  instant was written with the suspendedContext it had been parked with
 *  LAST time, on no list, and was simply gone from the image that came
 *  back -- and whatever it held, a Mutex say, was held for ever there.
 *  SCHED_freeze asks every other worker to park what it runs onto its
 *  ready list and idle; SCHED_wait_frozen waits until they have; and
 *  SCHED_thaw lets them take it all back once the file is closed.
 *
 *  Not a safepoint, though it looks like one, because the image writer
 *  takes a safepoint of its own for the collection it runs first, and a
 *  safepoint inside a safepoint is a requester parked waiting for
 *  itself.  What the freeze provides is weaker and enough: the scheduler
 *  state the writer needs is the LISTS, and the lists are complete once
 *  every other worker is in the idle loop with nothing in its hands.
 *
 *  Signals keep flowing during a freeze -- a waiter taken off a
 *  semaphore lands on its ready list and stays there -- so an image
 *  saved mid-delay has that delay's waiter ready rather than lost.
 */
void
SCHED_freeze(void)
{
    ST_store_seq(&frozen, 1);
}

void
SCHED_thaw(void)
{
    ST_store_seq(&frozen, 0);
}

int
SCHED_wait_frozen(int64_t timeout_ns)
{
    int64_t     deadline = ST_time_monotonic_ns() + timeout_ns;
    unsigned    others   = WORKER_count() > 0 ? WORKER_count() - 1 : 0;

    for (;;) {
        unsigned    i;
        int         quiet = ST_load_seq(&idle_workers) >= (int) others;

        /*
         *  Idle is not the same as landed: a worker enters the idle
         *  count before it has cleared `held' for a process a drained
         *  signal handed it a moment ago.  Both, then.
         */
        if (quiet) {
            for (i = 0; i < ST_MAX_INTERPRETERS; ++i) {
                st_hands   *h = &hands[i];

                if (h == my_hands())
                    continue;
                if (OM_is_present((st_oop) ST_load_acquire(&h->held))
                 || OM_is_present((st_oop) ST_load_acquire(&h->nominee))
                 || OM_is_present((st_oop) ST_load_acquire(&h->taken)))
                    quiet = 0;
            }
        }
        if (quiet)
            return 1;
        if (ST_time_monotonic_ns() > deadline || SCHED_stop_requested())
            return 0;
        WORKER_poll();
        ST_sleep_ns(IDLE_WAIT_SLICE_NS);
    }
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
