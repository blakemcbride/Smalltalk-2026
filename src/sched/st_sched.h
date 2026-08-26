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

/*
 *  The two things ProcessorScheduler used to do to the ready lists from
 *  Smalltalk, done inside the VM under the ready lock instead.  While those
 *  methods walked the array themselves the lists could not be split per
 *  worker; asking the VM means the VM can keep the processes where it likes.
 *
 *  remove answers whether the process was waiting for the processor at all.
 *  Only its own priority's ready list is considered: a process waiting on a
 *  semaphore is not waiting for the processor, and taking it off that list
 *  would lose the signal it is waiting for.
 */
int         SCHED_remove_ready_process(st_oop process);
st_oop      SCHED_first_ready_process_at(st_int priority);

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
/*
 *  Answers 1 if the signal was queued and 0 if the queue was full and it
 *  was dropped.  The pump and the timer ignore the answer; the network
 *  I/O thread keeps its socket armed and tries again.
 */
int         SCHED_asynchronous_signal(st_oop semaphore);

/*  The same, as the hook the network layer takes: a token is an oop.  */
int         SCHED_signal_token(uintptr_t token);

/*
 *  Make the async queue's lock before starting a thread that will post
 *  to it, on the thread that starts it.
 */
void        SCHED_async_init(void);

/*  The queued semaphores, for the root walk.  */
void        SCHED_visit_async_roots(om_visit_fn visit);

/*
 *  Something outside the scheduler that will end a wait -- the network
 *  layer's armed sockets.  Asked, beside the delay timer, before the idle
 *  loop declares that nothing can ever run again.
 */
void        SCHED_set_external_wait_hook(int (*hook)(void));

/*
 *  Stop every worker at its next bytecode boundary or idle slice.  Safe
 *  from a signal handler: one atomic store.
 */
void        SCHED_request_stop(void);
int         SCHED_stop_requested(void);

/*
 *  Join the scheduler with no process of one's own -- what a `-serve'
 *  worker other than the first does.  Returns with a nomination pending
 *  for the interpreter loop to act on, or with st_vm.running cleared.
 */
void        SCHED_enter_idle(void);

/*
 *  Called once per bytecode.  Delivers queued asynchronous signals and
 *  performs any pending process switch.  This is also where the safepoint
 *  poll will live once threads arrive.
 */
void        SCHED_check_process_switch(void);
void    SCHED_release_nomination(void);   /*  a run that ends hands its nominee back  */

/*  Primitives 85 to 88.  */
int         SCHED_primitive_signal(void);
int         SCHED_primitive_yield(void);      /*  167  */
int         SCHED_primitive_wait(void);
int         SCHED_primitive_resume(void);
int         SCHED_primitive_suspend(void);

/*  Set by primitive 93; signalled when input arrives.  */
/*
 *  The delay timer -- primitive 136.  See the block in st_sched.c for why
 *  it has to be a thread of its own rather than a poll in the idle loop.
 */
void        SCHED_signal_at_ms(st_oop semaphore, uint32_t target_ms);
int         SCHED_timer_pending(void);
st_oop      SCHED_timer_semaphore(void);
void        SCHED_timer_stop(void);

void        SCHED_set_input_semaphore(st_oop semaphore);
st_oop      SCHED_input_semaphore(void);

/*
 *  Called while no process can run and the system is waiting on the timer.
 *
 *  An idling interpreter never returns to whoever called ST_interp_run, so
 *  without this the window is only serviced at the end of a bytecode slice
 *  -- and a slice that covers several delays covers seconds.  The hook is
 *  the host's chance to keep the screen honest while the image sleeps.
 */
void        SCHED_set_idle_hook(void (*hook)(void));

void        SCHED_reset(void);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_SCHED_H_INCLUDED  */
