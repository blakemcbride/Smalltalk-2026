/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The mourn queue: the half of ephemeron support that is not reachability.
 *
 *  The collector decides which ephemerons have a key nothing else holds.
 *  That is the half that decides whether memory is CORRECT, and it lives in
 *  om_mt.c.  This is the other half, and it decides whether an ephemeron is
 *  of any use: a WeakKeyAssociation whose key has died has to be told so,
 *  because the only thing that can take it out of its dictionary is the
 *  association itself, in Smalltalk, by sending removeKey: to the container
 *  it remembers.  A collector cannot do that -- it runs with every worker
 *  parked and no process to run a send in.
 *
 *  So the collector queues them here and signals a Semaphore, and a process
 *  in the image at a high priority wakes, takes them one at a time through
 *  primitive 236, and sends each one #mourn.  Without it a WeakKeyDictionary
 *  keeps every entry it was ever given: the keys are reclaimed correctly and
 *  the dictionary never hears about it, which is a leak with a correct
 *  collector underneath it (Bugs4 MEM-2).
 *
 *  Shared by both object memories, because the primitive and the image-side
 *  process are the same in both.  The Blue Book memory queues nothing --
 *  OM_instantiate_ephemeron there makes an ordinary object -- so the queue
 *  is simply always empty and the process never wakes.
 */

#ifndef ST_OM_FINALIZE_H
#define ST_OM_FINALIZE_H

#include "om.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Queue an ephemeron for #mourn.  Called by the collector, at a safepoint,
 *  after it has resurrected the ephemeron's fields -- the queue holds it and
 *  its key alive for one more cycle, which is what makes `container
 *  removeKey: key' possible at all.
 */
void        OM_mourn_queue_add(st_oop ephemeron);

/*  The queued ephemerons, as roots.  Called from inside the mark phase.  */
void        OM_mourn_queue_visit(om_visit_fn visit);

/*  How many are waiting.  Zero means the finalization process has caught up. */
uint32_t    OM_mourn_pending(void);

/*
 *  Take the oldest queued ephemeron, or ST_NIL when there is none.  The
 *  primitive behind Finalizer>>primNextMournedObjectSignalling:.
 */
st_oop      OM_take_mourned(void);

/*
 *  The Semaphore the collector signals when it queues something, as the
 *  image hands it over.  Held as a root for as long as it is registered,
 *  since the only other reference is an instance variable of a process that
 *  is asleep on it.
 */
void        OM_set_mourn_semaphore(st_oop semaphore);
st_oop      OM_mourn_semaphore(void);

/*
 *  Wake the finalization process if anything is waiting.  Called once a
 *  collection has finished and the workers are running again; not from
 *  inside the safepoint, where a signal would be queued behind the very
 *  workers that are parked.
 */
void        OM_mourn_wake(void);

/*  Forget everything, for OM_shutdown and for a test that re-inits.  */
void        OM_mourn_reset(void);

/*  236: Finalizer class>>primNextMournedObjectSignalling:  */
int         OM_primitive_next_mourned(void);

#ifdef __cplusplus
}
#endif

#endif  /*  ST_OM_FINALIZE_H  */
