/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Phase H's gate: semaphores that survive being used by real threads.
 *
 *  Chapter 29's Semaphore algorithm is correct on one thread and wrong on
 *  several, and it is wrong in the quietest possible way.  signal reads
 *  excessSignals, adds one, and writes it back; wait reads excessSignals,
 *  sees zero, and THEN queues the process.  Neither is one step.
 *
 *  So two threads signalling lose counts to each other, and a signal that
 *  lands between a waiter's read and its queueing is spent on a list that
 *  is about to stop being empty -- and the waiter waits for ever, holding
 *  whatever it was waiting to be handed.  Nothing crashes.  A count is
 *  simply lower than it should be, or a process never runs again.
 *
 *  Both are checked here by arithmetic rather than by hoping a sanitizer
 *  noticed: N threads perform exactly M operations each, and the totals
 *  have exactly one right answer.  Run it under the thread sanitizer,
 *  where it earns its keep:
 *
 *      make OM=mt TSAN=1 test
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "worker.h"
#include "st_sched.h"
#include "prim.h"
#include "st_atomic.h"
#include "st_port.h"

#include <stdio.h>
#include <string.h>

/*
 *  How many operations each worker performs.
 *
 *  The ARITHMETIC is what catches the bug, and it catches it at any count:
 *  the totals have exactly one right answer whether each thread does four
 *  hundred operations or twenty thousand.  What a large count buys is the
 *  chance of an unlucky interleaving -- and under a sanitizer that chance
 *  is already high, because everything is slowed down and instrumented.
 *  Ten million lock acquisitions under TSAN take half an hour and find
 *  nothing the first few thousand did not.
 */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
 || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                             || __has_feature(address_sanitizer)))
#define SIGNALS_PER_WORKER  400
#else
#define SIGNALS_PER_WORKER  20000
#endif
#define SEMAPHORE_COUNT     8

static st_oop           semaphores[SEMAPHORE_COUNT];
static st_atomic_int    signals_sent;

/*
 *  A frame to push onto.
 *
 *  The primitives take their receiver from the interpreter's stack, which
 *  is the active context -- that is the interface, and calling them any
 *  other way would be testing a copy of the code rather than the code.  So
 *  each worker stands up a context of its own, exactly as an activation
 *  would, and never runs a bytecode in it.
 */
static void
give_this_worker_a_frame(void)
{
    st_oop  context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 16);

    OM_increase_ref(context);
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, ST_NIL);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.stack_pointer = ST_CTX_TEMP_FRAME_START;
}

/*
 *  Enough objects that the guaranteed pointers exist, since the object
 *  table hands them out in order and ST_SCHEDULER_ASSOCIATION is one of
 *  them.
 */
static void
build_fixed_objects(void)
{
    int i;

    for (i = 0; i < 64; ++i) {
        st_oop  p = OM_instantiate_pointers(ST_NIL, 4);

        OM_increase_ref(p);
        (void) p;
    }
}

#define PRIORITY_COUNT      4
#define PROCESSES_PER_LIST  200

static st_oop           scheduler;
static st_atomic_int    processes_taken;
static st_atomic_int    taken_twice;

/*
 *  A ProcessorScheduler, by hand: an Array of LinkedLists, one per
 *  priority, reachable where SCHED_scheduler() looks for it.
 */
static void
build_scheduler(void)
{
    st_oop      lists = OM_instantiate_pointers(ST_NIL, PRIORITY_COUNT);
    unsigned    i;

    OM_increase_ref(lists);
    for (i = 0; i < PRIORITY_COUNT; ++i) {
        st_oop  list = OM_instantiate_pointers(ST_NIL, 2);

        OM_store_pointer(ST_LIST_FIRST_LINK, list, ST_NIL);
        OM_store_pointer(ST_LIST_LAST_LINK, list, ST_NIL);
        OM_store_pointer(i, lists, list);
    }
    scheduler = OM_instantiate_pointers(ST_NIL, 2);
    OM_increase_ref(scheduler);
    OM_store_pointer(ST_SCHEDULER_PROCESS_LISTS, scheduler, lists);
    OM_store_pointer(ST_ASSOCIATION_VALUE, ST_SCHEDULER_ASSOCIATION,
                     scheduler);
}

