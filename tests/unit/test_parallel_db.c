/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The schema graph, used by real threads.
 *
 *  lib/Database is built on one connection per process, and that is what
 *  makes database access here parallel rather than merely concurrent: N
 *  workers hold N connections on N cores and the server sees N clients.
 *  Every object in the package belongs to one connection except one.
 *
 *  A DbSchemaGraph is shared.  Reading a large schema's foreign keys is the
 *  slowest thing a connection does, so an application is invited to read it
 *  once and hand the same graph to every connection it opens -- which means
 *  N workers walking one Dictionary of OrderedCollections while another
 *  adds to it.  This checks that the way test_parallel_lib checks a Mutex
 *  and test_parallel_json checks a JSONObject: by arithmetic with one
 *  correct answer, on real native threads.
 *
 *      make OM=mt TSAN=1 test
 *
 *  Two properties:
 *
 *  Every edge added is in the graph afterwards.  Each worker adds the same
 *  number of foreign keys over the same ten table pairs, so the table count
 *  is exactly twenty however many workers there were -- more would mean two
 *  workers each created the adjacency entry for one table -- and the edge
 *  count is exactly the number added.  An unguarded Dictionary loses an
 *  entry while it is growing and an unguarded OrderedCollection loses an
 *  element at add:, and both are silent.
 *
 *  A join path is found while the graph is being read by everyone else.
 *  joinPathFor:root: holds the lock across the whole search on purpose --
 *  half a path through the schema as it was and half through the schema as
 *  it became connects nothing in particular, and would be produced without
 *  any error -- so this also says that holding it does not deadlock against
 *  the readers underneath it, which is the risk a non-re-entrant Mutex
 *  brings.
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

/*  The profile, because DbFixture and Mutex both come from lib/.  */
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
 *  ----------  The two kernels  ----------
 *
 *  Both run on every worker and both reach the SAME graph: it is a class
 *  variable of DbFixture, made once by its class initializer.  A per-worker
 *  graph would test nothing.
 */

#define PER_WORKER      100
#define PER_SEARCH      20
#define SHARED_TABLES   20

/*
 *  A hundred foreign keys per worker over the same ten table pairs.  The
 *  names are built once by DbFixture and handed out ready-made, so nothing
 *  is allocated on the way into the critical section but the edge itself.
 */
static const char *const add_source =
    "| n k |"
    " n := 0."
    " 1 to: 100 do: [:i |"
    "    k := (i - 1) // 10 + 1."
    "    DbFixture graph"
    "        from: (DbFixture fromTableAt: k) column: 'id'"
    "        to: (DbFixture toTableAt: k) column: 'id'."
    "    n := n + 1]."
    " ^n";

/*
 *  Every worker searches the same graph at the same time.  A path exists by
 *  construction -- the edges were added above -- so anything but a path of
 *  exactly one edge is a graph that was read while it was inconsistent.
 */
static const char *const search_source =
    "| n |"
    " n := 0."
    " 1 to: 20 do: [:i |"
    "    (DbFixture graph"
    "        joinPathFor: (Array with: 'from1' with: 'to1') root: 'from1')"
    "            size = 1 ifTrue: [n := n + 1]]."
    " ^n";

static st_oop           setup_method;
static st_oop           add_method;
static st_oop           search_method;
static st_oop           read_tables_method;
static st_oop           read_edges_method;
static st_oop           running_method;
static st_atomic_int    reported;
static st_atomic_int    wrong_answers;

static void
provide_db_roots(om_visit_fn visit)
{
    BOOT_provide_roots(visit);
    /*
     *  A method is running while it allocates, so a collection that did not
     *  know about these would free one out from under its own activation.
     */
    if (OM_is_object(setup_method))
        visit(setup_method);
    if (OM_is_object(add_method))
        visit(add_method);
    if (OM_is_object(search_method))
        visit(search_method);
    if (OM_is_object(read_tables_method))
        visit(read_tables_method);
    if (OM_is_object(read_edges_method))
        visit(read_edges_method);
}

/*  ST_DB_WORKERS lets one worker be told apart from many.  */
static unsigned
want_workers(void)
{
    const char *text = getenv("ST_DB_WORKERS");

    return text ? (unsigned) atoi(text) : 0;
}

static void
db_worker(st_worker *self, void *user)
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
    CHECK_EQ_INT(WORKER_start(workers_wanted, db_worker, NULL), 0);
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

    ST_TEST_BEGIN("the schema graph, in parallel");

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
     *  A display, and then the class initializers -- DbFixture's shared
     *  graph is made by one, and without it every worker would send
     *  from:column:to:column: to nil.  test_parallel_lib records the same
     *  lesson.
     */
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);
    ST_interp_install_roots(provide_db_roots);
    ST_interp_register();

    add_method         = compile_expression(add_source);
    search_method      = compile_expression(search_source);
    read_tables_method = compile_expression(" ^DbFixture graph tableNames size");
    read_edges_method  = compile_expression(" ^DbFixture edgeCount");
    CHECK(add_method != ST_OOP_INVALID);
    CHECK(search_method != ST_OOP_INVALID);
    CHECK(read_tables_method != ST_OOP_INVALID);
    CHECK(read_edges_method != ST_OOP_INVALID);
    if (add_method == ST_OOP_INVALID || search_method == ST_OOP_INVALID
     || read_tables_method == ST_OOP_INVALID
     || read_edges_method == ST_OOP_INVALID)
        return ST_TEST_END();

    /*  ----------  A graph that loses no edge  ---------- */

    workers = run_on_pool(add_method, want_workers());
    printf("  %u threads added %u foreign keys each to one DbSchemaGraph\n",
           workers, (unsigned) PER_WORKER);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));

    /*
     *  Twenty tables, however many workers.  More would mean two of them
     *  each made the adjacency entry for one table and one list of edges
     *  was dropped with everything in it.
     */
    run_on_pool(read_tables_method, 1);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) SHARED_TABLES);

    run_on_pool(read_edges_method, 1);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_WORKER));

    /*  ----------  A path found while everyone else is reading  ---------- */

    workers = run_on_pool(search_method, want_workers());
    printf("  %u threads searched it %u times each\n",
           workers, (unsigned) PER_SEARCH);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * PER_SEARCH));

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    ST_TEST_BEGIN("the schema graph, in parallel");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif
