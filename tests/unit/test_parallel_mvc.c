/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Phase 8's exit, in one test: the 1983 class library and its interface
 *  loaded, and Smalltalk running on every core at once underneath them.
 *
 *  test_parallel_interp does this on the 36-class kernel, which proves the
 *  machinery.  This does it on the real thing -- 226 classes, a Display, a
 *  window scheduler, a Browser's worth of objects in the heap -- because the
 *  claim the project rests on is not that a toy image can be interpreted in
 *  parallel but that a Smalltalk-80 can.
 *
 *  Every worker computes answers only it can check, through library code:
 *  Collection>>inject:into:, Interval>>collect:, Fraction arithmetic and
 *  Point geometry.  A torn field, a lost reference count or a context freed
 *  under another thread shows up as arithmetic that does not add up.  Run it
 *  under the thread sanitizer, where it earns its keep:
 *
 *      make OM=mt TSAN=1 test
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

#define MANIFEST    "sources/MANIFEST"
#define MAX_SOURCES 512
#define ROUNDS      40

static char     paths[MAX_SOURCES][256];
static unsigned path_count;

static int
load_manifest(void)
{
    FILE   *f = fopen(MANIFEST, "r");
    char    line[256];

    if (!f)
        return 0;
    while (path_count < MAX_SOURCES && fgets(line, sizeof line, f)) {
        size_t  n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n)
            snprintf(paths[path_count++], sizeof paths[0], "%s", line);
    }
    fclose(f);
    return path_count > 0;
}

/*
 *  Compilation allocates, so it happens once on the main thread before the
 *  workers start.  What the workers do is execute, which is the part that
 *  has to be parallel.
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
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;

    if (strchr(expression, '^'))
        snprintf(source, sizeof source, "doIt %s", expression);
    else
        snprintf(source, sizeof source, "doIt ^%s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile \"%s\": %s\n", expression, res.error);
        return ST_OOP_INVALID;
    }
    OM_increase_ref(res.method);
    return res.method;
}

static st_oop
run_method(st_oop method)
{
    st_oop  context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);

    if (!OM_is_present(context))
        return ST_OOP_INVALID;
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
    ST_interp_run(4000000);
    if (st_vm.running)
        return ST_OOP_INVALID;
    return st_vm.return_value;
}

/*  ----------  The shared work  ----------  */

#define WORK_COUNT  4

static struct {
    const char *expression;
    st_int      answer;
    st_oop      method;
} work[WORK_COUNT] = {
    { "(1 to: 100) inject: 0 into: [:a :b | a + b]",       5050, 0 },
    { "((1 to: 20) collect: [:i | i * i]) last",            400, 0 },
    { "((3/4) + (1/4)) * 40",                                40, 0 },
    { "(0@0 corner: 12@11) area",                           132, 0 }
};

static st_atomic_int    wrong_answers;
static st_atomic_int    evaluations;
static st_atomic_int    collections;

static void
provide_test_roots(om_visit_fn visit)
{
    unsigned    i;

    /*  Installing our own replaces the bootstrap's, so chain to it.  */
    BOOT_provide_roots(visit);
    for (i = 0; i < WORK_COUNT; ++i)
        visit(work[i].method);
}

static void
library_worker(st_worker *self, void *user)
{
    unsigned    round;

    (void) user;
    ST_interp_register();

    for (round = 0; round < ROUNDS; ++round) {
        unsigned    i;

        for (i = 0; i < WORK_COUNT; ++i) {
            st_oop  value = run_method(work[i].method);

            if (!OM_is_int(value) || OM_int_value(value) != work[i].answer)
                ST_fetch_add_relaxed(&wrong_answers, 1);
            ST_fetch_add_relaxed(&evaluations, 1);
        }
        self->bytecodes += WORK_COUNT;

        /*  Collect underneath the others, from a worker rather than main.  */
        if (self->index == 0 && (round % 13) == 12) {
            OM_collect();
            ST_fetch_add_relaxed(&collections, 1);
        }
    }
    ST_interp_unregister();
}

int
main(void)
{
    st_bootstrap_result boot;
    st_boot_init_report init;
    const char         *list[MAX_SOURCES];
    unsigned            workers;
    unsigned            i;

    ST_TEST_BEGIN("the library, in parallel");

    if (!load_manifest()) {
        printf("skipped: %s not found (run from the top of the tree)\n",
               MANIFEST);
        return ST_TEST_END();
    }
    for (i = 0; i < path_count; ++i)
        list[i] = paths[i];
    if (BOOT_build(list, path_count, &boot) != 0) {
        printf("  bootstrap failed: %s\n", boot.error);
        CHECK(0);
        return ST_TEST_END();
    }
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);
    printf("  image: %u classes, %u methods, %u initializers\n",
           boot.classes_created, boot.methods_compiled, init.ran);
    /*  Three are deliberately skipped; never_initialize says why for each.  */
    CHECK_EQ_INT(init.ran + init.skipped, init.defined);

    /*  The interface is present while all this runs.  */
    CHECK(OM_is_present(BOOT_global("ScheduledControllers")));
    CHECK(OM_is_present(BOOT_global("SystemOrganization")));
    CHECK(OM_is_present(BOOT_global("DefaultTextStyle")));

    for (i = 0; i < WORK_COUNT; ++i) {
        work[i].method = compile_expression(work[i].expression);
        CHECK(work[i].method != ST_OOP_INVALID);
        if (work[i].method == ST_OOP_INVALID)
            return ST_TEST_END();
    }

    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();

    /*  Confirm the answers on one thread before asking many for them.  */
    for (i = 0; i < WORK_COUNT; ++i) {
        st_oop  value = run_method(work[i].method);

        ++st_test_checks;
        if (!OM_is_int(value) || OM_int_value(value) != work[i].answer) {
            char    text[128];

            ++st_test_failures;
            ST_print_object(value, text, sizeof text);
            printf("  FAIL %s: got %s, want %lld\n", work[i].expression, text,
                   (long long) work[i].answer);
        }
    }

    ST_store_seq(&wrong_answers, 0);
    ST_store_seq(&evaluations, 0);
    ST_store_seq(&collections, 0);

    CHECK_EQ_INT(WORKER_start(0, library_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();

    printf("  %u threads ran %d library expressions on %d CPUs,"
           " %d collections\n",
           workers, ST_load_seq(&evaluations), ST_cpu_count(),
           ST_load_seq(&collections));

    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&evaluations),
                 (int) (workers * ROUNDS * WORK_COUNT));
    CHECK(ST_load_seq(&collections) > 0);
    CHECK(workers >= 1);

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: this needs the 64-bit object memory\n");
    return 0;
}

#endif