static st_oop
make_process(st_int priority)
{
    st_oop  p = OM_instantiate_pointers(ST_NIL, 4);

    OM_increase_ref(p);
    OM_store_pointer(ST_LINK_NEXT, p, ST_NIL);
    OM_store_pointer(ST_PROCESS_PRIORITY, p, OM_int_oop(priority));
    OM_store_pointer(ST_PROCESS_MY_LIST, p, ST_NIL);
    return p;
}

/*
 *  Take processes off the ready lists until they are empty, and count.
 *
 *  Finding the highest non-empty list and taking from it has to be ONE
 *  step.  Two workers that both look, both see the same process at the
 *  head, and both take it end up running one process on two native
 *  threads, through one context -- and the count here is one higher than
 *  the number of processes that ever existed.
 */
static void
draining_worker(st_worker *self, void *user)
{
    (void) user;
    (void) self;
    ST_interp_register();
    give_this_worker_a_frame();
    for (;;) {
        st_oop  taken = SCHED_wake_highest_priority();

        if (!OM_is_present(taken))
            break;
        /*
         *  myList is nilled by the removal, so a process taken twice is
         *  visible: the second taker finds it already detached.
         */
        if (OM_is_present(OM_fetch_pointer(ST_PROCESS_MY_LIST, taken)))
            ST_fetch_add_relaxed(&taken_twice, 1);
        ST_fetch_add_relaxed(&processes_taken, 1);
    }
    ST_interp_unregister();
}

/*
 *  A Semaphore, made by hand: this test has no image, and does not need
 *  one.  What it exercises is the two primitives' arithmetic, which is in
 *  C and knows nothing about the class library above it.
 */
static st_oop
make_semaphore(void)
{
    st_oop  s = OM_instantiate_pointers(ST_CLASS_SEMAPHORE, 3);

    if (!OM_is_present(s))
        return ST_OOP_INVALID;
    OM_increase_ref(s);
    OM_store_pointer(ST_LIST_FIRST_LINK, s, ST_NIL);
    OM_store_pointer(ST_LIST_LAST_LINK, s, ST_NIL);
    OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, s, OM_int_oop(0));
    return s;
}

static st_int
excess_of(st_oop semaphore)
{
    st_oop  excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore);

    return OM_is_int(excess) ? OM_int_value(excess) : -1;
}

static void
provide_test_roots(om_visit_fn visit)
{
    unsigned    i;

    for (i = 0; i < SEMAPHORE_COUNT; ++i)
        visit(semaphores[i]);
    visit(scheduler);
}

/*
 *  Every worker signals every semaphore, the same number of times.
 *
 *  Nobody is waiting, so each signal must land as an excess -- and the
 *  totals are then fixed: workers x SIGNALS_PER_WORKER on each semaphore,
 *  with no allowance for timing.  A lost increment is a wrong number, and
 *  a read-modify-write that is not one step loses increments the moment
 *  two threads overlap on the same stripe.
 */
static void
signalling_worker(st_worker *self, void *user)
{
    unsigned    round;
    unsigned    i;

    (void) user;
    ST_interp_register();
    give_this_worker_a_frame();
    for (round = 0; round < SIGNALS_PER_WORKER; ++round) {
        for (i = 0; i < SEMAPHORE_COUNT; ++i) {
            /*
             *  Through the primitive, on a stack of one, which is how the
             *  interpreter calls it -- the point is to test the code that
             *  actually runs and not a copy of it.
             */
            ST_push(semaphores[i]);
            SCHED_primitive_signal();
            ST_pop_n(1);
            ST_fetch_add_relaxed(&signals_sent, 1);
        }
        self->bytecodes += SEMAPHORE_COUNT;
    }
    ST_interp_unregister();
}

/*
 *  And the other half: waiters spending signals that are already there.
 *
 *  Each worker alternates -- signal, then wait -- on its own semaphore, so
 *  the excess it spends is always one it has just put there and the wait
 *  never blocks.  What is being checked is that spending and remembering
 *  are the same one step: a wait that reads a positive excess and then
 *  decrements it separately can decrement a zero that a concurrent waiter
 *  has already taken, and excessSignals goes NEGATIVE -- a semaphore that
 *  will hand out a signal nobody sent.
 */
