/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  What the 1983 library shares without a lock, on real workers.
 *
 *  doc/CONCURRENCY.md asks for an audit of every piece of state the class
 *  library leans on the green scheduler to protect.  This is the gate on
 *  that audit's findings: each section below is a thing that lost data or
 *  hung when eight or thirty-one workers used it at once, and each is now
 *  serialized, replicated or reorganized in lib/.  The numbers in the
 *  comments are what the first run of each kernel answered, before the
 *  fix, so that a regression can be recognised by its size.
 *
 *      make OM=mt TSAN=1 test
 *
 *  Two harnesses.  The first runs one kernel per worker directly, the way
 *  test_parallel_lib does, for kernels that never block: each answers a
 *  count, and the sum has exactly one right value.  The second is for
 *  Delay and yield, which block and migrate: every worker runs the
 *  scheduler until all the kernels, forked as green processes, have
 *  finished -- because a worker that stops at the first process to
 *  return `off the bottom' stops at somebody else's.  Those two sections
 *  are the ones that found the scheduler's own faults: a nomination an
 *  idle worker never looked at, a process linked before it was parked, a
 *  running process on the ready list, a signal lost between a read and a
 *  link, a running process kept alive by nothing but the last worker's
 *  switch, and a safepoint that wrote a parked process's registers over
 *  another worker's progress.  Each is described where it was fixed, in
 *  src/sched/st_sched.c and src/interp/interp.c.
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
#include <unistd.h>

#define PER_KERNEL_DELAYS   20

/*
 *  ----------  Kernels that never block: one per worker, answer a count  ----------
 *
 *  Each is a {setup on one worker, kernel on every worker, check on one
 *  worker} triple.  %u in a source is the pool size.
 */
typedef struct {
    const char *name;
    const char *setup;          /*  may be NULL  */
    const char *kernel;         /*  answers an Integer  */
    long        per_worker;     /*  what each worker must answer  */
    const char *check;          /*  may be NULL; answers an Integer  */
    long        check_expected; /*  ... which must equal this  */
} kernel;

static const kernel kernels[] = {
  { "Symbol intern: -- the same 300 new strings interned on every worker",
    " Smalltalk at: #SharedTestSlots put: (Array new: 64). ^0",
    /*
     *  Every worker interns the same three hundred strings, none of them a
     *  Symbol yet, and files what it got.  The check asks whether every
     *  worker got the SAME Symbol for each: two workers that both missed
     *  and both made one gave 132 of 600 different at two workers and
     *  1,170 of 2,400 at eight.
     */
    "| syms | syms := (1 to: 300) collect: [:k | ('sharedTest%u_', k printString) asSymbol]."
    " (Smalltalk at: #SharedTestSlots) at: Processor activeWorkerIndex + 1 put: syms. ^300",
    300,
    "| slots bad | slots := (Smalltalk at: #SharedTestSlots) reject: [:s | s isNil]. bad := 0."
    " 1 to: 300 do: [:k | | first | first := slots first at: k."
    "   slots do: [:s | (s at: k) == first ifFalse: [bad := bad + 1]]]. ^bad",
    0 },
  { "Smalltalk at:put: -- 200 new globals per worker",
    " Smalltalk at: #SharedTestWorkers put: %u. ^0",
    /*  Eight workers lost 245 of their 1,600 globals to one grow.  */
    "| w | w := Processor activeWorkerIndex printString."
    " 1 to: 200 do: [:i | Smalltalk at: ('SharedTestG%u_', w, '_', i printString) asSymbol put: i]. ^200",
    200,
    "| bad | bad := 0. 0 to: (Smalltalk at: #SharedTestWorkers) - 1 do: [:w | 1 to: 200 do: [:i |"
    "  (Smalltalk at: ('SharedTestG%u_', w printString, '_', i printString) asSymbol ifAbsent: [nil]) = i"
    "     ifFalse: [bad := bad + 1]]]. ^bad",
    0 },
  { "Object addDependent: -- two dependents on 100 models per worker",
    NULL,
    /*  One IdentityDictionary for every model: 143 of 1,600 lost at eight.  */
    "| bad | bad := 0. 1 to: 100 do: [:i | | m | m := Object new. m addDependent: i. m addDependent: i + 1."
    " m dependents size = 2 ifFalse: [bad := bad + 1]. m release]. ^100 - bad",
    100, NULL, 0 },
  /*
   *  Not here: CompiledMethod>>setTempNamesIfCached:, the other cache read
   *  once.  Its reader is ContextPart>>tempNames, which on a miss parses
   *  the method's source, and this harness has no source file to parse.
   *  The fix is one read into a temporary and is held by inspection.
   */
  { "Compiler evaluate: -- 3 + 4, 25 times per worker",
    NULL,
    /*
     *  Evaluation used to install #DoIt in the receiver's class and remove
     *  it again: 138 of 800 answers were nil at eight workers, each one a
     *  worker whose DoIt another had just removed.  Primitive 188 now.
     */
    "| good | good := 0. 1 to: 25 do: [:i |"
    " (Compiler evaluate: '3 + 4' for: nil notifying: nil logged: false) = 7 ifTrue: [good := good + 1]]. ^good",
    25, NULL, 0 },
  { "Smalltalk classNames -- read while another worker flushes the cache, 10 times",
    NULL,
    /*
     *  Ten, not a hundred: every read after a flush walks the whole of
     *  Smalltalk and sorts three hundred names, and under ThreadSanitizer
     *  with thirty-one workers a hundred of those per worker ran for over
     *  an hour.  Ten per worker is three hundred rebuilds racing three
     *  hundred flushes, which is what the read-once fix is being asked.
     */
    "| good | good := 0. 1 to: 10 do: [:i | Smalltalk flushClassNameCache."
    " Smalltalk classNames size > 100 ifTrue: [good := good + 1]]. ^good",
    10, NULL, 0 },
  { "Transcript show: -- 50 lines per worker, headless",
    NULL,
    /*
     *  One WriteStream on one String, which grows by become:.  Eight
     *  workers showing lines at once were eight threads writing into a
     *  String being replaced under them; ThreadSanitizer saw the freed
     *  String's memory reused while a worker still wrote to it.  Locked
     *  per send now, in lib/Concurrency/TextCollector.extension.st.
     */
    " 1 to: 50 do: [:i | Transcript show: 'abcdefghij'; cr]. ^50",
    50, NULL, 0 },
  { "one FileDirectory, textFile: and close -- 50 times per worker",
    /*
     *  The image has no Disk here -- the running system makes one -- so
     *  the setup makes a directory of its own and every worker shares it,
     *  the way every worker shares Disk.  The setup answers 0 only if a
     *  file really opened; the kernel counts only files that did.
     *
     *  A FILE OF ITS OWN, and not one of the repository's.  This opened
     *  README.md, which textFile: opens to write, and closing a stream that
     *  was never given a mode shortens it where it stands: FileStream>>
     *  writing answers `rwmode == nil ifTrue: [self readWriteShorten.
     *  ^true]' and close then truncates at position zero.  That was dormant
     *  only because FilePool held 3 for Shorten, so the bit test never
     *  matched and close flushed instead; the moment Shorten became the bit
     *  it is meant to be, this suite emptied README.md.  See File class>>
     *  initialize in lib/Files-Fixes.
     */
    "| d f | d := PosixFileDirectory new. Smalltalk at: #SharedTestDisk put: d."
    " f := d textFile: 'st80-parallel-file-test.txt'. f isNil ifTrue: [^-1]."
    " f close. ^0",
    "| d good | d := Smalltalk at: #SharedTestDisk. good := 0."
    " 1 to: 50 do: [:i | | f | f := d textFile: 'st80-parallel-file-test.txt'."
    "   f isNil ifFalse: [f close. good := good + 1]]. ^good",
    50, NULL, 0 },
};

