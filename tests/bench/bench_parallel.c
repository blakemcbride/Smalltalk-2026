/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  Does it actually scale?
 *
 *  This is the exit criterion doc/PLAN.md's Phase 7 set and never met, and
 *  it is the number the whole project exists to produce.  Every production
 *  Smalltalk runs its processes on one OS thread; the claim here is that
 *  Smalltalk-80 bytecodes can run on all of them at once over a shared
 *  mutable heap.  A claim like that is worth exactly what its measurement
 *  is worth, so:
 *
 *      - the TOTAL work is fixed and the workers divide it, so the numbers
 *        are speedups rather than throughput at different sizes
 *      - every worker computes something ONLY IT can check, and the sum is
 *        compared against the answer one thread produced.  A torn field or
 *        a lost reference count arrives as a wrong answer rather than as a
 *        hope that a sanitizer noticed
 *      - the time the world spends STOPPED is reported beside the speedup,
 *        because a scaling failure that cannot be attributed will be
 *        guessed at
 *
 *  Three kernels, because they measure different things:
 *
 *      mandelbrot   heavy arithmetic, almost no allocation.  The ceiling.
 *                   Fixed point rather than Float on purpose -- a Float
 *                   here is a boxed object, so a floating-point Mandelbrot
 *                   would be an allocation benchmark wearing a disguise.
 *
 *      intervals    pure interpretation: sends, blocks, one context per
 *                   activation.  Measures what the interpreter costs when
 *                   the arithmetic is trivial.
 *
 *      collections  heavy allocation and collection.  This is the one that
 *                   will not scale at first, and its number is the honest
 *                   headline rather than the one to bury.
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

/*
 *  ----------  The kernels  ----------
 *
 *  Sized so that one worker takes seconds rather than milliseconds.  The
 *  first version ran for 36 ms and measured mostly the cost of starting
 *  thirty-one threads: intervals came out SLOWER on more workers, which
 *  was true of the measurement and said nothing about the interpreter.
 *  Work large enough to swamp pool startup is the difference between a
 *  benchmark and a stopwatch pointed at the wrong thing.
 *
 *  Each slices the work by asking the VM which worker it is -- primitive
 *  243 and 244, which exist for exactly this.  So the same compiled method
 *  runs on every worker and each does a different, disjoint part, and the
 *  parts sum to an answer that does not depend on how many there were.
 */

/*
 *  Fixed point at 1/1024.  The escape test is |z|^2 > 4, which in this
 *  scale is (zr*zr + zi*zi) >> 10 > 4096; every value stays inside a
 *  SmallInteger, so nothing is allocated in the inner loop.
 */
static const char *const mandelbrot_source =
    "| total width height limit scale index step |"
    " width := 240. height := 180. limit := 96. scale := 1024."
    " total := 0."
    " index := Processor activeWorkerIndex."
    " step := Processor workerCount."
    " [index < (width * height)] whileTrue: ["
    "   | px py cr ci zr zi n done |"
    "   px := index \\\\ width. py := index // width."
    "   cr := (px * 3 * scale // width) - (2 * scale)."
    "   ci := (py * 2 * scale // height) - scale."
    "   zr := 0. zi := 0. n := 0. done := false."
    "   [done not and: [n < limit]] whileTrue: ["
    "     | zr2 zi2 |"
    "     zr2 := zr * zr // scale. zi2 := zi * zi // scale."
    "     (zr2 + zi2) > (4 * scale)"
    "        ifTrue: [done := true]"
    "        ifFalse: ["
    "           zi := (2 * zr * zi // scale) + ci."
    "           zr := zr2 - zi2 + cr."
    "           n := n + 1]]."
    "   total := total + n."
    "   index := index + step]."
    " ^total";

/*
 *  A word on the sizes here and below.
 *
 *  The first attempt ran for 36 ms and measured mostly the cost of
 *  starting thirty-one threads -- intervals came out SLOWER on more
 *  workers, which was true of the measurement and said nothing about the
 *  interpreter.  The second overcorrected into something whose
 *  SINGLE-THREADED reference run had not finished after twenty minutes,
 *  which is the other failure and the more expensive one to find.  These
 *  are sized for a few seconds per kernel at one worker: long enough that
 *  pool startup is noise, short enough that six widths across three
 *  kernels is a coffee rather than an afternoon.
 */

/*
 *  Sends and blocks and nothing else: one context per block activation,
 *  which is what makes this the interpreter's own cost.
 */
static const char *const intervals_source =
    "| total first last |"
    " first := (Processor activeWorkerIndex * 400000 // Processor workerCount)"
    "            + 1."
    " last := ((Processor activeWorkerIndex + 1) * 400000"
    "            // Processor workerCount)."
    " total := (first to: last) inject: 0 into: [:a :b | a + (b \\\\ 7)]."
    " ^total";

