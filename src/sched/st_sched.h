/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Processes and semaphores: Blue Book Chapter 29, control primitives.
 *
 *  This is the green scheduler -- cooperative, one process running at a
 *  time, exactly as Smalltalk-80 specifies.  It is what the image expects
 *  and what makes the display interactive: the input process waits on a
 *  semaphore that the VM signals when SDL delivers an event.
 *
 *  Phase 7 parallelizes this by multiplexing these same Process objects over
 *  a pool of native threads.  The structure here is chosen with that in
 *  mind: every state change goes through one of the operations below, so
 *  there is a single place to make them atomic.  What changes then is the
 *  guarantee, not the shape -- see doc/CONCURRENCY.md.
 */

#ifndef ST_SCHED_H_INCLUDED
#define ST_SCHED_H_INCLUDED

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Layouts, confirmed against the version 2 sources:
 *
 *      Object -> Link                        'nextLink'
 *      Link -> Process                       'suspendedContext priority myList'
 *      SequenceableCollection -> LinkedList  'firstLink lastLink'
 *      LinkedList -> Semaphore               'excessSignals'
 *      Object -> ProcessorScheduler          'quiescentProcessLists activeProcess'
 */
#define ST_LINK_NEXT                0

#define ST_PROCESS_SUSPENDED_CONTEXT 1
#define ST_PROCESS_PRIORITY          2
#define ST_PROCESS_MY_LIST           3

#define ST_LIST_FIRST_LINK          0
#define ST_LIST_LAST_LINK           1

#define ST_SEMAPHORE_EXCESS_SIGNALS 2

#define ST_SCHEDULER_PROCESS_LISTS  0
#define ST_SCHEDULER_ACTIVE_PROCESS 1

/*  The scheduler object itself, from the guaranteed association.  */
st_oop      SCHED_scheduler(void);
st_oop      SCHED_active_process(void);

/*  Blue Book list operations.  */
int         SCHED_is_empty_list(st_oop list);
st_oop      SCHED_remove_first_link(st_oop list);
st_oop      SCHED_pending_process(void);
void        SCHED_add_last_link(st_oop link, st_oop list);

/*  Process state changes.  */
/*
 *  Take the first process off the highest-priority non-empty ready list,
 *  or nil.  Finding and taking are one step, under the ready lock -- two
 *  workers that both look and both take would run one process on two
 *  native threads.
 */
st_oop      SCHED_wake_highest_priority(void);

void        SCHED_sleep(st_oop process);
void        SCHED_resume(st_oop process);
void        SCHED_suspend_active(void);
void        SCHED_transfer_to(st_oop process);
void        SCHED_synchronous_signal(st_oop semaphore);

/*
 *  Signal a semaphore from outside Smalltalk -- from the SDL event pump, or
 *  a timer.  The signal is queued and delivered at the next process-switch
 *  check rather than applied immediately, because the interpreter may be
 *  midway through a bytecode.
 */
void        SCHED_asynchronous_signal(st_oop semaphore);

/*
 *  Called once per bytecode.  Delivers queued asynchronous signals and
 *  performs any pending process switch.  This is also where the safepoint
 *  poll will live once threads arrive.
 */
void        SCHED_check_process_switch(void);

/*  Primitives 85 to 88.  */
int         SCHED_primitive_signal(void);
int         SCHED_primitive_wait(void);
int         SCHED_primitive_resume(void);
int         SCHED_primitive_suspend(void);

/*  Set by primitive 93; signalled when input arrives.  */
void        SCHED_set_input_semaphore(st_oop semaphore);
st_oop      SCHED_input_semaphore(void);

void        SCHED_reset(void);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SCHED_H_INCLUDED  */