static st_oop           kernel_method;
static st_oop           single_method;
static st_atomic_int    reported;
static st_atomic_int    wrong_answers;
static long             single_answer;
static int              single_is_int;

static void
provide_test_roots(om_visit_fn visit)
{
    BOOT_provide_roots(visit);
    if (OM_is_object(kernel_method))
        visit(kernel_method);
    if (OM_is_object(single_method))
        visit(single_method);
}

/*
 *  How many workers: ST_LIB_WORKERS, or one per core -- except under
 *  ThreadSanitizer, where four.  Every kernel here contends for one lock,
 *  and the sanitizer multiplies each acquisition by twenty or more: at
 *  thirty-one workers the classNames kernel alone ran for over an hour,
 *  and at eight the whole gate did not finish in ten minutes.  Four is
 *  enough contention to race on -- every fault above was seen at two --
 *  and finishes in nine minutes with the sanitizer watching.
 */
static unsigned
want_workers(void)
{
    const char *text = getenv("ST_LIB_WORKERS");

    if (text)
        return (unsigned) atoi(text);
#if defined(__SANITIZE_THREAD__)
    return 4;
#else
    return 0;
#endif
}

static void
kernel_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) self; (void) user;
    ST_interp_register();
    value = run_method(kernel_method);
    if (OM_is_int(value))
        ST_fetch_add_relaxed(&reported, (int) OM_int_value(value));
    else
        ST_fetch_add_relaxed(&wrong_answers, 1);
    ST_interp_unregister();
}

