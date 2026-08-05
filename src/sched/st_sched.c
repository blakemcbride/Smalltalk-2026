/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Processes and semaphores.  See sched.h for the contract.
 */

#include "st_sched.h"
#include "interp.h"
#include "prim.h"

#include <stdio.h>
#include <string.h>

/*
 *  Asynchronous signals are queued rather than applied where they arise.
 *  An event can land while the interpreter is midway through a bytecode, and
 *  switching processes at that moment would leave the stack inconsistent, so
 *  the queue is drained at the next process-switch check.
 */
#define ASYNC_QUEUE_MAX     64

static st_oop   async_queue[ASYNC_QUEUE_MAX];
static int      async_count;

static st_oop   input_semaphore = ST_NIL;
static int      new_process_waiting;
static st_oop   new_process;

void
SCHED_reset(void)
{
    async_count         = 0;
    input_semaphore     = ST_NIL;
    new_process_waiting = 0;
    new_process         = ST_NIL;
}

st_oop
SCHED_scheduler(void)
{
    return OM_fetch_pointer(ST_ASSOCIATION_VALUE, ST_SCHEDULER_ASSOCIATION);
}

st_oop
SCHED_active_process(void)
{
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
    st_oop  first = OM_fetch_pointer(ST_LIST_FIRST_LINK, list);
    st_oop  last;
    st_oop  next;

    if (first == ST_NIL)
        return ST_NIL;
    last = OM_fetch_pointer(ST_LIST_LAST_LINK, list);
    if (first == last) {
        OM_store_pointer(ST_LIST_FIRST_LINK, list, ST_NIL);
        OM_store_pointer(ST_LIST_LAST_LINK, list, ST_NIL);
    }  else  {
        next = OM_fetch_pointer(ST_LINK_NEXT, first);
        OM_store_pointer(ST_LIST_FIRST_LINK, list, next);
    }
    OM_store_pointer(ST_LINK_NEXT, first, ST_NIL);
    return first;
}

void
SCHED_add_last_link(st_oop link, st_oop list)
{
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
    if (!OM_is_object(lists))
        return;
    if (OM_int_value(priority) < 1
     || (uint32_t) OM_int_value(priority) > OM_fetch_word_length(lists))
        return;
    list = OM_fetch_pointer((uint32_t) OM_int_value(priority) - 1, lists);
    SCHED_add_last_link(process, list);
}

void
SCHED_transfer_to(st_oop process)
{
    new_process_waiting = 1;
    new_process         = process;
}

/*
 *  Find the highest-priority runnable process.  Scanning from the top is
 *  what makes priorities preemptive between levels.
 */
static st_oop
wake_highest_priority(void)
{
    st_oop      lists = OM_fetch_pointer(ST_SCHEDULER_PROCESS_LISTS,
                                         SCHED_scheduler());
    uint32_t    count;
    uint32_t    i;

    if (!OM_is_object(lists))
        return ST_NIL;
    count = OM_fetch_word_length(lists);
    for (i = count; i > 0; --i) {
        st_oop  list = OM_fetch_pointer(i - 1, lists);

        if (OM_is_object(list) && !SCHED_is_empty_list(list))
            return SCHED_remove_first_link(list);
    }
    return ST_NIL;
}

void
SCHED_suspend_active(void)
{
    st_oop  next = wake_highest_priority();

    if (next == ST_NIL) {
        fprintf(stderr, "st80: every process is blocked; nothing can run\n");
        st_vm.running = 0;
        return;
    }
    SCHED_transfer_to(next);
}

/*
 *  Resuming a higher-priority process preempts the active one; resuming a
 *  lower or equal one merely queues it.  Under the green scheduler this is
 *  the whole of the preemption rule.
 */
void
SCHED_resume(st_oop process)
{
    st_oop  active = SCHED_active_process();
    st_oop  active_priority;
    st_oop  new_priority;

    if (!OM_is_object(process))
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

    if (!OM_is_object(semaphore))
        return;
    if (SCHED_is_empty_list(semaphore)) {
        /*  Nobody is waiting, so the signal is remembered.  */
        excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore);
        if (OM_is_int(excess))
            OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                             OM_int_oop(OM_int_value(excess) + 1));
        return;
    }
    SCHED_resume(SCHED_remove_first_link(semaphore));
}

void
SCHED_asynchronous_signal(st_oop semaphore)
{
    if (!OM_is_object(semaphore))
        return;
    if (async_count >= ASYNC_QUEUE_MAX)
        return;                 /*  drop rather than corrupt  */
    async_queue[async_count++] = semaphore;
}

void
SCHED_check_process_switch(void)
{
    while (async_count > 0) {
        st_oop  semaphore = async_queue[0];
        int     i;

        for (i = 1; i < async_count; ++i)
            async_queue[i - 1] = async_queue[i];
        --async_count;
        SCHED_synchronous_signal(semaphore);
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
    OM_store_pointer(ST_SCHEDULER_ACTIVE_PROCESS, SCHED_scheduler(),
                     new_process);
    ST_set_active_context(
        OM_fetch_pointer(ST_PROCESS_SUSPENDED_CONTEXT, new_process));
}

/*  ----------  Primitives 85 to 88  ----------  */

int
SCHED_primitive_signal(void)
{
    st_oop  semaphore = ST_stack_top();

    if (!OM_is_object(semaphore))
        return 0;
    if (OM_fetch_class(semaphore) != ST_CLASS_SEMAPHORE)
        return 0;
    SCHED_synchronous_signal(semaphore);
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
    excess = OM_fetch_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore);
    if (!OM_is_int(excess))
        return 0;
    if (OM_int_value(excess) > 0) {
        OM_store_pointer(ST_SEMAPHORE_EXCESS_SIGNALS, semaphore,
                         OM_int_oop(OM_int_value(excess) - 1));
        return 1;
    }
    SCHED_add_last_link(SCHED_active_process(), semaphore);
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
    input_semaphore = semaphore;
}

st_oop
SCHED_input_semaphore(void)
{
    return input_semaphore;
}
