/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Smalltalk bytecodes executing on several CPUs at once.
 *
 *  This is the claim the whole project rests on.  Every production
 *  Smalltalk -- Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk -- runs its
 *  processes as green threads on one OS thread; here each worker has its own
 *  interpreter registers and they all send messages into one shared object
 *  memory at the same time.
 *
 *  What is being checked is not that it runs but that it is RIGHT: every
 *  worker computes an answer only it can verify, so a torn field, a lost
 *  reference count or a context freed under another thread shows up as
 *  arithmetic that does not add up.  Run it under the thread sanitizer,
 *  where it earns its keep: make OM=mt TSAN=1 test
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "worker.h"
#include "st_port.h"
#include "st_atomic.h"

#include <stdio.h>
#include <string.h>

#define KERNEL      "kernel/Kernel.st"
#define ROUNDS      120

/*
 *  Compile an expression against the shared image.  Compilation allocates,
 *  so it happens on the main thread before the workers start; what the
 *  workers do is execute, which is the part that has to be parallel.
 */
static st_oop
compile_expression(const char *expression)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[512];

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.lookup_global      = BOOT_lookup_global;

    snprintf(source, sizeof source, "doIt ^%s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile \"%s\": %s\n", expression, res.error);
        return ST_OOP_INVALID;
    }
    /*  Pinned: nothing in the image refers to a doIt.  */
    OM_increase_ref(res.method);
    return res.method;
}

/*
 *  Run a compiled method to completion on this thread.  Each call builds its
 *  own context, so two threads running the same method share the code and
 *  nothing else.
 */
static st_oop
run_method(st_oop method)
{
    st_oop  context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 32);

    if (!OM_is_present(context))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int) (BOOT_method_initial_ip(method) + 1)));
    OM_store_pointer(ST_CTX_SP, context, OM_int_oop(0));

    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(200000);
    if (st_vm.running)
        return ST_OOP_INVALID;
    return st_vm.return_value;
}

/*  ----------  The shared work  ----------  */

/*
 *  The compiled expressions are held only by C, so they have to be roots.
 *  Counting a reference from C is not enough: a collection recomputes every
 *  count from the root walk, so anything the walk cannot reach is freed no
 *  matter how many references C believes it holds.
 */
static st_oop           method_arithmetic;
static st_oop           method_sends;
static st_oop           method_allocating;
static st_atomic_int    wrong_answers;
static st_atomic_int    evaluations;

static void
provide_test_roots(om_visit_fn visit)
{
    visit(method_arithmetic);
    visit(method_sends);
    visit(method_allocating);
}

static void
interpreter_worker(st_worker *self, void *user)
{
    unsigned    round;

    (void) user;

    /*  Without this the collector cannot see this thread's stack.  */
    ST_interp_register();

    for (round = 0; round < ROUNDS; ++round) {
        st_oop  value;

        /*  Arithmetic, answered by primitives.  */
        value = run_method(method_arithmetic);
        if (!OM_is_int(value) || OM_int_value(value) != 40)
            ST_fetch_add_relaxed(&wrong_answers, 1);

        /*  A chain of real message sends and method lookups.  */
        value = run_method(method_sends);
        if (value != ST_TRUE)
            ST_fetch_add_relaxed(&wrong_answers, 1);

        /*  Allocation, which contends on the object table.  */
        value = run_method(method_allocating);
        if (!OM_is_present(value)
         || OM_fetch_class(value) != BOOT_global("Point"))
            ST_fetch_add_relaxed(&wrong_answers, 1);

        ST_fetch_add_relaxed(&evaluations, 3);
        self->bytecodes += 3;
    }
    ST_interp_unregister();
}

static void
test_parallel_execution(void)
{
    static const char  *paths[1] = { KERNEL };
    st_bootstrap_result boot;
    unsigned            workers;

    if (BOOT_build(paths, 1, &boot) != 0) {
        printf("  bootstrap failed: %s\n", boot.error);
        CHECK(0);
        return;
    }
    printf("  image: %u classes, %u methods\n", boot.classes_created,
           boot.methods_compiled);

    method_arithmetic = compile_expression("(2 + 3) * (4 + 4)");
    method_sends      = compile_expression("3 isNil not");
    method_allocating = compile_expression("3 @ 4");
    CHECK(method_arithmetic != ST_OOP_INVALID);
    CHECK(method_sends      != ST_OOP_INVALID);
    CHECK(method_allocating != ST_OOP_INVALID);
    if (method_arithmetic == ST_OOP_INVALID)
        return;

    /*  Confirm the answers on one thread before asking many for them.  */
    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();
    CHECK_EQ_INT(OM_int_value(run_method(method_arithmetic)), 40);
    CHECK_EQ_INT(run_method(method_sends), ST_TRUE);

    ST_store_seq(&wrong_answers, 0);
    ST_store_seq(&evaluations, 0);

    CHECK_EQ_INT(WORKER_start(0, interpreter_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %u threads interpreted %d expressions on %d CPUs\n",
           workers, ST_load_seq(&evaluations), ST_cpu_count());

    /*
     *  Every answer correct.  These are not incidental checks: the sends
     *  walk shared method dictionaries, the allocations contend for object
     *  table entries, and the contexts are created and released on every
     *  round by every thread.
     */
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&evaluations), (int) (workers * ROUNDS * 3));
    CHECK(workers >= 1);

    ST_interp_unregister();
    OM_shutdown();
}

/*
 *  The same, with a collection running underneath.  Contexts are being
 *  created and abandoned on every thread while the collector walks the
 *  graph, so if the safepoint protocol or the root set is wrong this
 *  reclaims a running thread's stack and the answers stop adding up.
 */
static st_atomic_int    collections;

static void
worker_with_collections(st_worker *self, void *user)
{
    unsigned    round;

    (void) user;
    ST_interp_register();
    for (round = 0; round < ROUNDS; ++round) {
        st_oop  value = run_method(method_arithmetic);

        if (!OM_is_int(value) || OM_int_value(value) != 40)
            ST_fetch_add_relaxed(&wrong_answers, 1);
        ST_fetch_add_relaxed(&evaluations, 1);

        if (self->index == 0 && (round % 30) == 29) {
            OM_collect();
            ST_fetch_add_relaxed(&collections, 1);
        }
    }
    ST_interp_unregister();
}

static void
test_collection_under_execution(void)
{
    static const char  *paths[1] = { KERNEL };
    st_bootstrap_result boot;
    unsigned            workers;

    if (BOOT_build(paths, 1, &boot) != 0) {
        CHECK(0);
        return;
    }
    method_arithmetic = compile_expression("(2 + 3) * (4 + 4)");
    CHECK(method_arithmetic != ST_OOP_INVALID);
    if (method_arithmetic == ST_OOP_INVALID)
        return;

    ST_store_seq(&wrong_answers, 0);
    ST_store_seq(&evaluations, 0);
    ST_store_seq(&collections, 0);

    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();
    CHECK_EQ_INT(WORKER_start(0, worker_with_collections, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %d collections ran while %u threads interpreted %d expressions\n",
           ST_load_seq(&collections), workers, ST_load_seq(&evaluations));
    CHECK(ST_load_seq(&collections) > 0);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);

    ST_interp_unregister();
    OM_shutdown();
}

int
main(void)
{
    ST_TEST_BEGIN("parallel interpretation");

    test_parallel_execution();
    test_collection_under_execution();

    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: parallel interpretation needs the 64-bit memory\n");
    return 0;
}

#endif