static void
handshake_worker(st_worker *self, void *user)
{
    unsigned    round;
    st_oop      mine = semaphores[self->index % SEMAPHORE_COUNT];

    (void) user;
    ST_interp_register();
    give_this_worker_a_frame();
    for (round = 0; round < SIGNALS_PER_WORKER; ++round) {
        ST_push(mine);
        SCHED_primitive_signal();
        ST_pop_n(1);

        ST_push(mine);
        SCHED_primitive_wait();
        ST_pop_n(1);
        self->bytecodes += 2;
    }
    ST_interp_unregister();
}

/*
 *  Every worker must see its OWN active process, not one shared answer.
 *
 *  Processor>>activeProcess reads one instance variable, and one variable
 *  cannot answer a question with a different answer per thread.  This is
 *  the check that the primitive asks the caller: each worker nominates a
 *  process of its own and then reads it back, and no two may collide.
 */
static st_oop           worker_saw[64];
static st_atomic_int    distinct_failures;

static void
active_process_worker(st_worker *self, void *user)
{
    st_oop  mine;

    (void) user;
    ST_interp_register();
    give_this_worker_a_frame();

    /*
     *  A Process of this worker's own, made here rather than shared, and
     *  nominated the way a transfer nominates one.
     */
    mine = OM_instantiate_pointers(ST_NIL, 4);
    OM_increase_ref(mine);
    st_vm.active_process = mine;

    if (SCHED_active_process() != mine)
        ST_fetch_add_relaxed(&distinct_failures, 1);
    if (self->index < 64)
        worker_saw[self->index] = mine;
    ST_interp_unregister();
}

/*
 *  ----------  A process on its way off a list is never nowhere  ----------
 *
 *  Bugs4 MEM-1.  SCHED_primitive_detach is the only thing in this system
 *  allowed to conclude, without holding a lock, that a process is FREE --
 *  on no list and in no worker's hands -- and everything terminate,
 *  suspend and signalException: do afterwards rests on that conclusion
 *  being true.  It rests in turn on one rule, stated where the hands table
 *  is declared: a process is written into the slot it is moving TO before
 *  it is cleared from the one it is moving FROM.
 *
 *  take_first_runnable broke the rule for a process taken from the MIDDLE
 *  of a ready list -- the path used only while some other worker is
 *  detaching -- by unlinking first and publishing `taken' afterwards.  For
 *  the two or three instructions in between the process was on no list and
 *  in nobody's hands, a detacher looking at exactly that moment answered 0
 *  ("it was nowhere"), Process>>terminate wrote nil over the
 *  suspendedContext of a process another worker had already nominated, and
 *  that worker's switch was handed a nil to run: the image stopped in
 *  sixteen runs out of twenty, on thirty-two workers forking and
 *  terminating.
 *
 *  The invariant is checkable without any of that.  Every process here is
 *  always somewhere on purpose: each worker owns a few and puts each one
 *  straight back on a ready list after detaching it, and every worker also
 *  takes whatever the lists offer and puts it back at once.  Nothing is
 *  ever deliberately free, so a detach that answers 0 has seen the window
 *  and nothing else.  Ownership is disjoint so that two detachers cannot
 *  meet on one process, which is the only other way 0 could be honest.
 *  The rows belonging to workers this machine does not have are filled in
 *  anyway: nobody detaches them, so they are traffic on the lists, which
 *  is what keeps the takers busy.
 *
 *  It is a stress gate and not a proof -- the window is a few instructions
 *  wide, and it was entered in every one of six runs at this count and in
 *  half of them at a fifth of it.  Under a sanitizer the count is cut to
 *  keep the run short; the fault it is watching for is a missing store
 *  order, which no sanitizer reports.
 */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__) \
 || (defined(__has_feature) && (__has_feature(thread_sanitizer) \
                             || __has_feature(address_sanitizer)))
#define MEM1_ROUNDS         60
#else
#define MEM1_ROUNDS         20000
#endif
#define MEM1_OWNED          6

static st_oop           mem1_owned[64][MEM1_OWNED];
static st_atomic_int    mem1_nowhere;
static st_atomic_int    mem1_detaches;
/*  Somewhere for the hold loop below to store, so it cannot be elided. */
static st_atomic_int    mem1_spin;

static void
detach_and_take_worker(st_worker *self, void *user)
{
    unsigned    round;
    unsigned    i;
    st_oop      mine;

    (void) user;
    ST_interp_register();
    give_this_worker_a_frame();
    /*
     *  An active process of this worker's own: primitive 231 refuses the
     *  caller's own active process, and with none set SCHED_active_process
     *  falls back to the scheduler's shared field, which every worker
     *  would then be told is its own.
     */
    mine = OM_instantiate_pointers(ST_NIL, 4);
    OM_increase_ref(mine);
    st_vm.active_process = mine;

    if (self->index >= 64) {
        ST_interp_unregister();
        return;
    }
    for (round = 0; round < MEM1_ROUNDS; ++round) {
        st_oop  taken = SCHED_wake_highest_priority();

        /*
         *  Somebody's process, taken and put straight back -- the taker
         *  half.  With detaches in flight this goes through
         *  take_first_runnable's walk rather than its fast path, which is
         *  the code the window was in.
         */
        if (OM_is_present(taken)) {
            unsigned    spin;

            /*
             *  Held for a moment rather than handed straight back.  A
             *  process in a worker's hands is one a detacher must WAIT
             *  for, and a detacher that waits keeps its name in the
             *  table -- which is what puts a named process at the head
             *  of a ready list and sends the next taker down
             *  take_first_runnable's walk, the path the window was in.
             */
            for (spin = 0; spin < 64; ++spin)
                ST_fetch_add_relaxed(&mem1_spin, 1);
            SCHED_sleep(taken);
            OM_decrease_ref(taken);         /*  the removal's loan  */
        }
        for (i = 0; i < MEM1_OWNED; ++i) {
            st_oop  p = mem1_owned[self->index][i];
            st_oop  answer;

            ST_push(p);
            ST_push(ST_TRUE);
            if (!SCHED_primitive_detach()) {
                ST_pop_n(2);
                continue;
            }
            answer = ST_stack_top();
            ST_pop_n(1);
            ST_fetch_add_relaxed(&mem1_detaches, 1);
            if (OM_is_int(answer) && OM_int_value(answer) == 0)
                ST_fetch_add_relaxed(&mem1_nowhere, 1);
            /*  Parked and free now, which is the one moment it may be. */
            SCHED_sleep(p);
        }
        self->bytecodes += MEM1_OWNED;
    }
    ST_interp_unregister();
}