/*
 *  Allocation and collection.  Each round builds a collection, fills it,
 *  and drops it -- so the object table churns and the collector runs, which
 *  is the case a shared heap makes hardest.
 */
static const char *const collections_source =
    "| total first last |"
    " first := (Processor activeWorkerIndex * 3000 // Processor workerCount)"
    "            + 1."
    " last := ((Processor activeWorkerIndex + 1) * 3000"
    "            // Processor workerCount)."
    " total := 0."
    " first to: last do: [:round | | c |"
    "   c := OrderedCollection new."
    "   1 to: 40 do: [:i | c add: i * round]."
    "   total := total + c size]."
    " ^total";

/*
 *  The control.
 *
 *  Nothing but SmallInteger arithmetic in an inlined loop: no block is
 *  activated, so no context is allocated, and every value stored is a
 *  tagged integer, so no reference count is touched.  If THIS does not
 *  scale, the problem is in the interpreter's own loop and not in the
 *  object memory -- which is the first fork in the road and cannot be
 *  reasoned to from the other three kernels.
 */
static const char *const arithmetic_source =
    "| sum i first last |"
    " first := Processor activeWorkerIndex * 2000000 // Processor workerCount."
    " last := (Processor activeWorkerIndex + 1) * 2000000"
    "           // Processor workerCount."
    " sum := 0. i := first."
    " [i < last] whileTrue: [sum := sum + (i \\\\ 7). i := i + 1]."
    " ^sum";

typedef struct {
    const char *name;
    const char *source;
    st_oop      method;
    st_int      answer;         /*  what one worker computed  */
    double      one_worker_ms;
} kernel;

static kernel kernels[] = {
    { "arithmetic",  NULL, 0, 0, 0.0 },
    { "mandelbrot",  NULL, 0, 0, 0.0 },
    { "intervals",   NULL, 0, 0, 0.0 },
    { "collections", NULL, 0, 0, 0.0 }
};
#define KERNEL_COUNT (sizeof kernels / sizeof kernels[0])

static st_atomic_int    partial_total;
static st_atomic_int    wrong_answers;
static kernel          *running;

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

static void
provide_bench_roots(om_visit_fn visit)
{
    unsigned    i;

    BOOT_provide_roots(visit);
    for (i = 0; i < KERNEL_COUNT; ++i)
        visit(kernels[i].method);
}

static void
kernel_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) user;
    ST_interp_register();
    value = run_method(running->method);
    if (OM_is_int(value))
        ST_fetch_add_relaxed(&partial_total, (int) OM_int_value(value));
    else
        ST_fetch_add_relaxed(&wrong_answers, 1);
    self->bytecodes += st_vm.cycle;
    ST_interp_unregister();
}

/*
 *  Run one kernel on `count` workers and answer the wall time.
 *
 *  The answer is checked, not just timed: the parts each worker computed
 *  must sum to what one worker computed alone.  A benchmark that is only
 *  timed will happily report a beautiful speedup for work that came out
 *  wrong.
 */
static double
run_on(kernel *k, unsigned count, int *correct)
{
    int64_t     began;
    int64_t     ended;

    running = k;
    ST_store_seq(&partial_total, 0);
    ST_store_seq(&wrong_answers, 0);
    ST_store_seq(&no_context, 0);
    ST_store_seq(&out_of_budget, 0);
    WORKER_reset_safepoint_statistics();

    began = ST_time_monotonic_ns();
    if (WORKER_start(count, kernel_worker, NULL) != 0) {
        *correct = 0;
        return 0.0;
    }
    WORKER_stop();
    ended = ST_time_monotonic_ns();

    *correct = ST_load_seq(&wrong_answers) == 0
            && (st_int) ST_load_seq(&partial_total) == k->answer;
    return (double) (ended - began) / 1000000.0;
}

int
main(void)
{
    st_bootstrap_result boot;
    st_boot_init_report init;
    char                profile_error[512] = "";
    static const unsigned sweep[] = { 1, 2, 4, 8, 16, 0 };
    unsigned            cpus = (unsigned) ST_cpu_count();
    unsigned            s;
    unsigned            k;

    ST_TEST_BEGIN("parallel scaling");

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
    /*
     *  A display, because a third of the class initializers want a text
     *  style and without one they fail noisily into the debugger -- which
     *  is a page of backtrace in front of the numbers, not a wrong result.
     */
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);

    kernels[0].source = arithmetic_source;
    kernels[1].source = mandelbrot_source;
    kernels[2].source = intervals_source;
    kernels[3].source = collections_source;

    ST_interp_install_roots(provide_bench_roots);
    ST_interp_register();

    for (k = 0; k < KERNEL_COUNT; ++k) {
        kernels[k].method = compile_expression(kernels[k].source);
        CHECK(kernels[k].method != ST_OOP_INVALID);
        if (kernels[k].method == ST_OOP_INVALID)
            return ST_TEST_END();
    }

    /*
     *  The reference answers, computed on this thread with one "worker",
     *  so that every parallel run has something to be checked against.
     */
    for (k = 0; k < KERNEL_COUNT; ++k) {
        st_oop  value = run_method(kernels[k].method);

        CHECK(OM_is_int(value));
        if (!OM_is_int(value))
            return ST_TEST_END();
        kernels[k].answer = OM_int_value(value);
    }

    /*
     *  ST_BENCH_STRESS=n runs one kernel n times at full width instead of
     *  sweeping.  A wrong answer under concurrent allocation appeared once
     *  in several runs, and a sweep that takes ninety seconds is no way to
     *  chase something intermittent: this repeats the case that failed,
     *  and counts.
     */
    {
        const char *stress = getenv("ST_BENCH_STRESS");

        if (stress) {
            unsigned    rounds = (unsigned) atoi(stress);
            const char *widthv = getenv("ST_BENCH_WIDTH");
            unsigned    width = widthv ? (unsigned) atoi(widthv) : 0;
            const char *which = getenv("ST_BENCH_KERNEL");
            unsigned    failures = 0;
            unsigned    r;

            for (k = 0; k < KERNEL_COUNT; ++k)
                if (!which || strcmp(kernels[k].name, which) == 0)
                    break;
            if (k == KERNEL_COUNT)
                k = KERNEL_COUNT - 1;
            printf("  stressing %s on %d CPUs, %u rounds\n",
                   kernels[k].name, ST_cpu_count(), rounds);
            for (r = 0; r < rounds; ++r) {
                int     correct = 0;
                double  ms = run_on(&kernels[k], width, &correct);

                if (!correct) {
                    ++failures;
                    printf("  round %u: WRONG -- got %d, want %lld; %d "
                           "answered nothing (%d could not get a context, "
                           "%d ran out of budget) (%.0f ms)\n",
                           r, ST_load_seq(&partial_total),
                           (long long) kernels[k].answer,
                           ST_load_seq(&wrong_answers),
                           ST_load_seq(&no_context),
                           ST_load_seq(&out_of_budget), ms);
                }
            }
            printf("  %u of %u rounds wrong\n", failures, rounds);
            CHECK_EQ_INT((int) failures, 0);
            ST_interp_unregister();
            OM_shutdown();
            return ST_TEST_END();
        }
    }

    printf("  %u CPUs; total work fixed, divided among the workers\n", cpus);
    printf("  %-12s %8s %10s %8s %10s %7s %9s %8s\n",
           "kernel", "workers", "ms", "speedup", "stopped ms", "pauses",
           "worst ms", "answer");

    for (k = 0; k < KERNEL_COUNT; ++k) {
        for (s = 0; s < sizeof sweep / sizeof sweep[0]; ++s) {
            unsigned    want = sweep[s];
            int         correct = 0;
            double      ms;

            /*  Zero means "one per CPU"; skip counts above what we have.  */
            if (want != 0 && want > cpus)
                continue;
            ms = run_on(&kernels[k], want, &correct);
            if (kernels[k].one_worker_ms == 0.0)
                kernels[k].one_worker_ms = ms;
            {
                int     pauses = WORKER_safepoint_count();
                double  stopped = (double) WORKER_safepoint_pause_ns()
                                    / 1000000.0;

                printf("  %-12s %8u %10.1f %7.2fx %10.1f %7d %9.2f %8s\n",
                       kernels[k].name, want ? want : WORKER_count(), ms,
                       ms > 0.0 ? kernels[k].one_worker_ms / ms : 0.0,
                       stopped, pauses,
                       (double) WORKER_safepoint_worst_ns() / 1000000.0,
                       correct ? "ok" : "WRONG");
                if (pauses > 1)
                    printf("      best %.2f ms, mean %.2f ms, worst %.2f ms\n",
                           (double) WORKER_safepoint_best_ns() / 1000000.0,
                           stopped / pauses,
                           (double) WORKER_safepoint_worst_ns() / 1000000.0);
            }
            if (!correct)
                printf("      got %d, want %lld, %d worker(s) answered "
                       "no integer at all\n",
                       ST_load_seq(&partial_total),
                       (long long) kernels[k].answer,
                       ST_load_seq(&wrong_answers));
            /*
             *  The answer is checked at every width.  This is the check
             *  that makes the timings mean anything: work that came out
             *  wrong can be made arbitrarily fast.
             */
            CHECK(correct);
        }
    }

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