static void
single_worker(st_worker *self, void *user)
{
    st_oop  value;

    (void) self; (void) user;
    ST_interp_register();
    value = run_method(single_method);
    single_is_int = OM_is_int(value);
    single_answer = single_is_int ? (long) OM_int_value(value) : -1;
    ST_interp_unregister();
}

/*  A body that does nothing: for learning how big a pool of 0 is.  */
static void
idle_worker(st_worker *self, void *user)
{
    (void) self; (void) user;
}

/*  Run `fmt' (with the pool size substituted) on one worker.  */
static int
run_single(const char *fmt, unsigned n)
{
    char    source[4096];

    snprintf(source, sizeof source, fmt, n, n, n);
    single_method = compile_expression(source);
    if (single_method == ST_OOP_INVALID)
        return 0;
    single_is_int = 0;
    single_answer = -1;
    WORKER_start(1, single_worker, NULL);
    WORKER_stop();
    ST_interp_register();
    single_method = ST_OOP_INVALID;
    return 1;
}

static void
run_kernel(const kernel *k)
{
    char        source[4096];
    unsigned    n = want_workers();
    unsigned    workers;

    if (n == 0) {
        /*  The pool decides its own size; learn it, so %u can be right.  */
        WORKER_start(0, idle_worker, NULL);
        n = WORKER_count();
        WORKER_stop();
    }
    if (k->setup) {
        CHECK(run_single(k->setup, n));
        CHECK(single_is_int);
        CHECK_EQ_INT((int) single_answer, 0);
    }
    snprintf(source, sizeof source, k->kernel, n, n, n);
    kernel_method = compile_expression(source);
    CHECK(kernel_method != ST_OOP_INVALID);
    if (kernel_method == ST_OOP_INVALID)
        return;
    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    CHECK_EQ_INT(WORKER_start(n, kernel_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    ST_interp_register();
    kernel_method = ST_OOP_INVALID;

    printf("  %u threads: %s\n", workers, k->name);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    CHECK_EQ_INT(ST_load_seq(&reported), (int) (workers * k->per_worker));
    if (k->check) {
        CHECK(run_single(k->check, workers));
        CHECK(single_is_int);
        CHECK_EQ_INT((int) single_answer, (int) k->check_expected);
    }
}

/*
 *  ----------  Kernels that block: forked processes, workers that run the scheduler  ----------
 *
 *  Each worker runs a DRIVER, which forks the real kernel as a green
 *  process and then keeps the worker in the scheduler until every kernel
 *  in the pool has counted itself done.  The kernel waits on a Delay
 *  twenty times; the driver waits in one of two ways, and each way was a
 *  separate hang: on a Delay of its own, or in Processor yield.
 */
static const char *const kernel_body =
    "[ 1 to: 20 do: [:i | (Delay forMilliseconds: 1) wait]."
    "  (Smalltalk at: #SharedTestLock) critical: ["
    "     Smalltalk at: #SharedTestDone put: (Smalltalk at: #SharedTestDone) + 1] ] fork.";

static void
driver_worker(st_worker *self, void *user)
{
    (void) self; (void) user;
    ST_interp_register();
    run_method(kernel_method);
    ST_interp_unregister();
}

static void
run_blocking(const char *name, const char *waiting)
{
    char        source[4096];
    unsigned    n = want_workers();
    unsigned    workers;

    if (n == 0) {
        WORKER_start(0, idle_worker, NULL);
        n = WORKER_count();
        WORKER_stop();
    }
    CHECK(run_single(" Smalltalk at: #SharedTestDone put: 0."
                     " Smalltalk at: #SharedTestLock put: Mutex new. ^0", n));
    snprintf(source, sizeof source,
             "%s [(Smalltalk at: #SharedTestDone) < %u] whileTrue: [%s]. ^0",
             kernel_body, n, waiting);
    kernel_method = compile_expression(source);
    CHECK(kernel_method != ST_OOP_INVALID);
    if (kernel_method == ST_OOP_INVALID)
        return;
    /*
     *  A hang here is the failure this section exists to catch, and a
     *  hung test is a test nobody reads.  Two minutes is a hundred times
     *  what a run takes.
     */
    alarm(120);
    CHECK_EQ_INT(WORKER_start(n, driver_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    alarm(0);
    ST_interp_register();
    kernel_method = ST_OOP_INVALID;

    printf("  %u threads: %s\n", workers, name);
    CHECK(run_single(" ^Smalltalk at: #SharedTestDone", workers));
    CHECK(single_is_int);
    /*  Every kernel finished: none lost its wakeup, none ran twice.  */
    CHECK_EQ_INT((int) single_answer, (int) workers);
}

/*
 *  ----------  Stopping a process that is running on another worker  ----------
 *
 *  Bugs3 B16, with B13 and B3 riding on it.  `terminate' of a process
 *  running on another core did nothing: 1983's Process>>terminate, for a
 *  process that is not the active one, unlinks it from its list and
 *  clears suspendedContext, and a process on another core is on no list
 *  and has its real stack in that core's registers.  The loop kept
 *  counting and the worker it occupied was lost for the rest of the run.
 *
 *  Every driver forks a spinner at a lower priority -- lower so that a
 *  driver coming back from a Delay is taken off the ready list before a
 *  spinner is, or two spinners that never yield would own both workers
 *  of a two-worker pool and the drivers would starve -- and then WATCHES
 *  IT RUN: it reads the spinner's counter, busies itself for a
 *  millisecond, and reads it again.  A counter that moved while this
 *  process was running is a spinner running on another worker at that
 *  moment, which is the case the fix is for and the one no single-worker
 *  test can reach.  Then terminate, and the four things that must be
 *  true afterwards: the counter stops, the spinner's ensure: block ran,
 *  its suspendedContext is nil, and resuming it raises rather than
 *  running it.
 *
 *  A driver that wakes on the worker whose spinner was preempted by the
 *  wake sees its own spinner standing still -- there is no idle worker
 *  to take it yet -- so it waits again; the next look almost always
 *  finds it running, and forty looks make the odds of never seeing it
 *  run on two workers one in a million million.  With two or more
 *  workers at least one driver must have seen its spinner running;
 *  with one, none can, and that is checked too.
 */
static void
run_terminate_across_workers(void)
{
    char        source[4096];
    unsigned    n = want_workers();
    unsigned    workers;

    if (n == 0) {
        WORKER_start(0, idle_worker, NULL);
        n = WORKER_count();
        WORKER_stop();
    }
    CHECK(run_single(" Smalltalk at: #SharedTestDone put: 0."
                     " Smalltalk at: #SharedTestNext put: 0."
                     " Smalltalk at: #SharedTestSeenRunning put: 0."
                     " Smalltalk at: #SharedTestLock put: Mutex new."
                     " Smalltalk at: #SharedTestCounts put: (Array new: %u withAll: 0)."
                     " Smalltalk at: #SharedTestEnsured put: (Array new: %u). ^0", n));
    snprintf(source, sizeof source,
        "| lock counts w p tries c1 c2 good |"
        " lock := Smalltalk at: #SharedTestLock. counts := Smalltalk at: #SharedTestCounts."
        " w := lock critical: [Smalltalk at: #SharedTestNext put: (Smalltalk at: #SharedTestNext) + 1]."
        " p := [[[true] whileTrue: [counts at: w put: (counts at: w) + 1]]"
        "        ensure: [(Smalltalk at: #SharedTestEnsured) at: w put: true]] forkAt: 3."
        " tries := 0. c1 := 0. c2 := 0."
        " [tries < 40 and: [c1 = c2]] whileTrue: ["
        "    (Delay forMilliseconds: 5) wait."
        "    c1 := counts at: w. 1 to: 20000 do: [:i | i]. c2 := counts at: w."
        "    tries := tries + 1]."
        " c1 = c2 ifFalse: [lock critical: ["
        "    Smalltalk at: #SharedTestSeenRunning put: (Smalltalk at: #SharedTestSeenRunning) + 1]]."
        " p terminate."
        " c1 := counts at: w. (Delay forMilliseconds: 20) wait. c2 := counts at: w."
        " good := c1 = c2 and: [p suspendedContext isNil"
        "    and: [((Smalltalk at: #SharedTestEnsured) at: w) == true"
        "    and: [[p resume. false] on: Error do: [:e | true]]]]."
        " lock critical: [Smalltalk at: #SharedTestDone put: (Smalltalk at: #SharedTestDone) + 1]."
        " [(Smalltalk at: #SharedTestDone) < %u] whileTrue: [(Delay forMilliseconds: 2) wait]."
        " ^good ifTrue: [1] ifFalse: [0]", n);
    kernel_method = compile_expression(source);
    CHECK(kernel_method != ST_OOP_INVALID);
    if (kernel_method == ST_OOP_INVALID)
        return;
    ST_store_seq(&reported, 0);
    ST_store_seq(&wrong_answers, 0);
    alarm(120);
    CHECK_EQ_INT(WORKER_start(n, kernel_worker, NULL), 0);
    workers = WORKER_count();
    WORKER_stop();
    alarm(0);
    ST_interp_register();
    kernel_method = ST_OOP_INVALID;

    printf("  %u threads: terminate a spinning process from another worker\n",
           workers);
    CHECK_EQ_INT(ST_load_seq(&wrong_answers), 0);
    /*  Every spinner stopped, unwound, terminated and unresumable.  */
    CHECK_EQ_INT(ST_load_seq(&reported), (int) workers);
    CHECK(run_single(" ^Smalltalk at: #SharedTestSeenRunning", workers));
    CHECK(single_is_int);
    printf("  %ld of %u spinners were seen running on another worker "
           "when terminated\n", single_answer, workers);
    if (workers >= 2)
        CHECK(single_answer >= 1);
    else
        CHECK_EQ_INT((int) single_answer, 0);
}

int
main(void)
{
    st_bootstrap_result  boot;
    st_boot_init_report  init;
    char            profile_error[256];
    unsigned        i;

    ST_TEST_BEGIN("the 1983 library's shared state, in parallel");

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
    CHECK(BOOT_install_display(640, 480));
    BOOT_run_initializers(&init);
    ST_interp_install_roots(provide_test_roots);
    ST_interp_register();

    for (i = 0; i < sizeof kernels / sizeof kernels[0]; ++i)
        run_kernel(&kernels[i]);

    run_blocking("Delay wait, twenty times per forked process; drivers waiting on Delays",
                 "(Delay forMilliseconds: 2) wait");
    run_blocking("Delay wait, twenty times per forked process; drivers in Processor yield",
                 "Processor yield");
    run_terminate_across_workers();

    ST_interp_unregister();
    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    ST_TEST_BEGIN("the 1983 library's shared state, in parallel");
    printf("skipped: this needs the 64-bit object memory\n");
    return ST_TEST_END();
}

#endif
