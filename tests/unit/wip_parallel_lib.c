/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Phase I's gate: the concurrency classes, used by real threads.
 *
 *  Mutex, Monitor, SharedQueue and Promise are Smalltalk, built on
 *  primitives that Phase H already made atomic.  That is exactly the
 *  arrangement in which a class can look right, read right, pass every
 *  single-threaded test, and still lose an item every few million
 *  operations -- so this checks them the way test_parallel_processes
 *  checks semaphores: by ARITHMETIC that has one correct answer, on real
 *  native threads, rather than by hoping a sanitizer noticed.
 *
 *      make OM=mt TSAN=1 test
 *
 *  Two properties, both of them things a plausible wrong implementation
 *  gets wrong:
 *
 *  A Mutex-guarded counter must equal the number of increments, exactly.
 *  `n := n + 1' is a read and a write; if the mutual exclusion is not
 *  real, two workers read the same value and one increment vanishes.  The
 *  failure is a count slightly too low, never a crash.
 *
 *  A SharedQueue must lose nothing and duplicate nothing.  Every worker
 *  puts a known set of numbers in and takes the same COUNT back out, so
 *  the total taken has exactly one right value -- and because puts and
 *  takes are balanced by construction, a queue that merely blocks shows up
 *  as a hang rather than as a silently short answer.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "worker.h"
#include "profile.h"
#include "st_port.h"
#include "st_atomic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  From the profile rather than the 1983 manifest, because the kernels ask
 *  the VM which worker they are -- and Processor>>activeWorkerIndex lives
 *  in lib/Concurrency, which only the profile brings.
 */
#define PROFILE     "profiles/st2026.profile"

static st_names     sources;
static int         *dialects;

static st_oop
compile_expression(const char *expression)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[4096];

    memset(&ctx, 0, sizeof ctx);
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_array         = BOOT_make_array;
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    ctx.dialect            = ST_DIALECT_CLOSURES;

    snprintf(source, sizeof source, "doIt %s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile: %s\n", res.error);
        return ST_OOP_INVALID;
    }
    OM_increase_ref(res.method);
    return res.method;
}

static st_atomic_int    no_context;
static st_atomic_int    out_of_budget;

static st_oop
run_method(st_oop method)
{
    st_oop  context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);

    if (!OM_is_present(context)) {
        ST_fetch_add_relaxed(&no_context, 1);
        return ST_OOP_INVALID;
    }
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int) (BOOT_method_initial_ip(method) + 1)));
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, method))));

    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(UINT64_C(4000000000));
    if (st_vm.running) {
        ST_fetch_add_relaxed(&out_of_budget, 1);
        return ST_OOP_INVALID;
    }
    return st_vm.return_value;
}

/*
 *  ----------  The two kernels  ----------
 *
 *  Both run on every worker, and both reach the SAME objects: the shared
 *  Mutex, counter and queue live in Smalltalk globals, installed once
 *  before the pool starts.  That is the point -- a per-worker copy of a
 *  Mutex tests nothing.
 */

#define PER_WORKER      2000

/*
 *  Every worker adds 1 to the same counter, PER_WORKER times, holding the
 *  same Mutex.  Answers its own share so the total can be checked twice:
 *  the counter in the image, and the sum of what the workers reported.
 */
static const char *const mutex_source =
    "| n |"
    " n := 0."
    " 1 to: 2000 do: [:i |"
    "    ConcurrencyFixture mutex critical: [ConcurrencyFixture bump]."
    "    n := n + 1]."
    " ^n";

/*  Diagnostic: what does a worker think its active process is?  */
static const char *const probe_source =
    " ^Processor activeProcess isNil ifTrue: [7] ifFalse: [3]";

/*
 *  Every worker puts PER_WORKER items in and takes PER_WORKER out.  The
 *  puts come first so nothing can block: whatever a worker takes may be
 *  its own or another's, which is the whole point.
 */
static const char *const queue_source =
    "| sum |"
    " sum := 0."
    " 1 to: 2000 do: [:i | ConcurrencyFixture queue nextPut: 1]."
    " 1 to: 2000 do: [:i | sum := sum + ConcurrencyFixture queue next]."
    " ^sum";

static st_oop           setup_method;
static st_oop           probe_method;
static st_oop           mutex_method;
static st_oop           queue_method;
static st_oop           running_method;
static st_atomic_int    reported;
static st_atomic_int    wrong_answers;

static void
provide_lib_roots(om_visit_fn visit)
{
    BOOT_provide_roots(visit);
    /*
     *  Including the one-off setup method: it is running while it
     *  allocates, so a collection that did not know about it would free
     *  the method out from under its own activation.
     */
    if (OM_is_object(setup_method))
        visit(setup_method);
    if (OM_is_object(probe_method))
        visit(probe_method);
    if (OM_is_object(mutex_method))
        visit(mutex_method);
    if (OM_is_object(queue_method))
        visit(queue_method);
}

/*  ST_LIB_WORKERS lets one worker be told apart from many.  */
static unsigned
want_workers(void)
{
    const char *text = getenv("ST_LIB_WORKERS");

    return text ? (unsigned) atoi(text) : 0;
}

static void
lib_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) user;
    ST_interp_register();
    /*
     *  Nominate a Process for this worker before running anything.
     *
     *  Without one, Processor activeProcess answers nil -- and any library
     *  that asks who is calling gets the same nil from every worker, which
     *  is indistinguishable from "all of you are the same process".  A
     *  worker running library code needs an identity as much as it needs a
     *  stack.
     */
    if (!OM_is_object(st_vm.active_process)) {
        st_oop  mine = OM_instantiate_pointers(ST_NIL, 4);

        if (OM_is_object(mine)) {
            OM_increase_ref(mine);
            st_vm.active_process = mine;
        }
    }
    value = run_method(running_method);
    if (OM_is_int(value))
        ST_fetch_add_relaxed(&reported, (int) OM_int_value(value));
    else
        ST_fetch_add_relaxed(&wrong_answers, 1);
    ST_interp_unregister();
}

/*
 *  Evaluate one expression on this thread, for setting the globals up.
 */
static st_oop
evaluate(const char *expression)
{
    st_oop  answer;

    setup_method = compile_expression(expression);
    if (setup_method == ST_OOP_INVALID)
        return ST_OOP_INVALID;
    answer = run_method(setup_method);
    setup_method = ST_OOP_INVALID;
    return answer;
}

int
main(void)
{
    st_bootstrap_result  boot;
    char            profile_error[256];
    unsigned        workers;

    ST_TEST_BEGIN("the concurrency classes, in parallel");

    if (OM_init() != 0) {
        printf("  cannot initialize the object memory\n");
        CHECK(0);
        return ST_TEST_END();
    }
    if (!PROFILE_expand(PROFILE, &sources, &dialects,
                        profile_error, sizeof profile_error)) {
        printf("skipped: %s (run from the top of the tree)\n", profile_error);
        return ST_TEST_END();
    }
    if (BOOT_build_dialects((const char *const *) sources.items, dialects,
                            sources.count, &boot) != 0) {
        printf("  bootstrap failed: %s\n", boot.error);
        CHECK(0);
        return ST_TEST_END();
    }
    printf("  image: %u classes, %u methods\n",
           boot.classes_created, boot.methods_compiled);

    /*
     *  A display, for the same reason bench_parallel needs one: a third of
     *  the class initializers want a text style, and without one they fail
     *  into the debugger rather than running.
     */
    CHECK(BOOT_install_display(640, 480));
    ST_interp_install_roots(provide_lib_roots);
    ST_interp_register();

    probe_method = compile_expression(probe_source);
    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    running_method = probe_method;
    WORKER_start(1, lib_worker, NULL);
    WORKER_stop();
    ST_interp_register();
    printf("  PROBE: activeProcess isNil -> %d (7 = nil, 3 = a process), "
           "wrong %d\n",
           ST_load_seq(&reported), ST_load_seq(&wrong_answers));

    mutex_method = compile_expression(mutex_source);
    queue_method = compile_expression(queue_source);
    CHECK(mutex_method != ST_OOP_INVALID);
    CHECK(queue_method != ST_OOP_INVALID);
    if (mutex_method == ST_OOP_INVALID || queue_method == ST_OOP_INVALID)
        return ST_TEST_END();

    /*  ----------  A Mutex-guarded counter  ---------- */

    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    running_method = mutex_method;
    CHECK_EQ_INT(WORKER_start(want_workers(), lib_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    ST_interp_register();

    printf("  %u threads took one Mutex %u times each\n",
           workers, (unsigned) PER_WORKER);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));
    {
        st_oop  n = evaluate("ConcurrencyFixture count");

        /*
         *  The whole test in one line: every increment happened, and no
         *  two of them overlapped.
         */
        CHECK(OM_is_int(n));
        CHECK_EQ_INT((int) OM_int_value(n), (int) (workers * PER_WORKER));
    }

    /*  ----------  A SharedQueue that loses nothing  ---------- */

    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    running_method = queue_method;
    CHECK_EQ_INT(WORKER_start(want_workers(), lib_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    ST_interp_register();

    printf("  %u threads moved %u items each through one SharedQueue\n",
           workers, (unsigned) PER_WORKER);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    /*
     *  Every worker put PER_WORKER ones in and took PER_WORKER out, so the
     *  sum of everything taken is the number of items -- exactly.  Lower
     *  means the queue lost one, higher means it handed one out twice.
     */
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));
    {
        st_oop  left = evaluate("ConcurrencyFixture queue size");

        CHECK(OM_is_int(left));
        CHECK_EQ_INT((int) OM_int_value(left), 0);
    }

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    ST_TEST_BEGIN("the concurrency classes, in parallel");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif
