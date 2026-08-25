/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The JSON containers, used by real threads.
 *
 *  lib/Json is the first package here whose objects are meant to be shared
 *  between processes.  doc/CONCURRENCY.md says the base collections are
 *  unsynchronized on purpose, and a JSONObject holds one of each -- so
 *  every message that touches them takes a Mutex.  That is exactly the
 *  arrangement in which a class can look right, read right, pass every
 *  single-threaded test and still lose a member every few million
 *  operations, which is why this is checked the way test_parallel_lib
 *  checks Mutex itself: by ARITHMETIC with one correct answer, on real
 *  native threads.
 *
 *      make OM=mt TSAN=1 test
 *
 *  Three properties, each of them something a plausible wrong
 *  implementation gets wrong and no single-threaded test can see:
 *
 *  A JSONObject must keep every name put into it.  Each worker puts
 *  PER_WORKER names that only it can generate, so the size afterwards has
 *  exactly one right value.  Two failures show up here and they are
 *  different: an unguarded Dictionary loses an entry while it is growing,
 *  and an unguarded OrderedCollection of names loses one at addLast: --
 *  and the second is the interesting one, because the value is still
 *  THERE, reachable by at:, and only the enumeration and the writer cannot
 *  see it.  A document that reads back short while every value is still
 *  present is the shape of failure this test exists to catch.
 *
 *  A JSONArray must keep every element.  Same arithmetic, and it also
 *  catches the other half of addLast:: an OrderedCollection grows by
 *  copying into a bigger Array, and two workers growing at once leave one
 *  of the two copies behind with everything written into it since.
 *
 *  A document must never be observed half-written.  Every worker
 *  repeatedly overwrites a name in one shared document and then writes the
 *  whole document out and reads it back.
 *
 *  The values are ready-made Strings from the fixture, not the loop
 *  counter, and the reason is worth keeping even now that it is history.
 *  With integer values the writer sent printString on every round trip,
 *  and 1983's SmallInteger>>printOn:base: kept its digits in a class
 *  variable shared by every integer in the image -- so eight workers
 *  printing at once read back each other's digits, and this kernel
 *  answered 3,099 of 3,100 with documents carrying 00 where 100 was put.
 *  lib/Concurrency now replaces that method and test_parallel_lib holds
 *  it, but a test of a lock is better off not also testing the printer:
 *  text that already exists keeps the subject the lock.  The text must always
 *  be parseable JSON: a writer that enumerated the live object rather than
 *  a snapshot would emit a member with no value, or a trailing comma, at
 *  the moment another worker removed or added one.  Each round trip that
 *  survives counts 1, so the total again has one right answer -- and the
 *  document stays the same SIZE throughout, so the work per round is
 *  bounded however many workers there are.
 *
 *  Both were checked against a build with the locks taken OUT, and neither
 *  answered a wrong number: the run HUNG.  1983's HashedCollection finds a
 *  key by scanning for it or for a nil slot, and a Dictionary whose
 *  invariants two writers have broken can have neither, so the scan never
 *  ends.  Worth knowing as the failure mode: a lost member is the gentle
 *  version of this, and a wedged worker pool is the one to expect.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "worker.h"
#include "st_sched.h"
#include "profile.h"
#include "st_port.h"
#include "st_atomic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  The profile, because JSONFixture and Mutex both come from lib/.  */
#define PROFILE     "profiles/st2026.profile"

static st_names     sources;
static int         *dialects;

/*
 *  Compiling and running one expression on this thread, and the green
 *  Process it has to be wrapped in.  Copied from test_parallel_lib, which
 *  carries the comments explaining why the Process and its context are made
 *  together -- a Process with a nil suspendedContext is a loaded gun, and
 *  was a segfault the first time this shape was written.
 */
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
    ctx.make_large_integer_digits = BOOT_make_large_integer_digits;
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

    /*
     *  Wrap the context in a green Process before running it.
     *
     *  A Process whose suspendedContext is nil is a loaded gun: the moment
     *  the scheduler transfers to it -- which it will, as soon as anything
     *  blocks and this one is picked off a ready list -- it calls
     *  ST_set_active_context(nil) and fetch_context_registers reads off the
     *  end of nil.  That was the segfault, named by ASAN at
     *  st_sched.c:627.
     *
     *  So the process and the context it owns are made together and are
     *  consistent from birth.  SCHED_transfer_to keeps suspendedContext up
     *  to date from here on; it only had nothing to work with because this
     *  process had never owned a context in the first place.
     */
    {
        st_oop  assoc = BOOT_lookup_global("Process", NULL);
        st_oop  cls   = OM_is_object(assoc)
                            ? OM_fetch_pointer(ST_ASSOCIATION_VALUE, assoc)
                            : ST_OOP_INVALID;

        if (OM_is_object(cls)) {
            st_oop  proc = OM_instantiate_pointers(cls, 4);

            if (OM_is_object(proc)) {
                OM_store_pointer(ST_LINK_NEXT, proc, ST_NIL);
                OM_store_pointer(ST_PROCESS_SUSPENDED_CONTEXT, proc, context);
                OM_store_pointer(ST_PROCESS_PRIORITY, proc, OM_int_oop(4));
                OM_store_pointer(ST_PROCESS_MY_LIST, proc, ST_NIL);
                OM_increase_ref(proc);
                st_vm.active_process = proc;
            }
        }
    }

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
 *  ----------  The three kernels  ----------
 *
 *  Each runs on every worker and each reaches the SAME document: they are
 *  class variables of JSONFixture, made once by its class initializer.  A
 *  per-worker document would test nothing.
 */

#define PER_WORKER      500
#define PER_ROUNDTRIP   100
#define SHARED_NAMES     10

/*
 *  Five hundred puts per worker, over the same ten names for every worker:
 *  the LOCK is what wants exercising, and ten names shared by all of them
 *  is what makes the answer afterwards exact in BOTH directions.  Fewer
 *  than ten means a name was lost; more than ten means two workers both
 *  ran `names addLast:' for a name neither had yet, which is the race
 *  at:put: is written to prevent and the one no single-threaded test can
 *  see.
 *
 *  The names are built once by JSONFixture and handed out ready-made, not
 *  built per worker.  Few names and many puts, rather than many names:
 *  the first version of this test used 15,500 distinct names and took 143
 *  seconds at sixteen workers, and what it was measuring was 1983's
 *  String>>hash -- the first character, the second-to-last and the length,
 *  which gave `key1'..`key200' ELEVEN distinct values and made a large
 *  Dictionary of String keys quadratic to fill.  That hash is gone
 *  (lib/Collections-Protocol/String.extension.st reads every character,
 *  and 992 such names now fill in 9ms where they took 1.7 seconds), but
 *  the test keeps its ten names, because ten shared by everyone is what
 *  makes the count afterwards exact, and the lock is the subject.  See
 *  doc/JSON.md for the history.
 */
static const char *const object_source =
    "| n |"
    " n := 0."
    " 1 to: 500 do: [:i |"
    "    JSONFixture object"
    "        at: (JSONFixture nameAt: (i - 1) // 50 + 1) put: i."
    "    n := n + 1]."
    " ^n";

static const char *const array_source =
    "| n |"
    " n := 0."
    " 1 to: 500 do: [:i | JSONFixture array add: 1. n := n + 1]."
    " ^n";

static const char *const roundtrip_source =
    "| n |"
    " n := 0."
    " 1 to: 100 do: [:i |"
    "    JSONFixture document"
    "        at: (JSONFixture nameAt: (i - 1) // 10 + 1)"
    "        put: (JSONFixture nameAt: 11 - ((i - 1) // 10 + 1))."
    "    n := n + ([(JSONParser parse: JSONFixture document asJsonString)"
    "                    size > 0 ifTrue: [1] ifFalse: [0]]"
    "                on: JSONError do: [:each | 0])]."
    " ^n";

static st_oop           setup_method;
static st_oop           object_method;
static st_oop           array_method;
static st_oop           roundtrip_method;
static st_oop           read_object_method;
static st_oop           read_array_method;
static st_oop           read_sum_method;
static st_oop           running_method;
static st_atomic_int    reported;
static st_atomic_int    wrong_answers;

static void
provide_json_roots(om_visit_fn visit)
{
    BOOT_provide_roots(visit);
    /*
     *  A method is running while it allocates, so a collection that did not
     *  know about these would free one out from under its own activation.
     */
    if (OM_is_object(setup_method))
        visit(setup_method);
    if (OM_is_object(object_method))
        visit(object_method);
    if (OM_is_object(array_method))
        visit(array_method);
    if (OM_is_object(roundtrip_method))
        visit(roundtrip_method);
    if (OM_is_object(read_object_method))
        visit(read_object_method);
    if (OM_is_object(read_array_method))
        visit(read_array_method);
    if (OM_is_object(read_sum_method))
        visit(read_sum_method);
}

/*  ST_JSON_WORKERS lets one worker be told apart from many.  */
static unsigned
want_workers(void)
{
    const char *text = getenv("ST_JSON_WORKERS");

    return text ? (unsigned) atoi(text) : 0;
}

static void
json_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) self;
    (void) user;
    ST_interp_register();
    value = run_method(running_method);
    if (OM_is_int(value))
        ST_fetch_add_relaxed(&reported, (int) OM_int_value(value));
    else
        ST_fetch_add_relaxed(&wrong_answers, 1);
    ST_interp_unregister();
}

/*
 *  Run one kernel on the whole pool and answer how many workers there were.
 *  Every one of these runs is the same shape, and writing it out four times
 *  is how the last three got out of step with the first.
 */
static unsigned
run_on_pool(st_oop method, unsigned workers_wanted)
{
    unsigned    workers;

    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    running_method = method;
    CHECK_EQ_INT(WORKER_start(workers_wanted, json_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    ST_interp_register();
    return workers;
}

int
main(void)
{
    st_bootstrap_result  boot;
    st_boot_init_report  init;
    char            profile_error[256];
    unsigned        workers;

    ST_TEST_BEGIN("the JSON containers, in parallel");

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
     *  A display, and then the class initializers -- JSONFixture's shared
     *  documents are made by one, and without it every worker would send
     *  at:put: to nil.  test_parallel_lib records the same lesson.
     */
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);
    ST_interp_install_roots(provide_json_roots);
    ST_interp_register();

    object_method     = compile_expression(object_source);
    array_method      = compile_expression(array_source);
    roundtrip_method  = compile_expression(roundtrip_source);
    read_object_method = compile_expression(" ^JSONFixture object size");
    read_array_method  = compile_expression(" ^JSONFixture array size");
    read_sum_method    = compile_expression(
        " ^JSONFixture array inject: 0 into: [:a :b | a + b]");
    CHECK(object_method != ST_OOP_INVALID);
    CHECK(array_method != ST_OOP_INVALID);
    CHECK(roundtrip_method != ST_OOP_INVALID);
    CHECK(read_sum_method != ST_OOP_INVALID);
    if (object_method == ST_OOP_INVALID || array_method == ST_OOP_INVALID
     || roundtrip_method == ST_OOP_INVALID
     || read_sum_method == ST_OOP_INVALID)
        return ST_TEST_END();

    /*  ----------  A JSONObject that loses no name  ---------- */

    workers = run_on_pool(object_method, want_workers());
    printf("  %u threads put %u times each into one JSONObject, over the "
           "same %u names\n",
           workers, (unsigned) PER_WORKER, (unsigned) SHARED_NAMES);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));

    /*
     *  And the object agrees.  This is the half that matters: the workers
     *  counting their own puts proves only that they ran.
     */
    run_on_pool(read_object_method, 1);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    /*
     *  Ten names, however many workers.  MORE than ten is the failure this
     *  is really watching for: without the lock, two workers reaching a
     *  name neither has yet both run `names addLast:' and the object ends
     *  up carrying it twice.
     */
    CHECK_EQ_INT(ST_load_seq(&reported), (int) SHARED_NAMES);

    /*  ----------  A JSONArray that loses no element  ---------- */

    workers = run_on_pool(array_method, want_workers());
    printf("  %u threads added %u elements each to one JSONArray\n",
           workers, (unsigned) PER_WORKER);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));

    run_on_pool(read_array_method, 1);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));
    /*
     *  Every element is a 1, so the sum is the count -- which says the
     *  elements are the ones that were added and not nils left behind by a
     *  grow that overlapped another.
     */
    run_on_pool(read_sum_method, 1);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));

    /*  ----------  A document never seen half-written  ---------- */

    workers = run_on_pool(roundtrip_method, want_workers());
    printf("  %u threads wrote and re-read one document %u times each\n",
           workers, (unsigned) PER_ROUNDTRIP);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_ROUNDTRIP));

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    ST_TEST_BEGIN("the JSON containers, in parallel");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif
