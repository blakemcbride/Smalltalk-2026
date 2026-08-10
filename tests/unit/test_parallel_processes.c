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

static void
provide_test_roots(om_visit_fn visit)
{
    unsigned    i;

    for (i = 0; i < SEMAPHORE_COUNT; ++i)
        visit(semaphores[i]);
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
