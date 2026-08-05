/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The Phase 7 gate: many native threads mutating one shared object memory.
 *
 *  This is what the whole project exists for.  Every production Smalltalk
 *  runs its processes on a single OS thread; here the object memory is
 *  shared and the threads are real, so the properties that have to hold are
 *  the ones a green scheduler never has to think about:
 *
 *      - reference counts survive concurrent increment and decrement
 *      - allocation from several threads hands out distinct objects
 *      - a collection sees a consistent heap, because every mutator is
 *        parked at a safepoint first
 *      - become: exchanges two identities atomically
 *
 *  Run under the thread sanitizer, which is where this suite earns its
 *  keep: `make OM=mt TSAN=1 test`.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "worker.h"
#include "st_port.h"
#include "st_atomic.h"

#include <stdio.h>
#include <string.h>

#define FIXED_OBJECTS   (ST_SELECTOR_CANNOT_INTERPRET / 2)

static void
build_fixed_objects(void)
{
    int i;

    for (i = 1; i <= FIXED_OBJECTS; ++i) {
        st_oop  p = OM_instantiate_pointers(ST_NIL, 0);

        /*  Permanent, as in a real image; see test_om_mt.c.  */
        OM_increase_ref(p);
    }
}

/*  ----------  Shared state the workers hammer  ----------  */

static st_oop           shared_root;
static st_atomic_int    allocations;
static st_atomic_int    stores;

static void
provide_roots(om_visit_fn visit)
{
    visit(shared_root);
}

/*
 *  Each worker allocates, publishes into a slot of the shared array, and
 *  drops its own reference.  Slots collide on purpose: two threads writing
 *  the same slot is exactly the race that has to be safe.
 */
#define SLOTS           64
#define ROUNDS          400

static void
mutator(st_worker *self, void *user)
{
    unsigned    round;

    (void) user;
    for (round = 0; round < ROUNDS; ++round) {
        st_oop      object;
        uint32_t    slot = (self->index * 7 + round) % SLOTS;

        WORKER_poll();

        object = OM_instantiate_pointers(ST_CLASS_ARRAY, 3);
        if (object == ST_OOP_INVALID)
            continue;
        ST_fetch_add_relaxed(&allocations, 1);
        ++self->allocations;

        /*  Hold it while it is published, or a collision could free it.  */
        OM_increase_ref(object);
        OM_store_pointer(0, object, OM_int_oop((st_int) round));
        OM_store_pointer(1, object, ST_TRUE);
        OM_store_pointer(slot, shared_root, object);
        ST_fetch_add_relaxed(&stores, 1);
        OM_decrease_ref(object);

        ++self->bytecodes;
    }
}

static void
test_concurrent_mutation(void)
{
    unsigned    workers;
    unsigned    i;
    int         live_slots = 0;

    CHECK_EQ_INT(OM_init(), 0);
    build_fixed_objects();

    shared_root = OM_instantiate_pointers(ST_CLASS_ARRAY, SLOTS);
    OM_increase_ref(shared_root);
    OM_set_root_provider(provide_roots);

    ST_store_seq(&allocations, 0);
    ST_store_seq(&stores, 0);

    CHECK_EQ_INT(WORKER_start(0, mutator, NULL), 0);
    workers = WORKER_count();
    printf("  %u workers on %d CPUs\n", workers, ST_cpu_count());
    CHECK(workers >= 1);
    WORKER_stop();

    CHECK_EQ_INT(ST_load_seq(&allocations), (int) (workers * ROUNDS));
    CHECK_EQ_INT(ST_load_seq(&stores), (int) (workers * ROUNDS));

    /*  Every slot must hold a well-formed object, none of them torn.  */
    for (i = 0; i < SLOTS; ++i) {
        st_oop  slot = OM_fetch_pointer(i, shared_root);

        if (slot == ST_NIL)
            continue;
        ++live_slots;
        CHECK(OM_is_object(slot));
        if (!OM_is_object(slot))
            continue;
        CHECK_EQ_INT(OM_fetch_class(slot), ST_CLASS_ARRAY);
        CHECK_EQ_INT(OM_fetch_word_length(slot), 3);
        CHECK_EQ_INT(OM_fetch_pointer(1, slot), ST_TRUE);
    }
    printf("  %d of %d slots hold a surviving object\n", live_slots, SLOTS);
    CHECK(live_slots > 0);

    OM_set_root_provider(NULL);
    OM_shutdown();
}

