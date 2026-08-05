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
 *  ----------  The safepoint protocol  ----------
 *
 *  A thread wanting exclusive access to the object memory calls
 *  WORKER_request_safepoint, which blocks until every worker has parked,
 *  and WORKER_release_safepoint when it is done.  Workers call
 *  WORKER_poll once per bytecode; it is a single relaxed atomic load in
 *  the common case and parks only when asked.
 */
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

/*  Run fn with every worker parked.  Returns what fn returned.  */
uint32_t WORKER_at_safepoint(uint32_t (*fn)(void *user), void *user);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_WORKER_H  */