int
main(void)
{
    unsigned    workers;
    unsigned    i;
    unsigned    k;

    ST_TEST_BEGIN("processes and semaphores, in parallel");

    if (OM_init() != 0) {
        printf("  cannot initialize the object memory\n");
        CHECK(0);
        return ST_TEST_END();
    }
    build_fixed_objects();
    for (i = 0; i < SEMAPHORE_COUNT; ++i) {
        semaphores[i] = make_semaphore();
        CHECK(OM_is_present(semaphores[i]));
    }
    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();
    give_this_worker_a_frame();
    SCHED_reset();

    /*  ----------  Signals with no waiter must all be remembered  ---------- */

    ST_store_seq(&signals_sent, 0);
    CHECK_EQ_INT(WORKER_start(0, signalling_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %u threads sent %d signals to %d semaphores\n",
           workers, ST_load_seq(&signals_sent), SEMAPHORE_COUNT);
    CHECK_EQ_INT(ST_load_seq(&signals_sent),
                 (int) (workers * SIGNALS_PER_WORKER * SEMAPHORE_COUNT));
    for (i = 0; i < SEMAPHORE_COUNT; ++i) {
        /*
         *  The whole test in one line.  Every signal was sent to a
         *  semaphore nobody was waiting on, so every one of them must be
         *  sitting in excessSignals -- exactly, with nothing lost to a
         *  read-modify-write that overlapped another thread's.
         */
        CHECK_EQ_INT((int) excess_of(semaphores[i]),
                     (int) (workers * SIGNALS_PER_WORKER));
    }

    /*  ----------  Bugs4 PROC-6: the excess count saturates  ---------- */

    /*
     *  Chapter 29's `excessSignals _ excessSignals+1' promotes to a
     *  LargeInteger; the VM's version wrapped, and OM_int_oop of 2^62 is a
     *  NEGATIVE SmallInteger, which SCHED_primitive_wait reads as nothing
     *  owed -- so the next waiter on a semaphore holding four quintillion
     *  signals would have slept for ever.  Unreachable by counting to it,
     *  and one store away from being checked.
     */
    {
        st_oop  s = semaphores[0];

        OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, s,
                         OM_int_oop(ST_INT_MAX - 1));
        ST_push(s);
        SCHED_primitive_signal();
        ST_pop_n(1);
        CHECK(excess_of(s) == ST_INT_MAX);
        ST_push(s);
        SCHED_primitive_signal();
        ST_pop_n(1);
        /*  Stopped, and above all not turned over into a negative.  */
        CHECK(excess_of(s) == ST_INT_MAX);
        CHECK(excess_of(s) > 0);
    }

    /*  ----------  Signal-then-wait must balance to zero  ---------- */

    for (i = 0; i < SEMAPHORE_COUNT; ++i)
        OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphores[i],
                         OM_int_oop(0));

    CHECK_EQ_INT(WORKER_start(0, handshake_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    for (i = 0; i < SEMAPHORE_COUNT; ++i) {
        /*
         *  Zero, and never below it.  A negative excess is a semaphore
         *  that will hand out a signal nobody sent, which is the failure
         *  that looks like a lock working until the day it does not.
         */
        CHECK_EQ_INT((int) excess_of(semaphores[i]), 0);
        CHECK(excess_of(semaphores[i]) >= 0);
    }
    printf("  %u threads made %u signal/wait handshakes each\n",
           workers, SIGNALS_PER_WORKER);

    /*  ----------  Each worker's active process is its own  ---------- */

    ST_store_seq(&distinct_failures, 0);
    memset(worker_saw, 0, sizeof worker_saw);
    CHECK_EQ_INT(WORKER_start(0, active_process_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    CHECK_EQ_INT(ST_load_seq(&distinct_failures), 0);
    /*
     *  And no two workers saw the same one.  A shared answer would show up
     *  here as a duplicate rather than as a crash, which is exactly how a
     *  scheduler that is not really per-worker would look from outside.
     */
    for (i = 0; i < workers && i < 64; ++i) {
        CHECK(OM_is_present(worker_saw[i]));
        for (k = 0; k < i; ++k)
            CHECK(worker_saw[i] != worker_saw[k]);
    }
    printf("  %u threads each saw an active process of their own\n",
           workers < 64 ? workers : 64);

    /*  ----------  The two walks Smalltalk used to do itself  ---------- */

    build_scheduler();
    {
        st_oop  a = make_process(2);
        st_oop  b = make_process(2);

        /*  Nothing waiting yet.  */
        CHECK(!OM_is_present(SCHED_first_ready_process_at(2)));
        CHECK_EQ_INT(SCHED_remove_ready_process(a), 0);

        SCHED_sleep(a);
        SCHED_sleep(b);
        CHECK_EQ_INT((int) (SCHED_first_ready_process_at(2) == a), 1);

        /*
         *  Removing from the MIDDLE, which 1983 never had to do -- it only
         *  ever took from the head -- and which is why the walk is in the
         *  VM now rather than in Smalltalk.
         */
        CHECK_EQ_INT(SCHED_remove_ready_process(b), 1);
        CHECK_EQ_INT(SCHED_remove_ready_process(b), 0);   /*  not twice  */
        CHECK_EQ_INT((int) (SCHED_first_ready_process_at(2) == a), 1);

        /*  And the head, leaving the list empty and consistent.  */
        CHECK_EQ_INT(SCHED_remove_ready_process(a), 1);
        CHECK(!OM_is_present(SCHED_first_ready_process_at(2)));
        /*  Both are off every list, so either may be queued again.  */
        SCHED_sleep(a);
        CHECK_EQ_INT((int) (SCHED_first_ready_process_at(2) == a), 1);
        CHECK_EQ_INT(SCHED_remove_ready_process(a), 1);
    }

    /*  ----------  No process may be taken off a ready list twice  ---------- */

    {
        unsigned    priority;
        unsigned    n;

        for (priority = 1; priority <= PRIORITY_COUNT; ++priority)
            for (n = 0; n < PROCESSES_PER_LIST; ++n)
                SCHED_sleep(make_process((st_int) priority));
    }
    ST_store_seq(&processes_taken, 0);
    ST_store_seq(&taken_twice, 0);
    CHECK_EQ_INT(WORKER_start(0, draining_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    /*
     *  Exactly what was put in, and no more: one too many means a process
     *  was handed to two workers, one too few means one was lost between
     *  the look and the take.
     */
    CHECK_EQ_INT(ST_load_seq(&processes_taken),
                 (int) (PRIORITY_COUNT * PROCESSES_PER_LIST));
    CHECK_EQ_INT(ST_load_seq(&taken_twice), 0);
    printf("  %u threads drained %d ready processes, none twice\n",
           workers, ST_load_seq(&processes_taken));

    /*  ----------  Bugs4 SCHED-1: a taken process is never dropped  ------ */

    /*
     *  SCHED_suspend_active looks for something to run, and it can be
     *  holding one process while a drained signal NOMINATES another.  Both
     *  paths out of that state used to be `return', and the one it was
     *  holding -- off its ready list, its `taken' slot soon overwritten by
     *  the next take, alive only through the removal's loan -- was dropped:
     *  a live process with a good context, at a runnable priority, on no
     *  list and in nobody's hands, that nothing would ever look at again.
     *  Nothing reported it, because that is the state a SUSPENDED process
     *  is in on purpose; the image stopped later and elsewhere, the next
     *  time nothing could run.
     *
     *  The whole of it is reproducible without a signal: a nomination
     *  pending when the function is entered is the same state a drain
     *  leaves behind, and this checks the invariant rather than the route
     *  to it -- whatever it was holding is back on its ready list, and the
     *  nomination is untouched.
     */
    {
        st_oop      ready    = make_process(2);
        st_oop      nominee  = make_process(3);
        st_oop      running  = make_process(2);
        int         saved_disowned = st_vm.disowned;
        st_oop      saved_active = st_vm.active_process;

        SCHED_sleep(ready);
        CHECK_EQ_INT((int) (SCHED_first_ready_process_at(2) == ready), 1);

        /*  As a worker that has parked its own process and handed it on. */
        st_vm.active_process = running;
        st_vm.disowned       = 1;
        SCHED_transfer_to(nominee);
        CHECK_EQ_INT((int) (SCHED_pending_process() == nominee), 1);

        SCHED_suspend_active();

        /*  The nomination stands, and the ready process is still ready. */
        CHECK_EQ_INT((int) (SCHED_pending_process() == nominee), 1);
        CHECK_EQ_INT((int) (SCHED_first_ready_process_at(2) == ready), 1);
        CHECK_EQ_INT((int) OM_is_present(
                         OM_fetch_pointer(ST_PROCESS_MY_LIST, ready)), 1);

        SCHED_release_nomination();
        CHECK_EQ_INT(SCHED_remove_ready_process(ready), 1);
        CHECK_EQ_INT(SCHED_remove_ready_process(nominee), 1);
        st_vm.active_process = saved_active;
        st_vm.disowned       = saved_disowned;
    }

    /*  ----------  Bugs4 MEM-1: a process is never nowhere  ---------- */

    {
        unsigned    w;
        unsigned    n;

        for (w = 0; w < 64; ++w)
            for (n = 0; n < MEM1_OWNED; ++n) {
                st_oop  p = make_process((st_int) (n % PRIORITY_COUNT) + 1);

                mem1_owned[w][n] = p;
                SCHED_sleep(p);
            }
    }
    ST_store_seq(&mem1_nowhere, 0);
    ST_store_seq(&mem1_detaches, 0);
    CHECK_EQ_INT(WORKER_start(0, detach_and_take_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %u threads detached %d processes that were always somewhere\n",
           workers, ST_load_seq(&mem1_detaches));
    /*
     *  Not one of them may have been answered "nowhere".  Every process in
     *  this section is on a ready list or in the hands of the worker that
     *  has just taken it, at every instant; a 0 here is the window that
     *  stopped the image.
     */
    CHECK_EQ_INT(ST_load_seq(&mem1_nowhere), 0);

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: this needs the 64-bit object memory\n");
    return 0;
}

#endif