/*
 *  The same, with a collection running concurrently.  The collector parks
 *  every mutator first; if that protocol is wrong this test corrupts the
 *  heap and the checks above it start failing.
 */
static st_atomic_int    collections_done;

static void
mutator_with_gc(st_worker *self, void *user)
{
    unsigned    round;

    (void) user;
    for (round = 0; round < ROUNDS; ++round) {
        st_oop      object;
        uint32_t    slot = (self->index * 13 + round) % SLOTS;

        WORKER_poll();

        object = OM_instantiate_pointers(ST_CLASS_ARRAY, 2);
        if (object == ST_OOP_INVALID)
            continue;
        OM_increase_ref(object);
        OM_store_pointer(0, object, OM_int_oop((st_int) self->index));
        OM_store_pointer(slot, shared_root, object);
        OM_decrease_ref(object);

        /*
         *  One worker also collects, which requires it to stop every other
         *  worker including itself -- the requester must not be counted as
         *  a thread that still has to park.
         */
        if (self->index == 0 && (round % 100) == 99) {
            OM_collect();
            ST_fetch_add_relaxed(&collections_done, 1);
        }
    }
}

static void
test_collection_under_mutation(void)
{
    unsigned    i;
    unsigned    workers;
    int         intact = 0;

    CHECK_EQ_INT(OM_init(), 0);
    build_fixed_objects();

    shared_root = OM_instantiate_pointers(ST_CLASS_ARRAY, SLOTS);
    OM_increase_ref(shared_root);
    OM_set_root_provider(provide_roots);
    ST_store_seq(&collections_done, 0);

    CHECK_EQ_INT(WORKER_start(0, mutator_with_gc, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %d collections ran while %u workers mutated\n",
           ST_load_seq(&collections_done), workers);
    CHECK(ST_load_seq(&collections_done) > 0);

    for (i = 0; i < SLOTS; ++i) {
        st_oop  slot = OM_fetch_pointer(i, shared_root);

        if (slot == ST_NIL)
            continue;
        CHECK(OM_is_object(slot));
        if (OM_is_object(slot)) {
            CHECK_EQ_INT(OM_fetch_class(slot), ST_CLASS_ARRAY);
            ++intact;
        }
    }
    printf("  %d slots intact after collection\n", intact);
    CHECK(intact > 0);

    OM_set_root_provider(NULL);
    OM_shutdown();
}

/*
 *  Reference counting under contention.  Every worker takes and releases a
 *  reference to one object many times; the count must return to where it
 *  started and the object must still be alive.
 */
static st_oop   contended;

static void
contend_on_counts(st_worker *self, void *user)
{
    unsigned    i;

    (void) user;
    (void) self;
    for (i = 0; i < 5000; ++i) {
        OM_increase_ref(contended);
        WORKER_poll();
        OM_decrease_ref(contended);
    }
}

static void
test_reference_counts_under_contention(void)
{
    CHECK_EQ_INT(OM_init(), 0);
    build_fixed_objects();

    contended   = OM_instantiate_bytes(ST_CLASS_STRING, 8);
    shared_root = contended;
    OM_increase_ref(contended);         /*  one holder: this thread  */
    OM_set_root_provider(provide_roots);

    CHECK_EQ_INT(WORKER_start(0, contend_on_counts, NULL), 0);
    WORKER_stop();

    /*  Balanced traffic leaves exactly the one reference we took.  */
    CHECK(OM_is_object(contended));
    CHECK_EQ_INT(OM_count_bits(contended), 1);

    OM_set_root_provider(NULL);
    OM_shutdown();
}

int
main(void)
{
    ST_TEST_BEGIN("parallel object memory");

    test_concurrent_mutation();
    test_collection_under_mutation();
    test_reference_counts_under_contention();

    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: parallelism is a property of the 64-bit object memory\n");
    return 0;
}

#endif
