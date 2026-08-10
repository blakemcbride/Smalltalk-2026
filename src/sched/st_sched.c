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
static int      async_count;
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

void
SCHED_suspend_active(void)
{
    st_oop  next = SCHED_wake_highest_priority();

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
    if (async_count < ASYNC_QUEUE_MAX)
        async_queue[async_count++] = semaphore;
    /*  else drop rather than corrupt, as before  */
    ST_mutex_unlock(&async_lock);
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
    if (async_count > 0) {
        st_oop      pending[ASYNC_QUEUE_MAX];
        int         count;
        int         i;

        ST_mutex_lock(&async_lock);
        count = async_count;
        for (i = 0; i < count; ++i)
            pending[i] = async_queue[i];
        async_count = 0;
        ST_mutex_unlock(&async_lock);

        for (i = 0; i < count; ++i)
            SCHED_synchronous_signal(pending[i]);
    }
    if (!new_process_waiting)
        return;
    new_process_waiting = 0;

    /*
     *  Park the running process's context in its Process object, then make
     *  the incoming one's context active.  Everything the old process needs
     *  to resume is in that one pointer.
     */
    ST_store_active_context();
    OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, SCHED_active_process(),
                     st_vm.active_context);
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
