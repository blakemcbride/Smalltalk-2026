/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The worker pool and safepoints -- the machinery that lets Smalltalk
 *  bytecodes run on more than one CPU.
 *
 *  Thread 0 is not here.  It is the SDL pump and never executes Smalltalk,
 *  because a worker parked in a garbage-collection safepoint at the moment
 *  the window server wants an answer would deadlock the compositor, and
 *  because macOS binds the Cocoa run loop to the thread that entered main().
 *  Workers are threads 1..N.
 *
 *  ----------  Safepoints  ----------
 *
 *  A collection cannot run while another thread is midway through a
 *  bytecode, so every worker has to reach a point where its registers are
 *  consistent and its roots are known.  Three mechanisms are available and
 *  we use the first:
 *
 *      1.  A polled flag, checked in the interpreter's dispatch loop.
 *      2.  Signals -- what Boehm and the MPS do on POSIX.
 *      3.  A guard page the collector mprotects, which HotSpot and OCaml
 *          use, trapping the mutator on a load.
 *
 *  Polling wins here for three reasons.  It is identical on Windows, which
 *  has no signal-based thread suspension at all.  It costs almost nothing
 *  against the price of interpreting a bytecode.  And decisively, it yields
 *  PRECISE roots: at a poll we know exactly which context slots hold object
 *  pointers, and precise roots are what make a moving, compacting collector
 *  possible later.  Conservative stack scanning would foreclose that.
 *
 *  The poll goes at message sends and backward jumps, which is enough to
 *  bound the time to safepoint: no bytecode sequence can run indefinitely
 *  without passing one.
 */

#ifndef ST_WORKER_H
#define ST_WORKER_H

#include "om.h"
#include "st_port.h"
#include "st_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST_MAX_WORKERS      64

typedef struct st_worker st_worker;

/*
 *  Per-worker state.  Anything on the interpreter's hot path lives here so
 *  that workers touch their own cache lines and not each other's.
 */
struct st_worker {
    unsigned        index;
    st_thread       thread;
    st_atomic_int   at_safepoint;
    st_atomic_int   running;
    /*
     *  Distinct from running.  A worker that has finished need not park; a
     *  worker that has not started yet must.  Both read running == 0 under
     *  the old timing, which is precisely how they came to be confused.
     */
    st_atomic_int   exited;

    /*  Statistics, for the scaling benchmark.  */
    uint64_t        bytecodes;
    uint64_t        allocations;

    void          (*body)(st_worker *self, void *user);
    void           *user;
};

/*
 *  Start and stop the pool.  `count` of zero means one worker per CPU,
 *  less the one reserved for the SDL pump.
 */
int         WORKER_start(unsigned count,
                         void (*body)(st_worker *self, void *user),
                         void *user);
void        WORKER_stop(void);

unsigned    WORKER_count(void);
st_worker  *WORKER_self(void);
/*
 *  Worker `i', or NULL past the end of the pool.  The object memory walks
 *  the pool to decide when a retired object is safe to reclaim, which needs
 *  every worker's state and not just its own.
 */
st_worker  *WORKER_at(unsigned index);

/*
 *  ----------  The safepoint protocol  ----------
 *
 *  A thread wanting exclusive access to the object memory calls
 *  WORKER_request_safepoint, which blocks until every worker has parked,
 *  and WORKER_release_safepoint when it is done.  Workers call
 *  WORKER_poll once per bytecode; it is a single relaxed atomic load in
 *  the common case and parks only when asked.
 */
/*
 *  How long the world has been stopped in total, and how many times.
 *
 *  Without it a scaling failure cannot be attributed and you will guess:
 *  eight workers going half as fast as four could be contending on a lock,
 *  thrashing a cache line, or simply parked while one of them collects, and
 *  those want three different fixes.
 */
int64_t     WORKER_safepoint_pause_ns(void);
int         WORKER_safepoint_count(void);
/*  The worst and best single stop, because a mean hides the shape.  */
int64_t     WORKER_safepoint_worst_ns(void);
int64_t     WORKER_safepoint_best_ns(void);
void        WORKER_reset_safepoint_statistics(void);

extern st_atomic_int    st_safepoint_requested;

static inline int
WORKER_poll_needed(void)
{
    return ST_load_relaxed(&st_safepoint_requested) != 0;
}

void    WORKER_poll_slow(void);

static inline void
WORKER_poll(void)
{
    if (WORKER_poll_needed())
        WORKER_poll_slow();
}

void    WORKER_request_safepoint(void);
void    WORKER_release_safepoint(void);

/*
 *  ----------  Blocking outside the object memory  ----------
 *
 *  A worker that calls into a library which may block -- a database driver
 *  waiting on a socket is the case this was written for -- stops reaching
 *  WORKER_poll, because it is not running bytecodes.  Nothing is wrong with
 *  that until a safepoint is asked for, and then everything is: the
 *  requester waits for a worker that is waiting for a server, and one slow
 *  query stops every core for as long as the query takes.
 *
 *  So the worker declares the blocking region.  WORKER_enter_native parks it
 *  exactly as the poll would -- registers written back, counted among the
 *  parked -- and WORKER_leave_native waits for any safepoint in progress and
 *  rejoins.  The collector then runs while the worker is inside the driver,
 *  which is correct precisely because the worker has promised not to touch
 *  the object memory in there.
 *
 *  THE PROMISE IS NOT CHECKED, so it must be kept by construction: copy
 *  every argument out of the object memory before entering, and build every
 *  result after leaving.  An OOP itself survives -- it is an object-table
 *  index, and the collector may move the object but not the entry -- but a
 *  raw pointer into an object's bytes does not, and neither does the
 *  reachability of anything the parked worker's context does not name.
 *
 *  Nesting is not supported; a region is entered and left once.
 */
void    WORKER_enter_native(void);
void    WORKER_leave_native(void);

/*  Run fn with every worker parked.  Returns what fn returned.  */
uint32_t WORKER_at_safepoint(uint32_t (*fn)(void *user), void *user);

/*
 *  How many workers are neither parked nor finished, excluding the caller.
 *
 *  Inside a safepoint this must be zero -- that IS the safepoint's contract,
 *  and it is what makes the collector's exclusive access exclusive.  It is
 *  exported so a test can assert the property directly rather than wait for
 *  a violation to corrupt something and hope a sanitizer notices.
 */
unsigned WORKER_unparked_count(void);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_WORKER_H  */
